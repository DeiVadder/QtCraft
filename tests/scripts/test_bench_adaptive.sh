#!/bin/sh
# The decision of the adaptive rounds (scripts/bench-adaptive.sh, decision E-388) on canned result
# files: a scenario runs further rounds when a key's difference to the first label lies within the
# band around the threshold or a label's spread is high; only the latest series counts.
# Usage: tests/scripts/test_bench_adaptive.sh <path of bench-adaptive.sh>
set -u
SCRIPT=$1
DIR=$(mktemp -d "${TMPDIR:-/tmp}/bench-adaptive.XXXXXX") || exit 2
trap 'rm -rf "$DIR"' EXIT
failed=0

# check <name> <expected output line> [environment assignments...]: runs the script on $DIR/result.txt.
check() {
    name=$1
    expected=$2
    shift 2
    output=$(env "$@" sh "$SCRIPT" "$DIR/result.txt" 2>&1)
    case "
$output
" in
    *"
$expected
"*) ;;
    *)
        echo "FAIL ($name): expected the line '$expected' in:" >&2
        echo "$output" >&2
        failed=1
        ;;
    esac
}

# Run lines of round $1, scenario $2: label a with tick $3 and memory $4, label b with $5 and $6.
runs() {
    echo "$1 $2 a pos=1/2 order=a,b load=2.0/2.1 busy=-/- loaded=0 settings=- adaptive=0 BENCH frame_p99_ms=10.0 tick_avg_ms=$3 peak_rss_mb=$4"
    echo "$1 $2 b pos=2/2 order=a,b load=2.0/2.1 busy=-/- loaded=0 settings=- adaptive=0 BENCH frame_p99_ms=10.0 tick_avg_ms=$5 peak_rss_mb=$6"
}

{
    # An older series that would ask for more rounds everywhere: ignored.
    echo "# series 2026-10-01 10:00:00 rounds=3 seconds=20 view=8 scenarios=default builds=a=x b=y"
    runs 1 stand 1.0 400 2.0 400
    runs 1 far 1.0 400 1.0 400
    echo "# series 2026-10-15 10:00:00 tool=bench-ab.sh rounds=3-6 adaptive=1 seconds=10 view=8 scenarios=stand,crowd,near,below,far,noisy,memory,zero labels=a,b keys=tick_avg_ms,peak_rss_mb builds=a=x b=y"
    for round in 1 2 3; do
        # stand: 1 % apart, quiet -> done
        runs "$round" stand 1.00 400 1.01 401
        # crowd: +10 %, right at the threshold -> more
        runs "$round" crowd 1.00 400 1.10 400
        # near: -6 %, inside the band (5 to 15 %) -> more
        runs "$round" near 1.00 400 0.94 400
        # below: -4 %, under the band -> done
        runs "$round" below 1.00 400 0.96 400
        # far: +30 %, clearly above the band -> done (a clear regression needs no more rounds)
        runs "$round" far 1.00 400 1.30 400
        # memory: +12 % memory -> more through peak_rss_mb
        runs "$round" memory 1.00 400 1.00 448
        # zero: a key that is 0 for every label does not count
        runs "$round" zero 0 400 0 400
    done
    # noisy: equal means, but b ranges from 0.7 to 1.3 (60 % spread) -> more
    runs 1 noisy 1.00 400 0.70 400
    runs 2 noisy 1.00 400 1.00 400
    runs 3 noisy 1.00 400 1.30 400
} >"$DIR/result.txt"

check stand "done stand"
check crowd "more crowd tick_avg_ms:b:diff=+10.0%"
check near "more near tick_avg_ms:b:diff=-6.0%"
check below "done below"
check far "done far"
check memory "more memory peak_rss_mb:b:diff=+12.0%"
check zero "done zero"
check noisy "more noisy tick_avg_ms:b:spread=60.0%"

# KEYS from the environment win over the header: frame_p99_ms alone is equal everywhere.
check keys-env "done crowd" KEYS=frame_p99_ms
# The band and the threshold are configurable: with THRESHOLD=30 BAND=2 only far (+30 %) is close.
check threshold "more far tick_avg_ms:b:diff=+30.0%" THRESHOLD=30 BAND=2
check threshold-crowd "done crowd" THRESHOLD=30 BAND=2
# A higher spread limit lets noisy through.
check spread "done noisy" SPREAD=70

# Without labels= in the header the first label of the series is the base.
{
    echo "# series 2026-10-15 11:00:00 rounds=3 seconds=20 view=8 scenarios=default builds=b=y a=x"
    echo "1 stand b pos=1/2 BENCH tick_avg_ms=1.10"
    echo "1 stand a pos=2/2 BENCH tick_avg_ms=1.00"
} >"$DIR/result.txt"
check base "more stand tick_avg_ms:a:diff=-9.1%"

# labels= in the header names the base even when another label ran first.
{
    echo "# series 2026-10-15 12:00:00 rounds=3-6 labels=b,a keys=tick_avg_ms builds=b=y a=x"
    echo "1 stand a pos=1/2 BENCH tick_avg_ms=1.00"
    echo "1 stand b pos=2/2 BENCH tick_avg_ms=1.10"
} >"$DIR/labels.txt"
output=$(sh "$SCRIPT" "$DIR/labels.txt" 2>&1)
[ "$output" = "more stand tick_avg_ms:a:diff=-9.1%" ] || {
    echo "FAIL (labels): $output" >&2
    failed=1
}

# The report lists means, spreads and differences per key and label.
output=$(sh "$SCRIPT" --report "$DIR/result.txt" 2>&1)
case "$output" in
*"report stand tick_avg_ms a mean=1 runs=1 spread=0.0% diff=-9.1%"*) ;;
*)
    echo "FAIL (report): $output" >&2
    failed=1
    ;;
esac

# Usage errors.
if sh "$SCRIPT" "$DIR/missing.txt" >/dev/null 2>&1; then
    echo "FAIL: a missing file was accepted" >&2
    failed=1
fi
exit "$failed"
