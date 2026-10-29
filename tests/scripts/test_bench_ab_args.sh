#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# Argument checks of scripts/bench-ab.sh (--check: no lock, no runs): settings per label, values
# with ":" (pack ids), and refused variables and values (decision E-332); optional rounds and
# seconds, adaptive rounds, scenario groups and the headless form (decision E-388).
# Usage: tests/scripts/test_bench_ab_args.sh <path of bench-ab.sh>
set -u
SCRIPT=$1
DIR=$(mktemp -d "${TMPDIR:-/tmp}/bench-ab-args.XXXXXX") || exit 2
trap 'rm -rf "$DIR"' EXIT
APP="$DIR/qtcraft"
printf '#!/bin/sh\n' >"$APP"
chmod +x "$APP"
failed=0

# expect <exit code> <text the output must contain> <build arguments...>
expect() {
    expect_raw "$1" "$2" --check "$DIR/result.txt" 1 1 "$3" ${4:+"$4"}
}

# expect_raw <exit code> <text the output must contain> <all arguments of the script...>
expect_raw() {
    code=$1
    text=$2
    shift 2
    output=$(sh "$SCRIPT" "$@" 2>&1)
    status=$?
    if [ "$status" -ne "$code" ]; then
        echo "FAIL: exit $status instead of $code for: $*" >&2
        echo "$output" >&2
        failed=1
    fi
    case "$output" in
    *"$text"*) ;;
    *)
        echo "FAIL: output lacks '$text' for: $*" >&2
        echo "$output" >&2
        failed=1
        ;;
    esac
}

expect 0 "a $APP settings=-" "a=$APP"
expect 0 "hd $APP settings=TEXTURE_PACKS=builtin:qtcraft_hd_64" "hd=$APP:TEXTURE_PACKS=builtin:qtcraft_hd_64"
expect 0 "two $APP settings=TEXTURE_PACKS=builtin:a,builtin:b;PACK_SWITCH=builtin:qtcraft_hd_128" \
    "two=$APP:TEXTURE_PACKS=builtin:a,builtin:b:PACK_SWITCH=builtin:qtcraft_hd_128"
expect 0 "w $APP settings=WORLD_TYPE=flat" "a=$APP" "w=$APP:WORLD_TYPE=flat"
expect 2 "unknown variable" "x=$APP:texture_packs=builtin:a"
expect 2 "unknown variable" "x=$APP:FOO=1"
expect 2 "without blanks" "x=$APP:TEXTURE_PACKS=a b"
expect 2 "without blanks" "x=$APP:TEXTURE_PACKS=a;b"
expect 2 "without blanks" "x=$APP:TEXTURE_PACKS="
expect 2 "label used twice" "x=$APP" "x=$APP:WORLD_TYPE=flat"
expect 2 "not an executable file" "x=$DIR/missing"
# Rounds and seconds are optional: adaptive 3 to 6 rounds of 10 s by default, a number fixes the
# rounds, <min>-<max> sets the adaptive bounds.
expect_raw 0 "rounds=3-6 adaptive=1 seconds=10 scenarios=stand sprint showcase crowd" --check "$DIR/r" "a=$APP"
expect_raw 0 "rounds=4-4 adaptive=0 seconds=10" --check "$DIR/r" 4 "a=$APP"
expect_raw 0 "rounds=2-5 adaptive=1 seconds=20" --check "$DIR/r" 2-5 20 "a=$APP"
expect_raw 0 "rounds=3-6 adaptive=1 seconds=15" --check "$DIR/r" auto 15 "a=$APP"
expect_raw 2 "must be positive numbers" --check "$DIR/r" 5-2 "a=$APP"
expect_raw 2 "must be positive numbers" --check "$DIR/r" x "a=$APP"
expect_raw 0 "b $APP settings=RUN_SECONDS=20;EXPLORE_STEP=5" --check "$DIR/r" "a=$APP" "b=$APP:RUN_SECONDS=20:EXPLORE_STEP=5"

# Scenario groups: expanded in order, each scenario once; an unknown group is refused.
output=$(SCENARIOS="@core,@signal stand" sh "$SCRIPT" --check "$DIR/r" "a=$APP" 2>&1)
case "$output" in
*"scenarios=stand sprint showcase crowd signal-stress showcase-signal_components showcase-signal_machines showcase-signal_farm"*) ;;
*)
    echo "FAIL: groups not expanded: $output" >&2
    failed=1
    ;;
esac
if SCENARIOS="@nope" sh "$SCRIPT" --check "$DIR/r" "a=$APP" >/dev/null 2>&1; then
    echo "FAIL: unknown group accepted" >&2
    failed=1
fi

# The headless form takes build directories with test_scenario_measure; its default group is @server.
mkdir -p "$DIR/build/tests"
cp "$APP" "$DIR/build/tests/test_scenario_measure"
expect_raw 0 "seconds=60 scenarios=crowd gravel-collapse farm farm-seeds explore" --headless --check "$DIR/r" "a=$DIR/build"
expect_raw 2 "not a build directory" --headless --check "$DIR/r" "a=$APP"
expect_raw 2 "usage" --headless --duration-check "$DIR/r" "$APP"
exit "$failed"
