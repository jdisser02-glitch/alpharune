#pragma once

// Shuffles and coin flips that give the same result on every platform.
//
// std::shuffle and std::uniform_int_distribution are implementation-defined:
// libstdc++ (the native build) and libc++ (the browser build, Emscripten) draw
// different numbers from the same engine, so a game recorded in the dashboard's
// Play tab dealt other cards when replayed natively (riftbound_review). These
// follow libc++'s algorithms on a 32-bit target, the browser engine's, so the
// browser's games keep their deals and native replays reproduce them.

#include <algorithm>
#include <bit>
#include <cstdint>
#include <iterator>
#include <utility>

namespace riftbound {

/// A uniform integer in [lo, hi] (hi - lo < 2^32), drawn the way libc++'s
/// uniform_int_distribution draws from a 64-bit engine: take the low
/// ceil(log2(range)) bits of one output, reject and redraw if out of range.
template <class Engine>
int64_t portableUniform(Engine& g, int64_t lo, int64_t hi) {
    const uint32_t range = static_cast<uint32_t>(static_cast<uint64_t>(hi - lo) + 1u);
    if (range == 1) return lo;
    if (range == 0) return lo + static_cast<int64_t>(static_cast<uint32_t>(g()));
    unsigned w = 31u - static_cast<unsigned>(std::countl_zero(range));
    if ((range & (0xFFFFFFFFu >> (32u - w))) != 0) ++w;
    const uint64_t mask = w >= 64 ? ~uint64_t{0} : ((uint64_t{1} << w) - 1u);
    uint32_t u;
    do { u = static_cast<uint32_t>(static_cast<uint64_t>(g()) & mask); } while (u >= range);
    return lo + static_cast<int64_t>(u);
}

/// Fisher-Yates in libc++'s order: position k swaps with k + uniform[0, n-1-k].
template <class RandomIt, class Engine>
void portableShuffle(RandomIt first, RandomIt last, Engine& g) {
    auto d = std::distance(first, last);
    if (d <= 1) return;
    for (--last, --d; first < last; ++first, --d) {
        const auto i = portableUniform(g, 0, static_cast<int64_t>(d));
        if (i != 0) std::iter_swap(first, first + i);
    }
}

}  // namespace riftbound
