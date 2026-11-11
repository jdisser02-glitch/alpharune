# Engine runtime libraries for the training machines

The engine binary built on GitHub's `ubuntu-latest` (Ubuntu 24.04) loads two Boost shared libraries that the
runner image does not ship. They used to be installed with apt on each of the 60-odd machines of a training
iteration; on 11 Nov 2026 the runners' apt mirror stopped resolving (`azure.archive.ubuntu.com`) on some machines
and four iterations in a row were lost, the last one stuck 40 minutes in the install.

`lib/` holds those two files, copied unchanged from Ubuntu 24.04's packages `libboost-context1.83.0` and
`libboost-program-options1.83.0`, version 1.83.0-2.1ubuntu3.2 (Boost Software License 1.0):

    bb223b99450e1f7acf0eef609b82f1903b4574cb26f64c098b9264347f5314a0  libboost_context.so.1.83.0
    15f34bcb75dfb0fa30af13cf2b82fb7a9a8e0fbbadf7cabcd397dd65bde552f3  libboost_program_options.so.1.83.0

`.github/workflows/riftlab.yml` (job p_plan) copies them into the plan artifact (`stage/lib`), and the self-play,
screen and gate jobs put that directory on `LD_LIBRARY_PATH`. Replace them when the runner image moves to another
Ubuntu release or Boost version (the jobs fail with "the engine's libraries are missing" if they no longer fit).
