#!/bin/sh
# SPDX-FileCopyrightText: 2026 the QtCraft authors
# SPDX-License-Identifier: GPL-3.0-or-later

# The large file check (scripts/check-large-files.sh, decision E-933) in a throwaway git repository
# with a bare remote; the pushes run the check from a pre-push hook as tools/githooks/pre-push does:
#   - index and --rev: a file over 2 MB fails, an allowed one passes; --rev without a value and an
#     unknown option are usage errors;
#   - --push: the first push to a remote without remote-tracking branches checks only the tree
#     (a large file only in older history passes, one in the tree fails); a later push checks only
#     what it sends (a large file already on the remote passes, a new one fails, also when it is
#     removed again in a later commit of the same push); a new branch is checked against the
#     remote-tracking branches; deleting a branch sends nothing.
# Usage: tests/scripts/test_check_large_files.sh <path of scripts/check-large-files.sh>
set -u
CHECK=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
DIR=$(mktemp -d "${TMPDIR:-/tmp}/check-large-files.XXXXXX") || exit 2
trap 'rm -rf "$DIR"' EXIT
failed=0

expect() {
    expected=$1
    name=$2
    shift 2
    output=$("$@" 2>&1)
    actual=$?
    if [ "$actual" -ne "$expected" ]; then
        echo "FAIL $name: exit $actual, expected $expected"
        printf '%s\n' "$output"
        failed=1
    else
        echo "ok   $name"
    fi
}

# A file of 2.2 MB whose content names it: equal contents would be one blob, already on the remote.
big() {
    {
        echo "$1"
        head -c 2200000 /dev/zero | tr '\0' x
    } >"$1"
}

commit() {
    git add -A && git commit -q -m "$1"
}

REPO="$DIR/repo"
git init -q -b main "$REPO" || exit 2
git init -q --bare "$DIR/remote.git" || exit 2
cd "$REPO" || exit 2
git config user.email test@example.invalid
git config user.name Test
git config commit.gpgsign false
mkdir -p scripts hooks
cp "$CHECK" scripts/check-large-files.sh
printf '# allowed\ndocs/allowed.bin\n' >scripts/large-files-allowed.txt
printf '#!/bin/sh\nexec scripts/check-large-files.sh --push\n' >hooks/pre-push
chmod +x scripts/check-large-files.sh hooks/pre-push
git config core.hooksPath hooks
echo small >small.txt
commit "small"

# Index and --rev.
expect 0 "index without large files" scripts/check-large-files.sh
mkdir -p docs
big docs/allowed.bin
git add docs/allowed.bin
expect 0 "allowed large file in the index" scripts/check-large-files.sh
big old.bin
git add old.bin
expect 1 "large file in the index" scripts/check-large-files.sh
commit "large files"
expect 1 "large file in a commit" scripts/check-large-files.sh --rev HEAD
expect 0 "older commit" scripts/check-large-files.sh --rev HEAD~1
expect 2 "--rev without a value" scripts/check-large-files.sh --rev
expect 2 "unknown option" scripts/check-large-files.sh --bogus

# First push to a new remote: only the tree counts.
git rm -q old.bin
commit "remove old.bin"
git remote add origin "$DIR/remote.git"
expect 0 "first push, large file only in history" git push -q origin main
expect 0 "nothing new to push" git push -q origin main

# Later pushes: what they send.
big new.bin
commit "new large file"
expect 1 "push with a new large file" git push -q origin main
git rm -q new.bin
commit "remove new.bin"
expect 1 "push that adds and removes a large file" git push -q origin main
git reset -q --hard origin/main
echo more >>small.txt
commit "small change"
expect 0 "push of a small change" git push -q origin main

# New branches against the remote-tracking branches, and deletions.
git checkout -q -b feature
echo feature >feature.txt
commit "feature"
expect 0 "new branch with small files" git push -q origin feature
git checkout -q -b large
big large.bin
commit "large on a branch"
expect 1 "new branch with a large file" git push -q origin large
expect 0 "deleting a branch" git push -q origin --delete feature

exit $failed
