#!/bin/sh
# Evaluates the latest series of a result file of scripts/bench-ab.sh or scripts/bench-headless.sh:
# per scenario and key the mean of each label, its spread and the difference to the first label, and
# whether the scenario needs more rounds (adaptive rounds, decision E-388). Reads only; runs nothing.
# Usage: scripts/bench-adaptive.sh [--report] <result file>
#   Without --report it prints one line per scenario: "more <scenario> <reasons>" or
#   "done <scenario>"; with --report also one line per scenario, key and label (means, spreads).
#
# The rule (all values in percent; the series' runs of every round so far, loaded runs included):
#   diff   = (mean of the label - mean of the first label) / mean of the first label, for every other label
#   spread = (largest - smallest value) / mean, per label
#   A scenario needs more rounds when for any key of KEYS
#     THRESHOLD - BAND <= |diff| <= THRESHOLD + BAND   (close to the threshold: is it above or below?)
#   or spread > SPREAD for any label                   (too noisy to tell)
#   Keys a scenario does not report, or whose means are all 0, do not count.
# Environment: KEYS (default: the "keys=" of the series header, else tick_avg_ms frame_p99_ms gpu_avg_ms
# render_cpu_avg_ms gui_cpu_avg_ms peak_rss_mb), THRESHOLD (default 10), BAND (default 5), SPREAD
# (default 25).
#
# The latest series starts at the last line "# series ..."; run lines are those starting with the
# round number: "<round> <scenario> <label> ... BENCH key=value ...". The first label is the first of
# "labels=" in the header, else the label of the first run line.

set -u

REPORT=0
if [ "${1:-}" = --report ]; then
    REPORT=1
    shift
fi
[ $# -eq 1 ] && [ -f "$1" ] || {
    echo "usage: $0 [--report] <result file>" >&2
    exit 2
}

header=$(grep '^# series ' "$1" | tail -1)
header_keys=$(printf '%s\n' "$header" | sed -n 's/.* keys=\([^ ]*\).*/\1/p' | tr ',' ' ')
labels=$(printf '%s\n' "$header" | sed -n 's/.* labels=\([^ ]*\).*/\1/p')
keys=${KEYS:-${header_keys:-"tick_avg_ms frame_p99_ms gpu_avg_ms render_cpu_avg_ms gui_cpu_avg_ms peak_rss_mb"}}

# Only the latest series.
awk 'FNR == NR { if (/^# series /) last = FNR; next } FNR >= last' "$1" "$1" | awk -v keys="$keys" -v labels="$labels" -v threshold="${THRESHOLD:-10}" -v band="${BAND:-5}" \
    -v spreadLimit="${SPREAD:-25}" -v report="$REPORT" '
function abs(x) { return x < 0 ? -x : x }
/^[0-9]+ / {
    scenario = $2
    label = $3
    if (first == "") first = label
    if (!(scenario in seen)) { seen[scenario] = 1; order[++nscen] = scenario }
    if (!((scenario, label) in known)) { known[scenario, label] = 1; byScenario[scenario] = byScenario[scenario] " " label }
    for (i = 4; i <= NF; i++) {
        split($i, kv, "=")
        if (!(kv[1] in wanted) || kv[2] !~ /^[0-9.]+$/) continue
        id = scenario SUBSEP label SUBSEP kv[1]
        v = kv[2] + 0
        if (!(id in count) || v < low[id]) low[id] = v
        if (!(id in count) || v > high[id]) high[id] = v
        count[id]++
        sum[id] += v
    }
}
BEGIN {
    n = split(keys, list, " ")
    for (i = 1; i <= n; i++) wanted[list[i]] = 1
    if (labels != "") { split(labels, given, ","); base = given[1] }
}
END {
    if (base == "") base = first
    for (s = 1; s <= nscen; s++) {
        scenario = order[s]
        reasons = ""
        m = split(byScenario[scenario], names, " ")
        for (k = 1; k <= n; k++) {
            key = list[k]
            baseId = scenario SUBSEP base SUBSEP key
            if (!(baseId in count)) continue
            baseMean = sum[baseId] / count[baseId]
            for (j = 1; j <= m; j++) {
                id = scenario SUBSEP names[j] SUBSEP key
                if (!(id in count)) continue
                mean = sum[id] / count[id]
                spread = mean > 0 ? (high[id] - low[id]) / mean * 100 : 0
                diff = baseMean > 0 ? (mean - baseMean) / baseMean * 100 : 0
                if (report)
                    printf "report %s %s %s mean=%.4g runs=%d spread=%.1f%% diff=%+.1f%%\n", scenario, key, names[j], mean, count[id], spread, diff
                if (baseMean == 0 && mean == 0) continue
                if (names[j] != base && abs(diff) >= threshold - band && abs(diff) <= threshold + band)
                    reasons = reasons " " key ":" names[j] ":diff=" sprintf("%+.1f%%", diff)
                if (spread > spreadLimit)
                    reasons = reasons " " key ":" names[j] ":spread=" sprintf("%.1f%%", spread)
            }
        }
        if (reasons == "") print "done " scenario
        else print "more " scenario reasons
    }
}'
