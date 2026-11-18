#!/usr/bin/env bash
# Install a built cxadc-linux revision as root on the capture rigs, from wm, with one YubiKey
# touch per host.
#
# Why this exists: CI used to run deploy_dkms_install.sh as root through a NOPASSWD sudoers
# rule. That script lives in rdodge's own checkout, so anything running as rdodge -- any CI job
# on those runners, any local process -- could edit it and become root with no password and no
# touch (security audit, 2026-11-18). The rule is gone; CI (deploy.yml) now only pulls and
# builds, and this script does the root part with a person at the key:
#
#   remote hosts (cs0, cs1): `ssh <host>-root` -- root login with an ed25519-sk key, the touch
#                            happens on wm's YubiKey (workflow-master/sudo-handshake/remote-root)
#   wm itself:               plain `sudo` (the touch is the authentication)
#
# Per host: build as rdodge (pull --ff-only, make), install as root (one connection), verify as
# rdodge, write the deployed-revision stamp that deploy.yml compares against.
#
#   src/scripts/deploy-root.sh                       dry run: what would happen, per host
#   src/scripts/deploy-root.sh --apply               cs0 cs1 wm
#   src/scripts/deploy-root.sh --apply --hosts cs1   just cs1 (comma list)
#   options: --force (redeploy an already-deployed revision), --reload (reload the module;
#            refuses while a capture holds it), --kernel <release>, --current-kernel-only,
#            --no-prune
set -euo pipefail

REPO_DIR=/home/rdodge/Repos/cxadc-linux
BRANCH=master
HOSTS=(cs0 cs1 wm)
STAMP='$HOME/.local/state/cxadc-deploy/deployed-sha'   # expanded on each host

apply=0 force=0 reload=0 kernel="" all_kernels=true prune=true
while [ $# -gt 0 ]; do
  case $1 in
    --apply) apply=1 ;;
    --hosts) IFS=, read -r -a HOSTS <<<"${2:?--hosts needs a list}"; shift ;;
    --force) force=1 ;;
    --reload) reload=1 ;;
    --kernel) kernel=${2:?--kernel needs a release}; shift ;;
    --current-kernel-only) all_kernels=false ;;
    --no-prune) prune=false ;;
    -h|--help) sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "deploy-root.sh: unknown option $1" >&2; exit 2 ;;
  esac
  shift
done
[ "$(hostname -s)" = workflow-master ] || { echo "run this on wm (it needs wm's YubiKey)" >&2; exit 1; }
[ "$(id -u)" -ne 0 ] || { echo "run as rdodge, not root: it calls ssh <host>-root / sudo itself" >&2; exit 1; }
[[ $kernel =~ ^[A-Za-z0-9._+-]*$ ]] || { echo "bad --kernel value" >&2; exit 2; }

# Runs as rdodge on the host: refuse a dirty or off-branch checkout, fast-forward, build.
build_sh=$(cat <<EOF
set -euo pipefail
cd $REPO_DIR
[ "\$(git rev-parse --abbrev-ref HEAD)" = $BRANCH ] || { echo "not on $BRANCH" >&2; exit 1; }
[ -z "\$(git status --porcelain)" ] || { echo "dirty working tree; refusing" >&2; git status --porcelain >&2; exit 1; }
git fetch -q --prune origin
git pull -q --ff-only origin $BRANCH
rev=\$(git rev-parse HEAD)
stamp=$STAMP
if [ "$force" != 1 ] && [ "\$(cat "\$stamp" 2>/dev/null)" = "\$rev" ]; then echo "UP-TO-DATE \$rev"; exit 0; fi
make -s clean >/dev/null
make -s -j4 >/dev/null
echo "BUILT \$rev"
EOF
)

