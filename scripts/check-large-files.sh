#!/bin/sh
# Refuses tracked files over 2 MB (decision E-933): screenshots belong in docs/screenshots as JPEG,
# generated data (such as web/handbook/data) is not committed. A file that must be larger is listed
# in scripts/large-files-allowed.txt (one path per line, # starts a comment).
#
# Modes:
#   (none)        the index: tracked and staged files (scripts/verify.sh, before the build);
#   --rev <rev>   the tree of that commit;
#   --push        what a push sends (tools/githooks/pre-push): reads the hook's lines
#                 "<local ref> <local sha> <remote ref> <remote sha>" on stdin and checks every blob
#                 of `git rev-list --objects <local> --not <remote>`. A new branch is checked against
#                 every remote-tracking branch (`<local> --not --remotes`); with no remote-tracking
#                 branch at all (the first push to a new remote) only the tree of <local> is checked,
#                 since the history before E-933 holds larger files and is not rewritten. Deletions
#                 send nothing.
# Submodules do not count. Each mode takes well under a second. Needs git 2.38 or newer
# (`git ls-files --format`).
# Usage: scripts/check-large-files.sh [--rev <rev> | --push]   (exit code 0: none too large,
#        1: findings, 2: usage)

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
LIMIT_BYTES=2097152
ALLOWED="$PROJECT_DIR/scripts/large-files-allowed.txt"
ZERO_SHA_PATTERN='^0*$'

usage() {
    echo "usage: scripts/check-large-files.sh [--rev <rev> | --push]" >&2
    exit 2
}

# Prints "<size> <path>" for every blob of the commit `$1`.
tree_sizes() {
    git ls-tree -r -l "$1" | awk -F '\t' '{ split($1, f, " "); if (f[2] == "blob") print f[4] " " $2 }'
}

# Prints "<size> <path>" for every blob in the index.
index_sizes() {
    git ls-files --format='%(objectmode) %(objectname) %(path)' | grep -v '^160000 ' | cut -d ' ' -f 2- |
        git cat-file --batch-check='%(objectsize) %(rest)'
}

# Prints "<size> <path>" for every blob that the pushed refs on stdin send.
push_sizes() {
    while read -r _local_ref local_sha _remote_ref remote_sha; do
        if printf '%s\n' "$local_sha" | grep -q "$ZERO_SHA_PATTERN"; then
            continue
        fi
        if ! printf '%s\n' "$remote_sha" | grep -q "$ZERO_SHA_PATTERN" && git cat-file -e "$remote_sha^{commit}" 2>/dev/null; then
            git rev-list --objects "$local_sha" --not "$remote_sha"
        elif [ -n "$(git for-each-ref --count=1 refs/remotes)" ]; then
            git rev-list --objects "$local_sha" --not --remotes
        else
            git ls-tree -r "$local_sha" | awk -F '\t' '{ split($1, f, " "); if (f[2] == "blob") print f[3] " " $2 }'
        fi
    done | git cat-file --batch-check='%(objecttype) %(objectsize) %(rest)' | awk '$1 == "blob" { sub(/^blob /, ""); print }'
}

case "${1-}" in
"") mode=index ;;
--rev)
    [ $# -eq 2 ] && [ -n "$2" ] || usage
    mode=rev
    ;;
--push)
    [ $# -eq 1 ] || usage
    mode=push
    ;;
*) usage ;;
esac

cd "$PROJECT_DIR" || exit 1

sizes() {
    case $mode in
    index) index_sizes ;;
    rev) tree_sizes "$2" ;;
    push) push_sizes ;;
    esac
}

allowed=$(grep -v '^[[:space:]]*#' "$ALLOWED" 2>/dev/null | sed 's/[[:space:]]*$//' | grep -v '^$')

found=$(sizes "$@" | awk -v limit="$LIMIT_BYTES" '$1 > limit' | sort -u -k 2 | while read -r size path; do
    if ! printf '%s\n' "$allowed" | grep -qxF -- "$path"; then
        echo "$path: $((size / 1024)) KB, over the limit of $((LIMIT_BYTES / 1024)) KB"
    fi
done)

if [ -n "$found" ]; then
    printf '%s\n' "$found"
    echo "Large files: make them smaller (screenshots as JPEG), keep generated data out of git, or list them in scripts/large-files-allowed.txt (decision E-933)."
    exit 1
fi
exit 0
