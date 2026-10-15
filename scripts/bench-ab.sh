#!/bin/sh
# Interleaved A/B benchmark of several builds (method: docs/Performance-Verlauf.md and
# docs/Performance.md). Per round and scenario every build runs once, so the builds are measured at
# nearly the same moment. The order rotates per round: round r starts with build (r - 1) mod n of the
# given list, so with n rounds every build is first and last once (a fixed order put the last build
# of a round consistently higher, decision E-331). Not part of ctest; never run it in parallel with
# builds or tests.
# Usage: scripts/bench-ab.sh [--check] <result file> [<rounds>] [<seconds>] <label>=<app>[:<VAR>=<value>...] ...
#        scripts/bench-ab.sh --duration-check <result file> <app> [<rounds>]
#   <rounds> is "auto" (the default: adaptive, ROUNDS_MIN to ROUNDS_MAX, see below), "<min>-<max>"
#   (adaptive with these bounds) or a number (exactly that many rounds for every scenario).
#   <seconds> per run, default 10 (decision E-388; 20 was the standard before).
#   <label> is a short name without "/" or blanks (e.g. a commit), <app> the qtcraft executable of a
#   release build (build/release/src/app/qtcraft.app/Contents/MacOS/qtcraft of each checkout).
#   Each ":<VAR>=<value>" after the app sets a variable of scripts/bench-common.sh for the runs of
#   that label only, e.g. hd64=<app>:TEXTURE_PACKS=builtin:qtcraft_hd_64 (one build under several
#   labels, each with other texture packs). <VAR> is one of TEXTURE_PACKS, PACK_SWITCH, WORLD_TYPE,
#   DEBUG_DISPLAY, PREVIEW_ACTIVE, EXPLORE_STEP and RUN_SECONDS (the seconds of this label's runs;
#   anything else, also in lower case, is refused); a new variable starts at ":" followed by its name
#   and "=", so values may contain ":" (pack ids) but no blanks and no ";".
#   --check only validates the arguments and prints "<label> <app> settings=<...>" per build and
#   "rounds=<min>-<max> adaptive=<0|1> seconds=<s> scenarios=<...>" (no lock, no runs;
#   tests/scripts/test_bench_ab_args.sh).
#   --duration-check measures one build at 10 s and at 20 s per run, interleaved (labels s10 and s20,
#   RUN_SECONDS), 6 rounds unless given, and prints scripts/bench-adaptive.sh --report at the end: does
#   the shorter run tell the same (docs/Performance.md, "Laufdauer prüfen")?
#   SCENARIOS="..." picks scenarios and groups (scripts/bench-groups.sh; default @core: stand sprint
#   showcase crowd), VIEW the view distance (default 8).
#
# Adaptive rounds (decision E-388): every scenario runs ROUNDS_MIN rounds (default 3); then
# scripts/bench-adaptive.sh decides per scenario, from the KEYS (default frame_p99_ms gpu_avg_ms
# render_cpu_avg_ms gui_cpu_avg_ms tick_avg_ms peak_rss_mb), whether the difference to the first label
# lies within BAND (5) of the THRESHOLD (10 %) or a label's spread exceeds SPREAD (25 %); only those
# scenarios run the further rounds up to ROUNDS_MAX (default 6). The result file notes the decision in
# a line "# adaptive after round <n>: more=<...> done=<...>", and every run line carries adaptive=1
# for a round beyond ROUNDS_MIN.
#
# The script holds the benchmark lock ~/.cache/qtcraft/bench.lock (a directory with an owner file)
# for the whole series and removes it at the end, also on errors and interrupts. If someone else
# holds it, the script waits until it is gone; a lock left by a bench script whose process no longer
# runs is taken over.
#
# The result file gets a header line per series ("# series ...": date, rounds, seconds, view,
# scenarios, labels, keys, builds) and one line per run:
#   <round> <scenario> <label> pos=<k>/<n> order=<labels of the round> load=<before>/<after>
#   busy=<before>/<after> loaded=<0|1> settings=<VAR=value;...|-> adaptive=<0|1> BENCH ...
# load is the 1-minute load average before and after the run. busy lists foreign processes as
# "name:percent" pairs (or "-"): any process of 80 % CPU or more except the window server and
# kernel_task (UI and system), and every qtcraft process whatever its CPU (the game measured is
# not running while the samples are taken, so a qtcraft there belongs to another session). Before
# each run the script waits up to 3 minutes (QUIET_WAIT seconds) for a quiet machine (load at most 6,
# nothing busy); when it is still not quiet, it measures anyway. A run is marked loaded=1 when a sample before or
# after it shows a load above 6 or a busy process. Before the first round it rests 30 s more (5 s
# headless; REST=<s> sets it).
# A run that cannot be measured because the screen stays locked ends the series (exit 3).
#
# scripts/bench-headless.sh runs this script with --headless: the same series for the server side
# of the scenarios, without a window and without real time (see there).

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
. "$PROJECT_DIR/scripts/bench-groups.sh"

