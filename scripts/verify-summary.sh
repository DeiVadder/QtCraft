# The stamp of a two-part full run and the end of a verify run (sourced by scripts/verify.sh, not
# run): the stamp lets the second part skip the steps without a sanitizer (decision E-890) except
# the debug timing tests the first part deferred; the commands that run deferred timing tests later
# and the final line (decision E-913).
# Expects from verify-phases.sh: deferred_timing and deferred_names_<preset>; from verify.sh:
# SKIP_SHARED, SHARED_DEFERRED and the functions step and build_and_test for run_debug_step, MODE,
# CHANGED, TSAN_LABEL and SCREENSHOT for all_green_line.

# The stamp: line 1 the clean commit and the hash of the local presets (clean_commit in verify.sh),
# then `deferred-debug: <names>` when the run deferred debug timing tests; the second part runs
# those against the existing build/debug. `passed-sanitizers: <names>` lists the sanitizers that
# passed on that commit so far, in this or an earlier part (decision E-929).
full_stamp_key() {
    head -n 1 "$1" 2>/dev/null
}

full_stamp_deferred() {
    sed -n 's/^deferred-debug: //p' "$1" 2>/dev/null
}

full_stamp_sanitizers() {
    sed -n 's/^passed-sanitizers: //p' "$1" 2>/dev/null
}

# Arguments: stamp file, commit key, deferred debug timing tests (may be empty), passed sanitizers
# (may be empty or left out).
write_full_stamp() {
    {
        echo "$2"
        if [ -n "$3" ]; then
            echo "deferred-debug: $3"
        fi
        if [ -n "${4:-}" ]; then
            echo "passed-sanitizers: $4"
        fi
    } >"$1"
}

# The debug step: build and test, or in the second part of a full run only the debug timing tests
# the first part deferred, or nothing.
run_debug_step() {
    if [ "$SKIP_SHARED" -eq 0 ]; then
        build_and_test debug
    elif [ -n "$SHARED_DEFERRED" ]; then
        step "test-debug-timing" run_deferred_debug_timing "$SHARED_DEFERRED"
    fi
}

# One command per preset that deferred timing tests, each running only those tests in that
# preset: --changed selects the deferred ones (their stamps were dropped), --tests limits it to them.
deferred_timing_commands() {
    for preset in debug asan tsan; do
        eval "names=\$deferred_names_$preset"
        [ -n "$names" ] || continue
        sanitizer=""
        [ "$preset" = debug ] || sanitizer=" --sanitizers $preset"
        echo "   $preset: scripts/verify.sh --quick --changed$sanitizer --tests '^($(echo "$names" | sed 's/ /|/g'))\$'"
    done
}

# The deferred timing tests per preset on one line ("debug: a b; tsan: c").
deferred_timing_names() {
    line=""
    for preset in debug asan tsan; do
        eval "names=\$deferred_names_$preset"
        [ -n "$names" ] && line="${line:+$line; }$preset: $names"
    done
    echo "$line"
}

# The last line of a green run.
all_green_line() {
    selected=""
    if [ "$CHANGED" -eq 1 ]; then
        selected=", changed tests only"
    fi
    if [ -n "$deferred_timing" ]; then
        selected="$selected, timing tests deferred"
    fi
    if [ -n "$TSAN_LABEL" ] && [ "$CHANGED" -eq 1 ]; then
        echo "ALL GREEN (quick$selected; TSan on label $TSAN_LABEL and changed tests)"
    elif [ -n "$TSAN_LABEL" ]; then
        echo "ALL GREEN (quick, TSan on label $TSAN_LABEL$selected)"
    elif [ "$MODE" = quick ]; then
        echo "ALL GREEN (quick$selected)"
    elif [ -n "$deferred_timing" ]; then
        echo "ALL GREEN EXCEPT DEFERRED TIMING TESTS (screenshot: $SCREENSHOT): the full verify is complete except for the timing tests $(deferred_timing_names); run them later with the commands above"
    else
        echo "ALL GREEN (screenshot: $SCREENSHOT)"
    fi
}
