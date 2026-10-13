#!/bin/sh
# Argument checks of scripts/bench-ab.sh (--check: no lock, no runs): settings per label, values
# with ":" (pack ids), and refused variables and values (decision E-332).
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
    code=$1
    text=$2
    shift 2
    output=$(sh "$SCRIPT" --check "$DIR/result.txt" 1 1 "$@" 2>&1)
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
exit "$failed"