HEADLESS=0
TOOL=bench-ab.sh
if [ "${1:-}" = --headless ]; then
    HEADLESS=1
    TOOL=bench-headless.sh
    shift
fi
usage() {
    if [ "$HEADLESS" -eq 1 ]; then
        echo "usage: scripts/bench-headless.sh [--check] <result file> [<rounds>] [<game seconds>] <label>=<build dir>[:<VAR>=<value>...] ..." >&2
    else
        echo "usage: $0 [--check] <result file> [<rounds>] [<seconds>] <label>=<app>[:<VAR>=<value>...] ..." >&2
        echo "       $0 --duration-check <result file> <app> [<rounds>]" >&2
    fi
    exit 2
}
CHECK_ONLY=0
DURATION_CHECK=0
case "${1:-}" in
--check)
    CHECK_ONLY=1
    shift
    ;;
--duration-check)
    [ "$HEADLESS" -eq 0 ] && [ $# -ge 3 ] || usage
    DURATION_CHECK=1
    result=$2
    app=$3
    rounds=${4:-6}
    shift $#
    set -- "$result" "$rounds" 20 "s10=$app:RUN_SECONDS=10" "s20=$app:RUN_SECONDS=20"
    ;;
esac
[ $# -ge 2 ] || usage
RESULT=$1
shift

# Rounds and seconds are optional; builds always contain "=".
ROUNDS_ARG=auto
if [ $# -gt 0 ]; then
    case "$1" in
    *=*) ;;
    *)
        ROUNDS_ARG=$1
        shift
        ;;
    esac
fi
if [ "$HEADLESS" -eq 1 ]; then
    SECONDS_PER_RUN=60
else
    SECONDS_PER_RUN=10
fi
if [ $# -gt 0 ]; then
    case "$1" in
    *=*) ;;
    *)
        SECONDS_PER_RUN=$1
        shift
        ;;
    esac
fi
[ $# -ge 1 ] || usage

case "$ROUNDS_ARG" in
auto)
    ROUNDS_MIN=${ROUNDS_MIN:-3}
    ROUNDS_MAX=${ROUNDS_MAX:-6}
    ;;
*-*)
    ROUNDS_MIN=${ROUNDS_ARG%-*}
    ROUNDS_MAX=${ROUNDS_ARG#*-}
    ;;
*)
    ROUNDS_MIN=$ROUNDS_ARG
    ROUNDS_MAX=$ROUNDS_ARG
    ;;
esac
for number in "$ROUNDS_MIN" "$ROUNDS_MAX" "$SECONDS_PER_RUN"; do
    case "$number" in
    '' | *[!0-9]*) echo "rounds (auto, <min>-<max> or a number) and seconds must be positive numbers: $ROUNDS_ARG $SECONDS_PER_RUN" >&2 && usage ;;
    esac
done
[ "$ROUNDS_MIN" -ge 1 ] && [ "$ROUNDS_MAX" -ge "$ROUNDS_MIN" ] && [ "$SECONDS_PER_RUN" -ge 1 ] || {
    echo "rounds must be positive numbers with <min> <= <max>, seconds a positive number: $ROUNDS_ARG $SECONDS_PER_RUN" >&2 && usage
}
ADAPTIVE=0
[ "$ROUNDS_MAX" -gt "$ROUNDS_MIN" ] && ADAPTIVE=1

if [ "$HEADLESS" -eq 1 ]; then
    SCENARIO_LIST=$(expand_scenarios "${SCENARIOS:-@server}") || usage
    KEYS=${KEYS:-"tick_avg_ms tick_p99_ms peak_rss_mb"}
else
    SCENARIO_LIST=$(expand_scenarios "${SCENARIOS:-@core}") || usage
    KEYS=${KEYS:-"frame_p99_ms gpu_avg_ms render_cpu_avg_ms gui_cpu_avg_ms tick_avg_ms peak_rss_mb"}
fi
[ -n "$SCENARIO_LIST" ] || usage
VARIABLES="TEXTURE_PACKS, PACK_SWITCH, WORLD_TYPE, DEBUG_DISPLAY, PREVIEW_ACTIVE, EXPLORE_STEP or RUN_SECONDS"

