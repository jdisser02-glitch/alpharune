#include "step_driver.h"

#ifndef __EMSCRIPTEN__
#include <boost/context/protected_fixedsize_stack.hpp>
#endif

#include <stdexcept>
#include <utility>

namespace riftbound {

namespace {
#ifdef __EMSCRIPTEN__
/// Thrown inside the engine fiber when its driver is destroyed mid-game.
struct FiberAbandoned {};
#endif

// Per-fiber stack size. 256KB is plenty for the engine's deepest call
// stacks (chain resolution + triggers + cleanup) while keeping per-
// clone memory low. boost::context::protected_fixedsize_stack adds a
// guard page so stack overflow segfaults cleanly instead of corrupting
// the heap.
constexpr size_t kFiberStackBytes = 256 * 1024;
}

// ═══════════════════════════════════════════════════════════════════════════════
// Backend-independent part
// ═══════════════════════════════════════════════════════════════════════════════

StepDriver::StepDriver() = default;

Intent StepDriver::selectAction(
    const GameState& state,
    const std::vector<Intent>& legal_actions) {

    // Always present a non-empty action list to the caller. The engine
    // *should* guard against empty legal-action lists at each call site,
    // but in practice a small fraction of decisions slip through.
    std::vector<Intent> normalized;
    const std::vector<Intent>* effective = &legal_actions;
    if (legal_actions.empty()) {
        normalized.push_back(Intent::concede(state.turn.turn_player));
        effective = &normalized;
    }

    // Publish the decision snapshot for the caller, then yield.
    paused_legal_   = *effective;
    paused_player_  = state.turn.turn_player;
    at_decision_    = true;
    pending_choice_.reset();

    // Yield to caller. When the caller calls provideChoice(), they
    // resume the engine fiber, which switches back here with
    // pending_choice_ populated.
#ifdef __EMSCRIPTEN__
    swapToCaller();
    if (abandoned_) throw FiberAbandoned{};
#else
    yield_target_ = std::move(yield_target_).resume();
#endif

    at_decision_ = false;
    int chosen = pending_choice_.value_or(0);
    pending_choice_.reset();

    if (chosen < 0 || static_cast<size_t>(chosen) >= effective->size()) {
        chosen = 0;
    }
    return (*effective)[chosen];
}

void StepDriver::waitForDecision() {
    // Fiber backend: selectAction's yield ALREADY returned us here,
    // so there's nothing to wait for. The engine either yielded at a
    // decision (at_decision_ == true) or completed (done_ == true).
    // Keep this method for API compat with the previous thread-based
    // driver.
}

void StepDriver::markDone() {
    done_.store(true, std::memory_order_release);
}

#ifdef __EMSCRIPTEN__

// ═══════════════════════════════════════════════════════════════════════════════
// Emscripten (Asyncify fiber) backend
// ═══════════════════════════════════════════════════════════════════════════════

namespace {
// Asyncify parks the live wasm frames of the suspended side here on every
// swap; the engine's deepest stacks (chain → trigger → cleanup) fit well
// inside this.
constexpr size_t kAsyncifyEngineBytes = 128 * 1024;
constexpr size_t kAsyncifyCallerBytes = 64 * 1024;
}

struct StepDriver::EmFiber {
    emscripten_fiber_t caller{};
    emscripten_fiber_t engine{};
    std::unique_ptr<char[]> c_stack{new char[kFiberStackBytes]};
    std::unique_ptr<char[]> engine_async{new char[kAsyncifyEngineBytes]};
    std::unique_ptr<char[]> caller_async{new char[kAsyncifyCallerBytes]};
};

StepDriver::~StepDriver() {
    // If the fiber is still alive (engine paused at a decision, caller
    // abandoned the engine), resume it once with abandoned_ set so
    // selectAction throws and the engine stack unwinds; the entry's
    // catch marks done. Should the engine somehow survive that, fall
    // back to draining it with "first legal" choices.
    if (started_ && !done_.load(std::memory_order_acquire)) {
        abandoned_ = true;
        pending_choice_ = 0;
        swapToEngine();
    }
    while (started_ && !done_.load(std::memory_order_acquire)) {
        pending_choice_ = 0;
        swapToEngine();
    }
}

void StepDriver::emFiberEntry(void* self_ptr) {
    auto* self = static_cast<StepDriver*>(self_ptr);
    try {
        self->game_fn_();
    } catch (...) {
        // Stash-free: just mark done and hand control back.
    }
    self->done_.store(true, std::memory_order_release);
    // An Emscripten fiber's entry function must never return: park the
    // fiber for good by swapping to the caller. Nobody swaps back here
    // because done_ is set (provideChoice / the destructor check it).
    for (;;) self->swapToCaller();
}

void StepDriver::swapToEngine() {
    emscripten_fiber_swap(&em_->caller, &em_->engine);
}

void StepDriver::swapToCaller() {
    emscripten_fiber_swap(&em_->engine, &em_->caller);
}

void StepDriver::runInFiber(std::function<void()> game_fn) {
    if (started_) {
        throw std::logic_error(
            "StepDriver::runInFiber called twice — fiber already started.");
    }
    game_fn_ = std::move(game_fn);
    em_ = std::make_unique<EmFiber>();
    emscripten_fiber_init_from_current_context(
        &em_->caller, em_->caller_async.get(), kAsyncifyCallerBytes);
    emscripten_fiber_init(
        &em_->engine, &StepDriver::emFiberEntry, this,
        em_->c_stack.get(), kFiberStackBytes,
        em_->engine_async.get(), kAsyncifyEngineBytes);
    started_ = true;
    // Initial enter — runs until the first selectAction yield (or
    // until runGame returns immediately, in degenerate cases).
    swapToEngine();
}

void StepDriver::provideChoice(int legal_index) {
    if (!started_ || done_.load(std::memory_order_acquire)) {
        // Engine already finished (or never started). No-op.
        return;
    }
    pending_choice_ = legal_index;
    swapToEngine();
}

#else

// ═══════════════════════════════════════════════════════════════════════════════
// Native (boost::context::fiber) backend
// ═══════════════════════════════════════════════════════════════════════════════

StepDriver::~StepDriver() {
    // If the fiber is still alive (engine paused at a decision, caller
    // abandoned the engine), drain it by repeatedly providing a "first
    // legal" choice until it terminates. Mirrors the old thread-based
    // drain logic in GameEngine::~GameEngine, but the drain happens at
    // fiber switch time, not on a separate thread.
    while (engine_fiber_ && !done_.load(std::memory_order_acquire)) {
        pending_choice_ = 0;
        engine_fiber_ = std::move(engine_fiber_).resume();
    }
}

void StepDriver::runInFiber(std::function<void()> game_fn) {
    if (engine_fiber_) {
        throw std::logic_error(
            "StepDriver::runInFiber called twice — fiber already started.");
    }
    // Capture the function by value into the fiber's lambda. The fiber
    // entry point takes the caller's continuation (the "main fiber")
    // as its first arg; calling `.resume()` on that continuation
    // suspends the engine and returns control to the caller.
    boost::context::protected_fixedsize_stack stack_alloc(kFiberStackBytes);
    engine_fiber_ = boost::context::fiber(
        std::allocator_arg, stack_alloc,
        [this, game_fn = std::move(game_fn)](boost::context::fiber&& caller) {
            // We can't store `caller` directly in a member because the
            // fiber's resume() consumes and returns a new continuation.
            // Instead, we route every yield through a thread-local
            // (effectively fiber-local since fibers don't migrate
            // threads). When selectAction wants to yield, it reads
            // this and calls .resume() on it.
            yield_target_ = std::move(caller);
            try {
                game_fn();
            } catch (...) {
                // Stash the exception so the caller can rethrow when
                // they next call provideChoice / waitForDecision.
                // For now, just mark done and let the fiber unwind.
            }
            done_.store(true, std::memory_order_release);
            // Returning the yield_target hands control back to the
            // caller; this fiber is destroyed afterward.
            return std::move(yield_target_);
        });
    // Initial enter — runs until the first selectAction yield (or
    // until runGame returns immediately, in degenerate cases).
    engine_fiber_ = std::move(engine_fiber_).resume();
}

void StepDriver::provideChoice(int legal_index) {
    if (done_.load(std::memory_order_acquire)) {
        // Engine already finished. No-op — there's no fiber to resume.
        return;
    }
    pending_choice_ = legal_index;
    // Resume the engine fiber. It re-enters selectAction's body just
    // past the yield, reads pending_choice_, and runs forward until
    // the next selectAction yield or until runGame returns.
    engine_fiber_ = std::move(engine_fiber_).resume();
}

#endif

} // namespace riftbound
