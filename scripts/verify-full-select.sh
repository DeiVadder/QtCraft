# The slimmer full verify (sourced by scripts/verify.sh, not run; decision E-929).
# The last fully green full verify stands in build/.verify-full-last: line 1 the key of
# clean_commit (commit and hash of the local presets CMakeUserPresets.json), line 2
# `clang-tidy: <version>`. It lies outside build/debug, so removing that tree to free disk space
# keeps it. It is written only when that commit passed every step without a sanitizer and both
# ASan and TSan, in one run or in the two parts of a two-part run: the stamp of the shared steps
# (build/debug/.verify-full-shared, E-890) collects the sanitizers that passed on its commit
# (`passed-sanitizers:` line), and the run that completes `asan tsan` writes .verify-full-last
# (finish_full_run). A dirty tree, --tests or a failed shared step writes nothing; a failed
# sanitizer step (configure, build, tests, smoke) does not count that sanitizer.
# Against that stamp the full verify
#   - runs under TSan the tests labelled `concurrency` and, per test, every other test whose own
#     inputs, command line, ENVIRONMENT or LABELS changed since it last passed the TSan step of a
#     full verify, and every test after a change of tests/tsan.supp or CMakePresets.json
#     (scripts/verify-tests.py --full-label, stamps build/tsan/.verify-full-tsan-stamps.json,
#     renewed only by that step); other runtime data alone selects nothing (as in E-412). Every
#     test runs without the stamp, without TSan stamps of a full verify, with --tests, and with
#     --all-tsan or QC_FULL_TSAN_ALL=1;
#   - runs clang-tidy, clazy and qmllint only over the files changed since that commit and the
#     files that include a changed header directly (scripts/verify-full-select.py); every file
#     without the stamp, when git does not know its commit, when the analysis setup or compile
#     settings in CMake files changed, when the local presets hash or the clang-tidy version differ
#     from the stamp, when the selection reaches 40 % of the .cpp files, and with --full-analysis or
#     QC_FULL_ANALYSIS=1. The size limits and warning-only checks always cover the whole tree.
# --release (the release milestone) implies --all-tsan and --full-analysis; a playtest build uses
# the selections.
# Both selections are named in the summary (`selection_notes`). --dry-run prints them and stops; it
# builds and tests nothing, but like every verify.sh run it may create CMakeUserPresets.json, and
# the TSan selection renews the hash cache build/tsan/.verify-hash-cache.json.
# QC_VERIFY_DRY_SINCE=<commit> shows the analysis selection against that commit instead of the stamp
# (without comparing presets and clang-tidy version).
# --seed-green <commit> writes the stamp for a commit that passed a full verify before E-929, with
# the current presets hash and clang-tidy version (seed_full_green). Its TSan stamps are not taken
# over: the first full TSan step afterwards runs every test once.
# Expects from verify.sh: PROJECT_DIR, MODE, SANITIZERS, TEST_REGEX, failures, START_COMMIT,
# FULL_STAMP, debug_deferred_names, the function clean_commit and the functions of
# verify-summary.sh (full_stamp_key, full_stamp_sanitizers, write_full_stamp).

FULL_LAST="$PROJECT_DIR/build/.verify-full-last"
FULL_TSAN_STAMPS=.verify-full-tsan-stamps.json
FULL_TSAN_LABEL=concurrency
FULL_TSAN_ALL=${QC_FULL_TSAN_ALL:-0}
FULL_ANALYSIS=${QC_FULL_ANALYSIS:-0}
CLANG_TIDY_PATH=${CLANG_TIDY:-"$HOME/Qt/Qt Creator.app/Contents/Resources/libexec/clang/bin/clang-tidy"}
selection_notes=""
SHARED_PASSED=""
for value in "$FULL_TSAN_ALL" "$FULL_ANALYSIS"; do
    case "$value" in
    0 | 1) ;;
    *) echo "QC_FULL_TSAN_ALL and QC_FULL_ANALYSIS must be 0 or 1, not '$value'" >&2 && exit 2 ;;
    esac
done

# The commit of the last fully green full verify, empty without one.
# The dry run takes QC_VERIFY_DRY_SINCE instead, to show a selection against another commit.
full_last_commit() {
    if [ "${DRY_RUN:-0}" -eq 1 ] && [ -n "${QC_VERIFY_DRY_SINCE:-}" ]; then
        echo "$QC_VERIFY_DRY_SINCE"
    else
        head -n 1 "$FULL_LAST" 2>/dev/null | cut -d' ' -f1
    fi
}

# The presets hash and the clang-tidy version the stamp names.
full_last_presets() {
    head -n 1 "$FULL_LAST" 2>/dev/null | cut -d' ' -f2
}

full_last_tidy() {
    sed -n 's/^clang-tidy: //p' "$FULL_LAST" 2>/dev/null
}

# The current hash of the local presets (as in clean_commit) and the clang-tidy version line.
presets_hash() {
    git hash-object CMakeUserPresets.json 2>/dev/null || echo none
}