# Splits a build argument ($1): sets label, app and settings (the ":<VAR>=<value>" parts without
# the ":", one per line).
split_build() {
    label=${1%%=*}
    rest=${1#*=}
    app=$(printf '%s\n' "$rest" | sed 's/:[A-Z_][A-Z0-9_]*=.*//')
    settings=$(printf '%s\n' "${rest#"$app"}" | sed 's/:\([A-Z_][A-Z0-9_]*=\)/\
\1/g' | sed '/^$/d')
}

# The settings of the current build in one word for the result line: "VAR=value;..." or "-".
settings_word() {
    word=$(printf '%s\n' "$settings" | sed '/^$/d' | paste -sd ';' -)
    echo "${word:--}"
}

# Whether the target of a build argument exists: the qtcraft executable, or for --headless a build
# directory with tests/test_scenario_measure.
check_target() {
    if [ "$HEADLESS" -eq 1 ]; then
        [ -x "$1/tests/test_scenario_measure" ] || { echo "not a build directory with tests/test_scenario_measure: $1" >&2 && exit 2; }
    else
        [ -f "$1" ] && [ -x "$1" ] || { echo "not an executable file: $1" >&2 && exit 2; }
    fi
}

LABELS=
for build in "$@"; do
    case "$build" in
    *=*) ;;
    *) echo "not <label>=<app>: $build" >&2 && usage ;;
    esac
    split_build "$build"
    case "$app" in
    *:[A-Za-z_]*=*) echo "unknown variable for label $label in '${app#*:}' ($VARIABLES)" >&2 && usage ;;
    esac
    while IFS= read -r setting; do
        [ -n "$setting" ] || continue
        case "${setting%%=*}" in
        TEXTURE_PACKS | PACK_SWITCH | WORLD_TYPE | DEBUG_DISPLAY | PREVIEW_ACTIVE | EXPLORE_STEP | RUN_SECONDS) ;;
        *) echo "unknown variable for label $label: '${setting%%=*}' ($VARIABLES)" >&2 && usage ;;
        esac
        case "${setting#*=}" in
        '' | *[[:space:]]* | *\;*) echo "value of ${setting%%=*} for label $label must be non-empty, without blanks or \";\"" >&2 && usage ;;
        esac
    done <<EOF
$settings
EOF
    case "$label" in
    '' | */* | *[[:space:]]* | *,*) echo "label must be non-empty, without \"/\", \",\" or blanks: '$label'" >&2 && usage ;;
    esac
    case " $LABELS " in
    *" $label "*) echo "label used twice: $label" >&2 && usage ;;
    esac
    check_target "$app"
    LABELS="$LABELS $label"
    [ "$CHECK_ONLY" -eq 1 ] && echo "$label $app settings=$(settings_word)"
done
BUILD_COUNT=$#
LABEL_LIST=$(echo "$LABELS" | sed 's/^ //' | tr ' ' ',')
if [ "$CHECK_ONLY" -eq 1 ]; then
    echo "rounds=$ROUNDS_MIN-$ROUNDS_MAX adaptive=$ADAPTIVE seconds=$SECONDS_PER_RUN scenarios=$SCENARIO_LIST"
    exit 0
fi

LOCK_DIR="$HOME/.cache/qtcraft/bench.lock"
LOCK_OWNER="$TOOL pid=$$ dir=$PROJECT_DIR result=$RESULT"
WORK=$(mktemp -d "${TMPDIR:-/tmp}/qtcraft-ab.XXXXXX") || exit 2
HAVE_LOCK=0

# Removes the lock if this script holds it; prints where the logs are.
cleanup() {
    if [ "$HAVE_LOCK" -eq 1 ] && [ "$(cat "$LOCK_DIR/owner" 2>/dev/null)" = "$LOCK_OWNER" ]; then
        rm -f "$LOCK_DIR/owner"
        rmdir "$LOCK_DIR" 2>/dev/null
    fi
    echo "logs: $WORK" >&2
}
trap cleanup EXIT
trap 'exit 130' INT TERM HUP

. "$PROJECT_DIR/scripts/bench-common.sh"

# Takes the benchmark lock, waiting while someone else holds it.
take_lock() {
    mkdir -p "$(dirname "$LOCK_DIR")" || exit 2
    waited=0
    while ! mkdir "$LOCK_DIR" 2>/dev/null; do
        owner=$(cat "$LOCK_DIR/owner" 2>/dev/null)
        pid=$(echo "$owner" | sed -n 's/^bench-[a-z]*\.sh pid=\([0-9]*\) .*/\1/p')
        if [ -n "$pid" ] && ! kill -0 "$pid" 2>/dev/null; then
            echo "taking over the lock of a bench script that no longer runs ($owner)" >&2
            echo "$LOCK_OWNER" >"$LOCK_DIR/owner"
            HAVE_LOCK=1
            return
        fi
        [ $((waited % 600)) -eq 0 ] && echo "benchmark lock held (${owner:-no owner file}); waiting" >&2
        sleep 30
        waited=$((waited + 30))
    done
    echo "$LOCK_OWNER" >"$LOCK_DIR/owner"
    HAVE_LOCK=1
}

