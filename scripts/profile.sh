#!/bin/sh
# Deep profiling of the game (extends scripts/bench.sh). Builds the RelWithDebInfo profiling build
# (preset "profile", build/profile, QML debugging enabled for qmlprofiler) and starts short,
# time-limited benchmark runs that quit by themselves. Raw data goes to a temporary directory
# outside the repository; its path is printed at the end. Never run it in parallel with builds or
# tests.
#
# Usage: scripts/profile.sh <tool> [scenario] [seconds] [view distance]
#   breakdown  frame cost per scenario without VSync influence: CPU time per frame of the GUI and
#              render thread, world renderer, uploads, entity meshing, GPU time, meshing per
#              section, server tick, memory and section statistics (all scenarios when none is
#              given; one run each)
#   time       Instruments Time Profiler (xctrace) plus a text report of the hottest functions
#              per thread (scripts/profile-report.py)
#   alloc      heap(1) by type and vmmap(1) summary 10 s into the run
#   leaks      leaks(1) at exit, with malloc stack logging
#   metal      Instruments Metal System Trace (GPU intervals per frame)
#   sample     sample(1) of all threads for 10 s during the run
#   qml        qmlprofiler trace plus a report of the slowest bindings, signals and creations
#   sg         scene graph timing log (QSG_RENDER_TIMING, qt.scenegraph.time.*)
#   visualize  screenshots with QSG_VISUALIZE=overdraw, batches, clip and changes
# Scenarios: see scripts/bench-common.sh (default stand). Seconds: 15. View distance: 8.

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
cd "$PROJECT_DIR" || exit 2
. "$PROJECT_DIR/scripts/bench-common.sh"

TOOL=${1:-}
SCENARIO=${2:-stand}
SECONDS_PER_RUN=${3:-15}
VIEW=${4:-8}
APP="$PROJECT_DIR/build/profile/src/app/qtcraft.app/Contents/MacOS/qtcraft"
QT_BIN="$HOME/Qt/6.8.3/macos/bin"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/qtcraft-profile.XXXXXX") || exit 2
# Recording ends with the run; the limit only guards against a run that does not quit.
RECORD_LIMIT=$((SECONDS_PER_RUN + 60))

case "$TOOL" in
breakdown | time | alloc | leaks | metal | sample | qml | sg | visualize) ;;
*)
    sed -n '2,24p' "$0"
    exit 2
    ;;
esac

limited 300 cmake --preset profile >/dev/null 2>&1 || exit 1
limited 1200 cmake --build --preset profile >/dev/null 2>&1 || exit 1

# Writes a wrapper that starts the game and attaches an xctrace template to it. Attaching (not
# --launch) keeps the exact binary: a launch by bundle may pick another build of the same app.
xctrace_wrapper() {
    template=$1
    output=$2
    wrapper="$WORK/xctrace-$(echo "$template" | tr ' ' '-').sh"
    cat >"$wrapper" <<EOF
#!/bin/sh
"\$@" &
pid=\$!
perl -e 'select(undef, undef, undef, 2.0)'
xcrun xctrace record --template "$template" --time-limit ${RECORD_LIMIT}s --output "$output" --attach \$pid >"$output.log" 2>&1
wait \$pid
EOF
    chmod +x "$wrapper"
    echo "$wrapper"
}

run() {
    run_game "$APP" "$SCENARIO" "$SECONDS_PER_RUN" "$VIEW" "$WORK/data-$1" "$WORK/$1.log" ${2:-}
}

case "$TOOL" in
breakdown)
    scenarios=${2:-"stand walk sprint crowd inventory furnace handbook light liquids"}
    for SCENARIO in $scenarios; do
        line=$(run "$SCENARIO")
        echo "$SCENARIO view=$VIEW $line"
    done
    ;;
time)
    run time "$(xctrace_wrapper "Time Profiler" "$WORK/time.trace")"
    xcrun xctrace export --input "$WORK/time.trace" \
        --xpath '/trace-toc/run[@number="1"]/data/table[@schema="time-profile"]' >"$WORK/time.xml" 2>"$WORK/export.log"
    python3 -I "$PROJECT_DIR/scripts/profile-report.py" time "$WORK/time.xml" 30 >"$WORK/time-report.txt"
    head -80 "$WORK/time-report.txt"
    ;;
alloc)
    # Heap by type and memory regions 10 s into the run (heap(1), vmmap(1)). The Instruments
    # Allocations template does not finish on this machine (neither attached nor launched), see
    # docs/Performance.md.
    cat >"$WORK/heap.sh" <<EOF
#!/bin/sh
"\$@" &
pid=\$!
perl -e 'select(undef, undef, undef, 10.0)'
heap -s \$pid >"$WORK/heap.txt" 2>&1
vmmap --summary \$pid >"$WORK/vmmap.txt" 2>&1
wait \$pid
EOF
    chmod +x "$WORK/heap.sh"
    run alloc "$WORK/heap.sh"
    grep -E "^(Physical footprint|All zones)" "$WORK/heap.txt" "$WORK/vmmap.txt"
    sed -n '/COUNT/,+25p' "$WORK/heap.txt"
    ;;
metal)
    run metal "$(xctrace_wrapper "Metal System Trace" "$WORK/metal.trace")"
    ;;
leaks)
    # leaks starts the binary itself and reports what is unreachable when it exits.
    run leaks "env MallocStackLogging=1 leaks --atExit --"
    grep -E "leaks for|total leaked" "$WORK/leaks.log"
    ;;
sample)
    cat >"$WORK/sample.sh" <<EOF
#!/bin/sh
"\$@" &
pid=\$!
perl -e 'select(undef, undef, undef, 8.0)'
sample \$pid 10 -file "$WORK/sample.txt" >/dev/null 2>&1
wait \$pid
EOF
    chmod +x "$WORK/sample.sh"
    run sample "$WORK/sample.sh"
    grep -E "^ *[0-9]+ Thread_|Thread_[0-9]+" "$WORK/sample.txt" | head -20
    ;;
qml)
    run qml "$QT_BIN/qmlprofiler --output $WORK/qml.qtd"
    python3 -I "$PROJECT_DIR/scripts/profile-report.py" qml "$WORK/qml.qtd" 30 | tee "$WORK/qml-report.txt"
    ;;
sg)
    QSG_RENDER_TIMING=1 QT_LOGGING_RULES="qt.scenegraph.time.*=true" run sg
    grep -E "time\.(renderloop|renderer|texture|glyph|compilation)" "$WORK/sg.log" | tail -20
    ;;
visualize)
    for mode in overdraw batches clip changes; do
        env QSG_VISUALIZE="$mode" QTCRAFT_SCREEN="${QTCRAFT_SCREEN:-builtin}" QTCRAFT_BACKGROUND=1 QTCRAFT_DATA_DIR="$WORK/data-$mode" QTCRAFT_SCREENSHOT="$WORK/visualize-$mode.png" \
            QTCRAFT_SCREENSHOT_FRAMES=60 perl -e 'alarm shift; exec @ARGV or die' 120 "$APP" --play --seed 42 --view-distance "$VIEW" --look 45,-10 --no-audio \
            --overlay inventory >"$WORK/visualize-$mode.log" 2>&1
        ls "$WORK/visualize-$mode.png"
    done
    ;;
esac
echo "raw data: $WORK"
