#!/bin/sh
# The stamps and selections of the full verify (scripts/verify-full-select.sh, decision E-929),
# calling its real functions with stubbed python3, ctest, git and clang-tidy:
#   - finish_full_run: the commit becomes the last green full verify only once both sanitizers
#     passed on it (one run, or both parts of a two-part run on the same commit); part two on
#     another commit, a dirty tree, --tests, a failed shared step and a failed sanitizer step
#     (also its smoke run) keep it out; the stamp names the clang-tidy version;
#   - the TSan selection: every test without the stamp, without TSan stamps, with --all-tsan or
#     --tests, otherwise verify-tests.py --full-label;
#   - the static analysis: the selection with --cheap-all, every file without the stamp, with
#     QC_FULL_ANALYSIS=1, after a change of the local presets or the clang-tidy version;
#   - seed_full_green and the dry run.
# Runs no build, no ctest and no analysis.
# Usage: tests/scripts/test_verify_full_stamps.sh <path of scripts/verify-full-select.sh>
set -u
SCRIPTS=$(cd "$(dirname "$1")" && pwd)
DIR=$(mktemp -d "${TMPDIR:-/tmp}/verify-full-stamps.XXXXXX") || exit 2
trap 'rm -rf "$DIR"' EXIT
failed=0
printf '#!/bin/sh\necho "LLVM (http://llvm.org/):"\necho "  LLVM version ${TIDY_VERSION:-19.1.0}"\n' >"$DIR/clang-tidy"
chmod +x "$DIR/clang-tidy"

# Runs shell code with verify-summary.sh and verify-full-select.sh sourced in a fake project
# directory that persists between calls (rm -rf "$DIR/project" resets it); prints its output.
# Arguments: environment assignments before `--`, then the code.
run() {
    (
        while [ "$1" != -- ]; do
            export "${1?}"
            shift
        done
        shift
        PROJECT_DIR="$DIR/project"
        mkdir -p "$PROJECT_DIR/build/debug" "$PROJECT_DIR/build/tsan"
        cd "$PROJECT_DIR" || exit 2
        CLANG_TIDY="$DIR/clang-tidy"
        MODE=${MODE:-full}
        TEST_REGEX=${REGEX:-}
        START_COMMIT=${START-abc123 presets}
        FULL_STAMP="$PROJECT_DIR/build/debug/.verify-full-shared"
        failures=${FAILURES:-}
        debug_deferred_names=""
        . "$SCRIPTS/verify-summary.sh"
        . "$SCRIPTS/verify-full-select.sh"
        clean_commit() {
            echo "${NOW-$START_COMMIT}"
        }
        python3() {
            echo "python3 $*" >&2
            case "$*" in
            *verify-full-select.py*)
                out=""
                previous=""
                for word in "$@"; do
                    [ "$previous" = --out ] && out=$word
                    previous=$word
                done
                [ -n "${PYTHON_FAILS:-}" ] && return 1
                echo "${ANALYSIS_LINE:-changed: 2 files}"
                printf '%s\n' "$PROJECT_DIR/src/a.cpp" "$PROJECT_DIR/src/a.h" >"$out"
                ;;
            *) echo '^(test_c|test_d)$' ;;
            esac
        }
        ctest() {
            echo "Total Tests: 400"
        }
        git() {
            case "$1" in
            hash-object) echo "${PRESETS:-presets}" ;;
            rev-parse) [ "$4" = "bad^{commit}" ] && return 1 || echo "full-$4" | sed 's/\^{commit}//' ;;
            *) printf '%s\n' a.cpp b.cpp c.cpp ;;
            esac
        }
        mkdir -p scripts
        printf '#!/bin/sh\necho static-analysis "$@"\n' >scripts/static-analysis.sh
        chmod +x scripts/static-analysis.sh
        eval "$1"
    ) 2>&1
}

# expect <label> <output> <text the output must contain>...
expect() {
    label=$1
    output=$2
    shift 2
    for text in "$@"; do
        case "$output" in
        *"$text"*) ;;
        *)
            echo "FAIL $label: missing '$text' in:" >&2
            echo "$output" >&2
            failed=1
            return
            ;;
        esac
    done
    echo "ok   $label"
}

