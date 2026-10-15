#!/bin/sh
# Interleaved A/B measurement of the server side of benchmark scenarios, without a window and without
# real time (decision E-388): server tick mean, p99 and maximum, the tick phases, the server's
# creatures, items, falling blocks, block entities and chunks, and the memory of the measuring process
# (server plus a raw client). Per run tests/gameplay/test_scenario_measure of each build plays the
# scene of scripts/bench-common.sh (the same chat lines, world, spawn and look as the window run):
# 3 s of warm-up, then <game seconds> of game time, ticked as fast as the server can. A run takes
# seconds instead of minutes, needs no screen and no quiet desktop beyond the load check.
# Usage: scripts/bench-headless.sh [--check] <result file> [<rounds>] [<game seconds>] <label>=<build dir>[:<VAR>=<value>...] ...
#   <build dir> is a release build directory (build/release of each checkout) with the targets
#   test_scenario_measure and qtcraft-headless (the showcase stops). A build from before this
#   script gets tests/gameplay/test_scenario_measure.cpp and its line in tests/CMakeLists.txt copied
#   into its checkout (as the measure tests of stage 31.3 were).
#   <rounds>: auto (default, adaptive 3 to 6), <min>-<max> or a number; <game seconds>: default 60.
#   SCENARIOS: default @server (crowd gravel-collapse farm farm-seeds explore and the signal stations
#   of the showcase; scripts/bench-groups.sh). Walk, sprint, light, liquids and the screens have no
#   headless form and are skipped with a note. KEYS for the adaptive decision: default tick_avg_ms
#   tick_p99_ms peak_rss_mb. STATS=<ms> writes SAMPLE lines (memory, chunks, creatures) every
#   <ms> / 1000 game seconds into each run's log (the memory curve of explore). VIEW, WORLD_TYPE,
#   EXPLORE_STEP and per-label variables as in scripts/bench-ab.sh.
# Result lines, lock, load checks, rotation and adaptive rounds: as scripts/bench-ab.sh (the header
# says tool=bench-headless.sh). Example, dev against a feature branch:
#   scripts/bench-headless.sh /tmp/server.txt dev=../dev/build/release new=build/release
exec sh "$(dirname "$0")/bench-ab.sh" --headless "$@"