# Runs as ROOT on the host, in the one root connection.
root_sh=$(cat <<EOF
set -euo pipefail
env DKMS_TARGET_KERNEL='$kernel' DKMS_INSTALL_ALL_KERNELS='$all_kernels' PRUNE_OLD_DKMS_VERSIONS='$prune' \\
  /usr/bin/bash $REPO_DIR/src/scripts/deploy_dkms_install.sh
if [ $reload = 1 ]; then
  if lsmod | grep -q '^cxadc'; then
    refs=\$(lsmod | awk '/^cxadc/{print \$3}')
    [ "\$refs" = 0 ] || { echo "cxadc in use (refcount \$refs): not reloading; a capture is running" >&2; exit 3; }
    modprobe -r cxadc
  fi
  modprobe cxadc
  lsmod | grep -q '^cxadc' || { echo "cxadc did not load" >&2; exit 1; }
  echo "cxadc reloaded"
fi
EOF
)

# Runs as rdodge on the host: the same checks deploy.yml used to run, then the stamp.
verify_sh=$(cat <<EOF
set -euo pipefail
cd $REPO_DIR
rev=\$(git rev-parse HEAD)
for t in cx-capture leveladj cxadc-status; do command -v "\$t" >/dev/null || { echo "\$t missing after install" >&2; exit 1; }; done
name=\$(awk -F= '/^PACKAGE_NAME=/{gsub(/"/,"",\$2); print \$2}' dkms.conf)
ver=\$(awk -F= '/^PACKAGE_VERSION=/{gsub(/"/,"",\$2); print \$2}' dkms.conf)
st=\$(dkms status -m "\$name" -v "\$ver" || true)
echo "\$st"
grep -qE "^\$name/\$ver, .*: installed" <<<"\$st" || { echo "target DKMS version not installed" >&2; exit 1; }
! grep -qE "^\$name/\$ver, .*: built\$" <<<"\$st" || { echo "built but not installed for some kernel" >&2; exit 1; }
modinfo -n cxadc | grep -qE '/(updates/dkms|extra)/' || { echo "cxadc module is not DKMS-managed" >&2; exit 1; }
modinfo -F vermagic cxadc | grep -q "\$(uname -r)" || { echo "cxadc vermagic does not match \$(uname -r)" >&2; exit 1; }
stamp=$STAMP
mkdir -p "\$(dirname "\$stamp")"
printf '%s\n' "\$rev" > "\$stamp"
echo "deployed \$rev"
EOF
)

on() {  # on <host> <script>: run as rdodge on the host (local on wm)
  if [ "$1" = wm ]; then bash -c "$2"; else ssh -o BatchMode=yes "$1" "bash -c $(printf %q "$2")"; fi
}

rc=0
for h in "${HOSTS[@]}"; do
  case $h in cs0|cs1|wm) ;; *) echo "unknown host $h (expected cs0, cs1, wm)" >&2; rc=1; continue ;; esac
  echo "== $h"
  if [ "$apply" = 0 ]; then
    echo "   would: build as rdodge (pull --ff-only $BRANCH, make), install as root"
    echo "          ($([ $h = wm ] && echo "sudo on wm" || echo "ssh $h-root, one touch")), verify, stamp$([ $reload = 1 ] && echo ", reload cxadc")"
    continue
  fi
  out=$(on "$h" "$build_sh") || { echo "$out"; echo "   build FAILED on $h" >&2; rc=1; continue; }
  echo "   $out"
  case $out in UP-TO-DATE*) continue ;; esac
  echo "   installing as root on $h -- touch the YubiKey when it blinks"
  if [ "$h" = wm ]; then
    sudo bash -c "$root_sh" || { echo "   install FAILED on wm" >&2; rc=1; continue; }
  else
    ssh "$h-root" "bash -c $(printf %q "$root_sh")" || { echo "   install FAILED on $h" >&2; rc=1; continue; }
  fi
  on "$h" "$verify_sh" | sed 's/^/   /' || { echo "   verify FAILED on $h" >&2; rc=1; }
done
exit $rc