# reject <label> <output> <text the output must not contain>
reject() {
    case "$2" in
    *"$3"*)
        echo "FAIL $1: unexpected '$3' in:" >&2
        echo "$2" >&2
        failed=1
        ;;
    *) echo "ok   $1" ;;
    esac
}

# The end of a full run as verify.sh does it: read the shared stamp at the start, finish at the end.
finish='read_shared_passed; finish_full_run; echo "last=[$(cat build/.verify-full-last 2>/dev/null | tr "\n" "|")]"; cat "$FULL_STAMP" 2>/dev/null'
reset() {
    rm -rf "$DIR/project"
}

reset
out=$(run SANITIZERS="asan tsan" -- "$finish")
expect "one-part run green on both" "$out" "last=[abc123 presets|clang-tidy: LLVM version 19.1.0|]" \
    "passed-sanitizers: asan tsan" "stamp $DIR/project/build/.verify-full-last written" "== selections of the full run"

reset
out=$(run SANITIZERS=asan -- "$finish")
expect "part one (ASan) alone writes no green stamp" "$out" "last=[]" "passed-sanitizers: asan" \
    "not yet green on both sanitizers for this commit (passed: asan)"
out=$(run SANITIZERS=tsan -- "$finish")
expect "part two (TSan) on the same commit completes it" "$out" "last=[abc123 presets|" "passed-sanitizers: asan tsan"

reset
run SANITIZERS=asan -- "$finish" >/dev/null
out=$(run SANITIZERS=tsan START="def456 presets" -- "$finish")
expect "part two on another commit does not complete it" "$out" "last=[]" "(passed: tsan)"
out=$(run SANITIZERS=tsan START="abc123 other-presets" -- "$finish")
expect "part two with other local presets neither" "$out" "last=[]" "(passed: tsan)"

reset
out=$(run SANITIZERS="asan tsan" START="" -- "$finish")
expect "dirty tree at the start writes nothing" "$out" "last=[]" "no stamp written"
reset
out=$(run SANITIZERS="asan tsan" NOW="" -- "$finish")
expect "tree dirty at the end writes nothing" "$out" "last=[]" "no stamp written"
reset
out=$(run SANITIZERS="asan tsan" REGEX=x -- "$finish")
expect "--full --tests writes nothing" "$out" "last=[]" "no stamp written"
reset
out=$(run SANITIZERS="asan tsan" FAILURES=" static-analysis" -- "$finish")
expect "a failed shared step writes nothing" "$out" "last=[]" "no stamp written"
reset
out=$(run SANITIZERS="asan tsan" FAILURES=" smoke-run-asan" -- "$finish")
expect "a failed ASan smoke run keeps ASan out" "$out" "last=[]" "passed-sanitizers: tsan" "(passed: tsan)"
reset
out=$(run SANITIZERS="asan tsan" FAILURES=" test-tsan" -- "$finish")
expect "a failed TSan test step keeps TSan out" "$out" "last=[]" "(passed: asan"
reset
out=$(run MODE=quick SANITIZERS=tsan -- "$finish")
reject "a quick run writes and says nothing" "$out" "selections of the full run"

# The TSan selection.
reset
tsan='if full_tsan_select; then note_tsan_selection "$(regex_count "$full_selection")"; echo "regex=$full_selection"; else note_tsan_selection "" "$full_reason"; fi; echo "notes:$selection_notes"'
out=$(run -- "$tsan")
expect "no green stamp: every TSan test" "$out" "TSan: all 400 tests (no green full verify recorded"
reject "no green stamp asks no selection" "$out" "verify-tests.py"
out=$(run -- "write_full_last 'abc123 presets'; $tsan")
expect "no TSan stamps of a full verify yet: every test" "$out" "TSan: all 400 tests (no TSan stamps of a full verify in build/tsan yet)"
out=$(run -- "touch build/tsan/.verify-full-tsan-stamps.json; $tsan")
expect "stamps: label plus changed tests" "$out" \
    "python3 scripts/verify-tests.py select --build-dir build/tsan --changed --full-label concurrency --stamps .verify-full-tsan-stamps.json" \
    "regex=^(test_c|test_d)\$" "TSan: 2 of 400 tests (label concurrency plus, per test, those changed since they last passed a full-verify TSan step; last green full verify abc123)"