tidy_version() {
    version=$("$CLANG_TIDY_PATH" --version 2>/dev/null | grep -i -m 1 version | sed 's/^ *//')
    echo "${version:-none}"
}

# Writes the stamp. Argument: the key of the commit (commit and presets hash).
write_full_last() {
    mkdir -p "$(dirname "$FULL_LAST")"
    {
        echo "$1"
        echo "clang-tidy: $(tidy_version)"
    } >"$FULL_LAST"
}

# --seed-green <commit>: the stamp for a commit that passed a full verify before E-929.
seed_full_green() {
    commit=$(git rev-parse --verify --quiet "$1^{commit}") || {
        echo "--seed-green: unknown commit '$1'" >&2
        return 2
    }
    write_full_last "$commit $(presets_hash)"
    echo "seeded $FULL_LAST:"
    cat "$FULL_LAST"
}

# One line for the summary.
selection_note() {
    selection_notes="$selection_notes
$1"
}

# Number of tests in build/<preset> (ctest -N).
count_tests() {
    ctest --test-dir "build/$1" -N 2>/dev/null | sed -n 's/^Total Tests: *//p'
}

# The TSan selection of a full run: sets full_selection (ctest regex, empty: none) and succeeds, or
# sets full_reason and fails, and the caller runs every test. Argument: extra options of
# verify-tests.py select (--no-pending for the dry run).
full_tsan_select() {
    full_selection=""
    last=$(full_last_commit)
    if [ "$FULL_TSAN_ALL" -eq 1 ]; then
        full_reason="--all-tsan, --release or QC_FULL_TSAN_ALL=1"
    elif [ -n "$TEST_REGEX" ]; then
        full_reason="--tests given"
    elif [ -z "$last" ]; then
        full_reason="no green full verify recorded in $FULL_LAST"
    elif [ ! -f "build/tsan/$FULL_TSAN_STAMPS" ]; then
        full_reason="no TSan stamps of a full verify in build/tsan yet"
    elif full_selection=$(python3 scripts/verify-tests.py select --build-dir build/tsan --changed \
        --full-label "$FULL_TSAN_LABEL" --stamps "$FULL_TSAN_STAMPS" "$@"); then
        return 0
    else
        full_reason="selection failed"
    fi
    return 1
}

# Count of the names in a ctest regex ^(a|b)$.
regex_count() {
    if [ -z "$1" ]; then
        echo 0
    else
        echo "$1" | tr '|' '\n' | grep -c .
    fi
}

# The summary line of the TSan selection. Arguments: selected count or empty for all, reason.
note_tsan_selection() {
    total=$(count_tests tsan)
    if [ -z "$1" ]; then
        selection_note "TSan: all ${total:-?} tests ($2)"
    else
        selection_note "TSan: $1 of ${total:-?} tests (label $FULL_TSAN_LABEL plus, per test, those changed since they last passed a full-verify TSan step; last green full verify $(full_last_commit | cut -c1-10))"
    fi
}

# Why the stamp does not hold for the analysis although its commit does: other local presets or
# another clang-tidy. Prints the reason, nothing when it holds or for QC_VERIFY_DRY_SINCE.
analysis_stamp_reason() {
    [ -n "$(full_last_commit)" ] || return 0
    [ "${DRY_RUN:-0}" -eq 1 ] && [ -n "${QC_VERIFY_DRY_SINCE:-}" ] && return 0
    if [ "$(full_last_presets)" != "$(presets_hash)" ]; then
        echo "all: local presets CMakeUserPresets.json changed since the last green full verify"
    elif [ "$(full_last_tidy)" != "$(tidy_version)" ]; then
        echo "all: clang-tidy version differs from the last green full verify ($(full_last_tidy | grep . || echo none))"
    fi
}

# The analysis selection: prints `all…` or `changed: …` and writes the files to the list file.
# Argument: the list file.
analysis_selection() {
    if [ "$FULL_ANALYSIS" -eq 1 ]; then
        echo "all: --full-analysis, --release or QC_FULL_ANALYSIS=1"
        return 0
    fi
    reason=$(analysis_stamp_reason)
    if [ -n "$reason" ]; then
        echo "$reason"
        return 0
    fi
    python3 scripts/verify-full-select.py analysis --since "$(full_last_commit)" --out "$1" ||
        echo "all: selection failed"
}

# The static analysis of a full run: every file, or the selection against the last green commit.
full_static_analysis() {
    list="$PROJECT_DIR/build/debug/.verify-analysis-files"
    : >"$list"
    line=$(analysis_selection "$list" | tail -n 1)
    echo "static analysis selection: $line"
    case "$line" in
    all*)
        cpp_count=$(git ls-files 'src/*.cpp' 'tests/*.cpp' | grep -c .)
        selection_note "static analysis: every file, $cpp_count .cpp files for clang-tidy and clazy ($line)"
        scripts/static-analysis.sh "$PROJECT_DIR/build/debug"
        ;;
    *)
        selection_note "static analysis: ${line#changed: }"
        # shellcheck disable=SC2046
        scripts/static-analysis.sh --cheap-all "$PROJECT_DIR/build/debug" $(cat "$list")
        ;;
    esac
}