# 1-minute load average.
load_now() {
    sysctl -n vm.loadavg | awk '{ print $2 }'
}

# Foreign processes as name:percent (blanks in names as "_"), or "-": 80 % CPU or more except the
# window server and kernel_task, and every qtcraft process.
busy_now() {
    busy=$(ps -Ao pcpu=,comm= | awk '{
        cpu = $1
        name = $0
        sub(/^ *[0-9.]+ +/, "", name)
        sub(".*/", "", name)
        gsub(/[ ,]/, "_", name)
        if (name ~ /^qtcraft/ || (cpu >= 80 && name != "WindowServer" && name != "kernel_task"))
            printf "%s:%d,", name, cpu
    }')
    busy=${busy%,}
    echo "${busy:-"-"}"
}

# True for a load of at most 6 and no busy process ($1 load, $2 busy).
quiet() {
    awk -v l="$1" 'BEGIN { exit !(l <= 6) }' && [ "$2" = "-" ]
}

# Waits up to QUIET_WAIT seconds (default 180) for a quiet machine; 0 measures at once (smoke runs;
# the lines still say whether the machine was loaded).
wait_quiet() {
    waited=0
    while ! quiet "$(load_now)" "$(busy_now)" && [ "$waited" -lt "${QUIET_WAIT:-180}" ]; do
        sleep 10
        waited=$((waited + 10))
    done
    quiet "$(load_now)" "$(busy_now)" || echo "machine not quiet after ${QUIET_WAIT:-180} s (load $(load_now), busy $(busy_now)); measuring anyway" >&2
}

# Build number $1 (counted from 0) of the builds that follow it: sets label, app and settings.
pick_build() {
    eval "build=\${$(($1 + 2))}"
    split_build "$build"
}

# Exports the settings of the current build (called in the subshell of a run).
apply_settings() {
    while IFS= read -r setting; do
        [ -n "$setting" ] && export "${setting?}"
    done <<EOF
$settings
EOF
    return 0
}

# The server side of one scenario without a window and without real time (tests/gameplay/
# test_scenario_measure.cpp, decision E-388): the scene of scripts/bench-common.sh, <seconds> of game
# time after 3 s of warm-up. Scenarios that move the player or need the game's own benchmark actions
# (walk, sprint, liquids) or a screen (inventory, furnace, handbook) have no headless form; light
# gets the game's torch ring as timed chat lines (light_ring_chat).
# Usage: run_headless <build dir> <scenario> <seconds> <log>. Prints the BENCH line.
run_headless() {
    scene_of "$1/src/headless/qtcraft-headless" "$2" "$3" "$4.chat" || return 0
    if [ "$SCENE_WALK" = light ]; then
        light_ring_chat "$3" >>"$4.chat"
        SCENE_WALK=stand
    fi
    if [ "$SCENE_WALK" != stand ] || [ -n "$SCENE_WINDOW" ]; then
        echo "Scenario $2 moves the player or needs the window; no headless run" >&2
        return 0
    fi
    stats=${STATS:-0}
    # Every chat line is logged as a message: scenes of thousands of lines (signal-stress,
    # forest-fire) would reach QtTest's limit of 2000 and lose the BENCH line without -maxwarnings 0.
    env QTCRAFT_SCENARIO_MEASURE="$4.chat" QTCRAFT_SCENARIO_SECONDS="$3" QTCRAFT_SCENARIO_WORLD="$SCENE_WORLD" \
        QTCRAFT_SCENARIO_LAYERS="$SCENE_LAYERS" QTCRAFT_SCENARIO_SPAWN="$SCENE_SPAWN" QTCRAFT_SCENARIO_LOOK="$SCENE_LOOK" \
        QTCRAFT_SCENARIO_VIEW="${VIEW:-8}" QTCRAFT_SCENARIO_SAMPLE=$((stats / 1000)) \
        perl -e 'alarm shift; exec @ARGV or die' "${RUN_LIMIT:-600}" "$1/tests/test_scenario_measure" -maxwarnings 0 >"$4" 2>&1
    grep -o 'BENCH .*' "$4" | tail -1
}

