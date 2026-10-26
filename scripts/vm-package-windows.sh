#!/bin/sh
# Windows x64 package from the Mac (stage 74.4): copies scripts/windows/package-windows.ps1 and the
# README template into the Parallels VM, runs it there over SSH for a git ref of this repository
# (the VM fetches the ref through the Parallels share \\Mac\Home, so only committed states are
# packaged) and copies the resulting QtCraft-<version>-<sha>-Windows-x64.zip and its -pdb.zip into
# deploy/ of the main checkout (gitignored). The VM must be running; this script never starts or
# stops it. Build and checks are described in package-windows.ps1; the build is registered with the
# slot broker (kind build) and runs in the VM with the granted jobs, at most 4 (the VM's cores).
#
# Usage: scripts/vm-package-windows.sh [ref] [-- extra arguments for package-windows.ps1]
#   ref                  branch or tag of this repository (default dev); a commit hash does not
#                        work (git upload-pack serves only refs): for an older commit create a tag
#                        or branch first (git tag playtest-<sha> <sha>)
#   QC_VM_HOST           SSH target (default: jonashilk@<IP of the running VM from prlctl list -a>;
#                        10.211.55.6 only where prlctl is not installed)
#   QC_VM_KEY            SSH key (default ~/.ssh/id_ed25519_parallels_vm)
#   QC_VM_ROOT           build folder in the VM (default C:\Qt-Projekte\Claude\QtCraft)
#   QC_VM_REPO           this repository as seen from the VM (default derived from $HOME → //Mac/Home)
#   QC_PLAYTEST_SERVER   LAN address of the playtest server for README-Windows.txt
#                        (default 192.168.0.201:25600)
# Example: scripts/vm-package-windows.sh dev -- -GuiCheck
#
# The package carries the bundled OpenSSL (DLLs and licence; decision E-872): the configure step in
# the VM builds it once per version into <QC_VM_ROOT>\openssl (scripts/windows/build-openssl.ps1
# of the packaged ref; first run about 10 min).

set -u

PROJECT_DIR=$(cd "$(dirname "$0")/.." && pwd)
REF=${1:-dev}
[ $# -gt 0 ] && shift
[ "${1:-}" = "--" ] && shift
KEY=${QC_VM_KEY:-"$HOME/.ssh/id_ed25519_parallels_vm"}
ROOT=${QC_VM_ROOT:-'C:\Qt-Projekte\Claude\QtCraft'}
SERVER=${QC_PLAYTEST_SERVER:-192.168.0.201:25600}
# The main checkout (worktrees share its refs); the VM sees $HOME as //Mac/Home.
MAIN_DIR=$(cd "$(git -C "$PROJECT_DIR" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)
if [ -z "${QC_VM_REPO:-}" ]; then
    case "$MAIN_DIR" in
    "$HOME"/*) ;;
    *)
        echo "the repository $MAIN_DIR is outside $HOME, which the VM sees as //Mac/Home; set QC_VM_REPO" >&2
        exit 2
        ;;
    esac
fi
REPO=${QC_VM_REPO:-"//Mac/Home${MAIN_DIR#"$HOME"}"}

# Only branches and tags: upload-pack in the VM's fetch serves refs, not commit hashes.
if git -C "$PROJECT_DIR" show-ref --verify --quiet "refs/heads/$REF"; then
    :
elif git -C "$PROJECT_DIR" show-ref --verify --quiet "refs/tags/$REF"; then
    :
else
    echo "$REF is not a branch or tag of this repository (for a commit: git tag playtest-<sha> <sha>)" >&2
    exit 2
fi
case "$REF$SERVER" in
*[!A-Za-z0-9._/:-]*)
    echo "ref and server address may contain only letters, digits and . _ / : -" >&2
    exit 2
    ;;
esac

if [ -z "${QC_VM_HOST:-}" ]; then
    if [ -x /usr/local/bin/prlctl ]; then
        line=$(/usr/local/bin/prlctl list -a 2>/dev/null | awk '/Windows 11/')
        case "$line" in
        *" running "*) ;;
        *)
            echo "the VM \"Jonas Windows 11\" is not running (prlctl list -a); start it first" >&2
            exit 2
            ;;
        esac
        ip=$(echo "$line" | awk '{ print $3 }')
        # prlctl shows "-" while the guest tools have not reported an address; the bridge address
        # the VM had so far is the best guess then.
        [ "$ip" = "-" ] && ip=10.211.55.6
    else
        ip=10.211.55.6
    fi
    QC_VM_HOST="jonashilk@$ip"
fi

echo "== $REF = $(git -C "$PROJECT_DIR" rev-parse --short=7 "$REF"), VM $QC_VM_HOST, $ROOT"
ssh -o ConnectTimeout=15 -o BatchMode=yes -i "$KEY" "$QC_VM_HOST" "if not exist \"$ROOT\\scripts\" mkdir \"$ROOT\\scripts\"" || {
    echo "cannot reach the VM over SSH ($QC_VM_HOST)" >&2
    exit 2
}
scp -q -i "$KEY" "$PROJECT_DIR/scripts/windows/package-windows.ps1" "$PROJECT_DIR/scripts/windows/README-Windows.txt" \
    "$QC_VM_HOST:$(echo "$ROOT" | tr '\\' '/')/scripts/" || exit 2

LOG=$(mktemp "${TMPDIR:-/tmp}/qc-vm-package.XXXXXX")
# The remote command takes the job count the broker granted (QC_BUILD_JOBS), at most 4.
QC_VM_PS_ARGS="$*" QC_VM_ROOT_Q="$ROOT" QC_VM_REPO_Q="$REPO" QC_VM_REF="$REF" QC_VM_SERVER="$SERVER" QC_VM_KEY_Q="$KEY" QC_VM_HOST_Q="$QC_VM_HOST" \
    "$PROJECT_DIR/scripts/qc-slot.py" run --kind build --jobs 4 --min-jobs 4 --who "74.4 vm package" -- sh -c '
        jobs=${QC_BUILD_JOBS:-4}
        [ "$jobs" -gt 4 ] && jobs=4
        exec ssh -o ConnectTimeout=15 -o BatchMode=yes -i "$QC_VM_KEY_Q" "$QC_VM_HOST_Q" \
            "powershell -NoProfile -ExecutionPolicy Bypass -File \"$QC_VM_ROOT_Q\\scripts\\package-windows.ps1\" -Ref \"$QC_VM_REF\" -Root \"$QC_VM_ROOT_Q\" -Repo \"$QC_VM_REPO_Q\" -Server \"$QC_VM_SERVER\" -Jobs $jobs $QC_VM_PS_ARGS"' |
    tee "$LOG"
zip=$(sed -n 's/^PACKAGE OK \(.*\)$/\1/p' "$LOG" | tr -d '\r' | head -1)
if [ -z "$zip" ]; then
    echo "packaging in the VM failed (output: $LOG)"
    exit 1
fi

mkdir -p "$MAIN_DIR/deploy" || exit 2
remote=$(echo "$zip" | tr '\\' '/')
for file in "$remote" "${remote%.zip}-pdb.zip"; do
    scp -q -i "$KEY" "$QC_VM_HOST:$file" "$MAIN_DIR/deploy/" || exit 1
    echo "copied $MAIN_DIR/deploy/$(basename "$file")"
done
rm -f "$LOG"