# The sanitizers of this run without a failed step (configure, build, tests, smoke).
passed_sanitizers() {
    for sanitizer in $SANITIZERS; do
        case " $failures " in
        *"-$sanitizer "*) ;;
        *) echo "$sanitizer" ;;
        esac
    done
}

# The sanitizers that passed on this commit in this run and, by the shared stamp, in an earlier one.
# Argument: the sanitizers the shared stamp named at the start for this commit.
union_sanitizers() {
    { echo "$1" | tr ' ' '\n'; passed_sanitizers; } | grep . | sort -u | tr '\n' ' ' | sed 's/ $//'
}

# At the start of a run: the sanitizers that already passed on this clean commit (SHARED_PASSED).
read_shared_passed() {
    SHARED_PASSED=""
    if [ -n "$START_COMMIT" ] && [ "$(full_stamp_key "$FULL_STAMP")" = "$START_COMMIT" ]; then
        SHARED_PASSED=$(full_stamp_sanitizers "$FULL_STAMP")
    fi
}

# Writes .verify-full-last when the commit has passed both sanitizers and the shared steps.
# Argument: the union of the passed sanitizers.
record_full_green() {
    case " $1 " in
    *" asan "*) has_asan=1 ;;
    *) has_asan=0 ;;
    esac
    case " $1 " in
    *" tsan "*) has_tsan=1 ;;
    *) has_tsan=0 ;;
    esac
    if [ "$has_asan" -eq 1 ] && [ "$has_tsan" -eq 1 ]; then
        write_full_last "$START_COMMIT"
        selection_note "full verify green on $(echo "$START_COMMIT" | cut -c1-10): stamp $FULL_LAST written (the next full verify selects against it)"
    else
        selection_note "full verify not yet green on both sanitizers for this commit (passed: ${1:-none}); $FULL_LAST unchanged"
    fi
}

# The end of a full run: a run that passed every step without a sanitizer on a clean, unchanged
# commit stamps it for the other part of a two-part run (E-890), naming the debug timing tests it
# deferred (E-913) and the sanitizers that passed on it so far; once both did, the commit is the
# last fully green full verify (E-929). Prints the selections of the run.
finish_full_run() {
    shared_failed=0
    for failed_step in $failures; do
        case "$failed_step" in
        *-asan | *-tsan) ;;
        *) shared_failed=1 ;;
        esac
    done
    if [ "$MODE" = full ] && [ "$shared_failed" -eq 0 ] && [ -z "$TEST_REGEX" ] &&
        [ -n "$START_COMMIT" ] && [ "$(clean_commit)" = "$START_COMMIT" ]; then
        passed=$(union_sanitizers "$SHARED_PASSED")
        write_full_stamp "$FULL_STAMP" "$START_COMMIT" "$debug_deferred_names" "$passed"
        record_full_green "$passed"
    elif [ "$MODE" = full ]; then
        selection_note "no stamp written (a shared step failed, --tests given, or the tree is or became dirty)"
    fi
    if [ -n "$selection_notes" ]; then
        echo "== selections of the full run (E-929)$selection_notes"
    fi
}

# --dry-run: the selections of a full run against the current stamps and build trees; runs nothing.
full_dry_run() {
    echo "== dry run of the full verify selection (decision E-929); builds and runs nothing"
    source="$FULL_LAST"
    [ -n "${QC_VERIFY_DRY_SINCE:-}" ] && source="QC_VERIFY_DRY_SINCE, instead of $FULL_LAST"
    echo "last green full verify: $(full_last_commit | grep . || echo none) ($source)"
    echo "shared stamp: $(full_stamp_key "$FULL_STAMP" | grep . || echo none); passed sanitizers there: $(full_stamp_sanitizers "$FULL_STAMP" | grep . || echo none)"
    list=$(mktemp "${TMPDIR:-/tmp}/qtcraft-verify-dry-run.XXXXXX") || return 2
    echo "static analysis: $(analysis_selection "$list" | tail -n 1)"
    sed "s|^$PROJECT_DIR/|   |" "$list" | head -n 20
    files=$(grep -c . "$list")
    if [ "$files" -gt 20 ]; then
        echo "   ... and $((files - 20)) more"
    fi
    rm -f "$list"
    if [ ! -f build/tsan/CTestTestfile.cmake ]; then
        echo "TSan: build/tsan not configured; the TSan step would run every test"
    elif full_tsan_select --no-pending; then
        echo "TSan: $(regex_count "$full_selection") of $(count_tests tsan) tests (by the binaries now in build/tsan; the next build may select more)"
    else
        echo "TSan: all $(count_tests tsan) tests ($full_reason)"
    fi
}