# One run of the current build: prints its BENCH line (empty when there is none); fails when the
# screen stays locked. Runs in a subshell.
measure_run() {
    apply_settings
    if [ "$HEADLESS" -eq 1 ]; then
        run_headless "$app" "$1" "${RUN_SECONDS:-$SECONDS_PER_RUN}" "$2"
    else
        run_game "$app" "$1" "${RUN_SECONDS:-$SECONDS_PER_RUN}" "${VIEW:-8}" "$WORK/$3" "$2"
    fi
}

# Runs round $1 of the scenarios ROUND_SCENARIOS (adaptive=ROUND_ADAPTIVE in the lines) for the
# builds that follow. Sets failed on a missing result.
run_round() {
    round=$1
    first=$(((round - 1) % BUILD_COUNT))
    shift
    order=
    k=0
    while [ "$k" -lt "$BUILD_COUNT" ]; do
        pick_build $(((first + k) % BUILD_COUNT)) "$@"
        order="$order${order:+,}$label"
        k=$((k + 1))
    done
    for scenario in $ROUND_SCENARIOS; do
        k=0
        while [ "$k" -lt "$BUILD_COUNT" ]; do
            pick_build $(((first + k) % BUILD_COUNT)) "$@"
            k=$((k + 1))
            wait_quiet
            load=$(load_now)
            busy=$(busy_now)
            name="$round-$scenario-$label"
            line=$(measure_run "$scenario" "$WORK/$name.log" "$name")
            status=$?
            load_after=$(load_now)
            busy_after=$(busy_now)
            rm -rf "${WORK:?}/$name"
            if [ "$status" -ne 0 ]; then
                echo "$scenario $label round $round: screen locked, no measurement possible; series ended" >&2
                exit 3
            fi
            if [ -z "$line" ]; then
                echo "$scenario $label round $round produced no result (log: $WORK/$name.log)" >&2
                failed=1
                continue
            fi
            loaded=0
            if ! quiet "$load" "$busy" || ! quiet "$load_after" "$busy_after"; then
                loaded=1
            fi
            echo "$round $scenario $label pos=$k/$BUILD_COUNT order=$order load=$load/$load_after busy=$busy/$busy_after loaded=$loaded settings=$(settings_word) adaptive=$ROUND_ADAPTIVE $line" >>"$RESULT"
        done
    done
}

take_lock
echo "# series $(date '+%Y-%m-%d %H:%M:%S') tool=$TOOL rounds=$ROUNDS_MIN-$ROUNDS_MAX adaptive=$ADAPTIVE seconds=$SECONDS_PER_RUN view=${VIEW:-8} scenarios=$(echo "$SCENARIO_LIST" | tr ' ' ',') labels=$LABEL_LIST keys=$(echo "$KEYS" | tr ' ' ',') builds=$*" >>"$RESULT"
wait_quiet
if [ "$HEADLESS" -eq 1 ]; then
    sleep "${REST:-5}"
else
    sleep "${REST:-30}"
fi

failed=0
ROUND_SCENARIOS=$SCENARIO_LIST
ROUND_ADAPTIVE=0
round=1
while [ "$round" -le "$ROUNDS_MAX" ]; do
    if [ "$round" -eq $((ROUNDS_MIN + 1)) ]; then
        decision=$(KEYS="$KEYS" sh "$PROJECT_DIR/scripts/bench-adaptive.sh" "$RESULT")
        ROUND_SCENARIOS=$(printf '%s\n' "$decision" | awk '$1 == "more" { print $2 }' | paste -sd ' ' -)
        done_list=$(printf '%s\n' "$decision" | awk '$1 == "done" { print $2 }' | paste -sd ',' -)
        echo "# adaptive after round $ROUNDS_MIN: more=$(echo "$ROUND_SCENARIOS" | tr ' ' ','); done=$done_list" >>"$RESULT"
        printf '%s\n' "$decision" | sed -n 's/^more /# adaptive reason: /p' >>"$RESULT"
        ROUND_ADAPTIVE=1
        [ -n "$ROUND_SCENARIOS" ] || break
    fi
    run_round "$round" "$@"
    round=$((round + 1))
done
if [ "$DURATION_CHECK" -eq 1 ]; then
    sh "$PROJECT_DIR/scripts/bench-adaptive.sh" --report "$RESULT"
fi
exit $failed