out=$(run QC_FULL_TSAN_ALL=1 -- "$tsan")
expect "QC_FULL_TSAN_ALL=1 runs every test" "$out" "TSan: all 400 tests (--all-tsan, --release or QC_FULL_TSAN_ALL=1)"
out=$(run REGEX=x -- "$tsan")
expect "--tests runs every matching test" "$out" "TSan: all 400 tests (--tests given)"
out=$(run QC_FULL_TSAN_ALL=yes -- "$tsan")
expect "QC_FULL_TSAN_ALL must be 0 or 1" "$out" "must be 0 or 1, not 'yes'"

# The static analysis (stamp from above: presets `presets`, clang-tidy 19.1.0).
analysis='full_static_analysis; echo "notes:$selection_notes"'
out=$(run -- "$analysis")
expect "selected files with --cheap-all" "$out" "static-analysis --cheap-all $DIR/project/build/debug $DIR/project/src/a.cpp $DIR/project/src/a.h" \
    "static analysis: 2 files"
out=$(run PRESETS=changed -- "$analysis")
expect "other local presets: every file" "$out" "static-analysis $DIR/project/build/debug
" "(all: local presets CMakeUserPresets.json changed since the last green full verify)"
reject "other local presets ask no selection" "$out" "verify-full-select.py"
out=$(run TIDY_VERSION=20.0.0 -- "$analysis")
expect "another clang-tidy: every file" "$out" "(all: clang-tidy version differs from the last green full verify (LLVM version 19.1.0))"
out=$(run ANALYSIS_LINE="all (selection ≥ 40 %): 600 of 1292 .cpp files since abc" -- "$analysis")
expect "selection of 40 % or more: every file" "$out" "static-analysis $DIR/project/build/debug
" "(all (selection ≥ 40 %): 600 of 1292"
out=$(run PYTHON_FAILS=1 -- "$analysis")
expect "failed selection: every file" "$out" "(all: selection failed)"
out=$(run QC_FULL_ANALYSIS=1 -- "$analysis")
expect "QC_FULL_ANALYSIS=1 analyses every file" "$out" "(all: --full-analysis, --release or QC_FULL_ANALYSIS=1)"
reject "QC_FULL_ANALYSIS=1 asks no selection" "$out" "verify-full-select.py"
reset
out=$(run -- "$analysis")
expect "no stamp: python answers for the empty commit" "$out" "python3 scripts/verify-full-select.py analysis --since  --out"

# Seeding and the dry run.
reset
out=$(run -- 'seed_full_green 3e893a4; echo "status=$?"')
expect "seed writes commit, presets and clang-tidy version" "$out" "full-3e893a4 presets
clang-tidy: LLVM version 19.1.0" "status=0"
out=$(run -- 'seed_full_green bad; echo "status=$?"')
expect "seed refuses an unknown commit" "$out" "--seed-green: unknown commit 'bad'" "status=2"
out=$(run DRY_RUN=1 -- 'full_dry_run')
expect "dry run names both selections" "$out" "last green full verify: full-3e893a4" "static analysis: changed: 2 files" \
    "   src/a.cpp" "TSan: build/tsan not configured"
out=$(run DRY_RUN=1 PYTHON_FAILS=1 -- 'full_dry_run')
expect "dry run with a failed selection" "$out" "static analysis: all: selection failed"
out=$(run DRY_RUN=1 QC_VERIFY_DRY_SINCE=other PRESETS=changed -- 'full_dry_run')
expect "dry run against another commit compares no presets" "$out" "last green full verify: other (QC_VERIFY_DRY_SINCE" \
    "--since other" "static analysis: changed: 2 files"

exit $failed
