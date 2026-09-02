#!/usr/bin/env bash
set -euo pipefail

repo_dir="/home/rdodge/Repos/cxadc-linux"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "ERROR: deploy_dkms_install.sh must run as root" >&2
  exit 1
fi

if [[ ! -d "${repo_dir}/.git" ]]; then
  echo "ERROR: repo not found at ${repo_dir}" >&2
  exit 1
fi

cd "${repo_dir}"

install_bin="/usr/local/bin"
build_bin="${repo_dir}/build/bin"

dkms_name="$(awk -F= '/^PACKAGE_NAME=/{gsub(/"/,"",$2); print $2}' dkms.conf)"
dkms_version="$(awk -F= '/^PACKAGE_VERSION=/{gsub(/"/,"",$2); print $2}' dkms.conf)"
kernel_module_version="$(sed -nE 's/.*version ([0-9]+(\.[0-9]+)*)/\1/p' src/kernel/cxadc.c | head -n1)"
readme_version="$(sed -nE 's/.*Current DKMS package version:[[:space:]]*`([^`]+)`.*/\1/p' README.md | head -n1)"
install_doc_version="$(sed -nE 's/.*DKMS package version[[:space:]]*`([^`]+)`.*/\1/p' INSTALL.md | head -n1)"

dkms_bin="$(command -v dkms || true)"
depmod_bin="$(command -v depmod || true)"
rsync_bin="$(command -v rsync || true)"

to_bool() {
  case "${1,,}" in
    1|true|yes|on) echo "true" ;;
    *) echo "false" ;;
  esac
}

dkms_target_kernel="${DKMS_TARGET_KERNEL:-$(uname -r)}"
# Both default ON. A host that keeps three kernels (Fedora's installonly_limit)
# but only builds cxadc for the running one has two bootable fallback kernels
# with no capture driver -- and, until this changed, with a *stale* driver,
# because nothing ever removed the previous DKMS version. Converge every
# installed kernel onto exactly one version and drop the rest.
dkms_install_all_kernels="$(to_bool "${DKMS_INSTALL_ALL_KERNELS:-true}")"
prune_old_dkms_versions="$(to_bool "${PRUNE_OLD_DKMS_VERSIONS:-true}")"
# Deliberately NOT threaded through deploy.yml's `sudo env ...` invocation:
# /etc/sudoers.d/cxadc-linux-deploy matches that command line positionally and
# lists exactly DKMS_TARGET_KERNEL, DKMS_INSTALL_ALL_KERNELS and
# PRUNE_OLD_DKMS_VERSIONS. A fourth variable would not match the alias, so
# every deploy on every rig would start prompting for a password. Set this from
# a real root shell when you want to preview a prune.
prune_dry_run="$(to_bool "${DKMS_PRUNE_DRY_RUN:-false}")"

if [[ -z "${dkms_name}" || -z "${dkms_version}" ]]; then
  echo "ERROR: failed to parse PACKAGE_NAME/PACKAGE_VERSION from dkms.conf" >&2
  exit 1
fi
if [[ -z "${kernel_module_version}" ]]; then
  echo "ERROR: failed to parse module version from src/kernel/cxadc.c" >&2
  exit 1
fi
if [[ -z "${readme_version}" ]]; then
  echo "ERROR: README.md missing 'Current DKMS package version:' marker" >&2
  exit 1
fi
if [[ -z "${install_doc_version}" ]]; then
  echo "ERROR: INSTALL.md missing 'DKMS package version' marker" >&2
  exit 1
fi
if [[ "${dkms_version}" != "${kernel_module_version}" \
   || "${dkms_version}" != "${readme_version}" \
   || "${dkms_version}" != "${install_doc_version}" ]]; then
  echo "ERROR: version mismatch detected:" >&2
  echo "  dkms.conf: ${dkms_version}" >&2
  echo "  src/kernel/cxadc.c: ${kernel_module_version}" >&2
  echo "  README.md: ${readme_version}" >&2
  echo "  INSTALL.md: ${install_doc_version}" >&2
  exit 1
fi
if [[ -z "${dkms_bin}" ]]; then
  echo "ERROR: dkms not found on deployment host" >&2
  exit 1
fi
if [[ -z "${depmod_bin}" ]]; then
  echo "ERROR: depmod not found on deployment host" >&2
  exit 1
fi
if [[ -z "${rsync_bin}" ]]; then
  echo "ERROR: rsync not found on deployment host" >&2
  exit 1
fi

if [[ "${dkms_install_all_kernels}" != "true" && -z "${dkms_target_kernel}" ]]; then
  echo "ERROR: DKMS_TARGET_KERNEL must be set when DKMS_INSTALL_ALL_KERNELS is false" >&2
  exit 1
fi

echo "DKMS deploy options:"
echo "  target kernel: ${dkms_target_kernel}"
echo "  install all kernels: ${dkms_install_all_kernels}"
echo "  prune old versions: ${prune_old_dkms_versions}"
echo "  prune dry run: ${prune_dry_run}"

echo "Installing userspace tools/scripts from prebuilt artifacts..."
for tool in leveladj levelmon cx-capture; do
  if [[ ! -x "${build_bin}/${tool}" ]]; then
    echo "ERROR: missing prebuilt tool ${build_bin}/${tool}" >&2
    echo "Run 'make clean && make' as non-root before invoking this helper." >&2
    exit 1
  fi
done

install -d "${install_bin}"
install -m 0755 "${build_bin}/leveladj" "${install_bin}/leveladj"
install -m 0755 "${build_bin}/levelmon" "${install_bin}/levelmon"
install -m 0755 "${build_bin}/cx-capture" "${install_bin}/cx-capture"
install -m 0755 "${repo_dir}/src/tools/cxadc-status/cxadc-status" "${install_bin}/cxadc-status"

for script in "${repo_dir}/src/scripts"/cx*; do
  [[ -f "${script}" ]] || continue
  install -m 0755 "${script}" "${install_bin}/$(basename "${script}")"
done

echo "Installing kernel module via DKMS ${dkms_name}/${dkms_version}..."
dkms_source_root="/usr/src/${dkms_name}-${dkms_version}"
dkms_source_staging="${dkms_source_root}.tmp.$$"

echo "Staging DKMS source payload into ${dkms_source_root}..."
rm -rf "${dkms_source_staging}"
mkdir -p "${dkms_source_staging}"
"${rsync_bin}" -a --delete \
  --include='dkms.conf' \
  --include='Makefile' \
  --include='src/' \
  --include='src/kernel/***' \
  --exclude='*' \
  "${repo_dir}/" "${dkms_source_staging}/"
rm -rf "${dkms_source_root}"
mv "${dkms_source_staging}" "${dkms_source_root}"

"${dkms_bin}" remove -m "${dkms_name}" -v "${dkms_version}" --all || true
"${dkms_bin}" add "${dkms_source_root}"

build_install_for_kernel() {
  local kernel_release="$1"
  if [[ ! -d "/lib/modules/${kernel_release}/build" ]]; then
    # This used to `return 0` quietly. That silent skip is exactly what makes
    # pruning dangerous: the stale version gets removed while this kernel never
    # received the new one, leaving a bootable kernel with no cxadc at all.
    # Nothing is decided here -- convergence is verified from `dkms status`
    # below, which is authoritative about what actually landed.
    echo "WARNING: kernel ${kernel_release}: no build tree (kernel-devel missing); cannot build" >&2
    return 0
  fi
  echo "Building DKMS module for kernel ${kernel_release}..."
  if ! "${dkms_bin}" build -m "${dkms_name}" -v "${dkms_version}" -k "${kernel_release}"; then
    echo "WARNING: kernel ${kernel_release}: dkms build failed" >&2
    return 0
  fi
  echo "Installing DKMS module for kernel ${kernel_release}..."
  if ! "${dkms_bin}" install -m "${dkms_name}" -v "${dkms_version}" -k "${kernel_release}" --force; then
    echo "WARNING: kernel ${kernel_release}: dkms install failed" >&2
    return 0
  fi
}

# ---------------------------------------------------------------------------
# Convergence and prune helpers
# ---------------------------------------------------------------------------

# Kernels for which ${dkms_name}/${dkms_version} is reported 'installed'.
# `dkms status` lines look like:  cxadc/1.1, 7.1.12-200.fc44.x86_64, x86_64: installed
current_version_installed_kernels() {
  "${dkms_bin}" status -m "${dkms_name}" -v "${dkms_version}" 2>/dev/null \
    | awk -F'[,:]' '
        /: *installed/ {
          gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2)
          if ($2 != "") print $2
        }
      ' \
    | sort -u
}

# Every version of this package DKMS currently knows about.
all_dkms_versions() {
  "${dkms_bin}" status -m "${dkms_name}" 2>/dev/null \
    | awk -F'[/,]' -v name="${dkms_name}" '
        $1 == name {
          gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2)
          if ($2 != "") print $2
        }
      ' \
    | sort -u
}

# "<version> <kernel>" for every registered pair, whatever its status.
dkms_version_kernel_pairs() {
  "${dkms_bin}" status -m "${dkms_name}" 2>/dev/null \
    | sed -nE "s#^${dkms_name}/([^,]+), *([^,]+), *[^:]+:.*#\1 \2#p"
}

# Kernel directories under /lib/modules belonging to a real, bootable kernel.
# modules.dep is written by every properly installed kernel; a leftover
# directory holding nothing but an orphaned extra/cxadc.ko has none, and must
# not be allowed to block pruning forever.
bootable_kernels() {
  local kdir kname
  for kdir in /lib/modules/*/; do
    kname="${kdir%/}"
    kname="${kname##*/}"
    [[ -f "/lib/modules/${kname}/modules.dep" ]] || continue
    printf '%s\n' "${kname}"
  done | sort
}

# Bootable kernels that did NOT end up with the current version. Derived from
# end state rather than from what the build loop believes it did, so it catches
# a missing build tree, a failed build, a partial install and a deliberately
# narrow DKMS_INSTALL_ALL_KERNELS=false run alike.
unconverged_kernels=()
compute_unconverged_kernels() {
  local -A have=()
  local k
  while IFS= read -r k; do
    [[ -n "${k}" ]] && have["${k}"]=1
  done < <(current_version_installed_kernels)

  unconverged_kernels=()
  while IFS= read -r k; do
    [[ -n "${k}" ]] || continue
    [[ -n "${have[${k}]:-}" ]] && continue
    unconverged_kernels+=("${k}")
  done < <(bootable_kernels)
}

# Remove every trace of superseded cxadc state. Only ever called once every
# bootable kernel carries the current version, so no stage here can be the
# thing that leaves a kernel driverless.
prune_stale_dkms_state() {
  local did_remove="false"
  local prefix="  "
  if [[ "${prune_dry_run}" == "true" ]]; then
    prefix="  [dry-run] "
  fi

  # -- a. superseded package versions ---------------------------------------
  local stale_version
  while IFS= read -r stale_version; do
    [[ -n "${stale_version}" ]] || continue
    [[ "${stale_version}" == "${dkms_version}" ]] && continue
    echo "${prefix}remove version: ${dkms_name}/${stale_version}"
    if [[ "${prune_dry_run}" != "true" ]]; then
      "${dkms_bin}" remove -m "${dkms_name}" -v "${stale_version}" --all || true
    fi
    did_remove="true"
  done < <(all_dkms_versions)

  # -- b. stale /usr/src trees ----------------------------------------------
  # `dkms remove` leaves these behind. A surviving tree can be re-registered by
  # a stray `dkms add`, resurrecting the version we just removed.
  local src_tree src_version
  for src_tree in "/usr/src/${dkms_name}-"*; do
    [[ -d "${src_tree}" ]] || continue
    src_version="${src_tree##*/}"
    src_version="${src_version#"${dkms_name}-"}"
    [[ -n "${src_version}" ]] || continue
    [[ "${src_version}" == "${dkms_version}" ]] && continue
    echo "${prefix}remove source tree: ${src_tree}"
    if [[ "${prune_dry_run}" != "true" ]]; then
      rm -rf -- "${src_tree}"
    fi
    did_remove="true"
  done

  # -- c. state for kernels that no longer exist ----------------------------
  # These accumulate as the distro rolls kernels past installonly_limit.
  local pair_version pair_kernel
  while read -r pair_version pair_kernel; do
    [[ -n "${pair_version}" && -n "${pair_kernel}" ]] || continue
    [[ -d "/lib/modules/${pair_kernel}" ]] && continue
    echo "${prefix}remove orphaned entry: ${dkms_name}/${pair_version} for absent kernel ${pair_kernel}"
    if [[ "${prune_dry_run}" != "true" ]]; then
      "${dkms_bin}" remove -m "${dkms_name}" -v "${pair_version}" -k "${pair_kernel}" || true
    fi
    did_remove="true"
  done < <(dkms_version_kernel_pairs)

  local kdir kname
  for kdir in "/var/lib/dkms/${dkms_name}/kernel-"*; do
    [[ -d "${kdir}" ]] || continue
    kname="${kdir##*/}"
    kname="${kname#kernel-}"
    kname="${kname%-*}"
    [[ -n "${kname}" ]] || continue
    [[ -d "/lib/modules/${kname}" ]] && continue
    echo "${prefix}remove orphaned dkms state: ${kdir}"
    if [[ "${prune_dry_run}" != "true" ]]; then
      rm -rf -- "${kdir}"
    fi
    did_remove="true"
  done

  # -- d. cxadc.ko with no live DKMS entry ----------------------------------
  # Basenames are matched literally: only this package's module is ever a
  # candidate, never a neighbouring one.
  local -A owned=()
  local k
  while IFS= read -r k; do
    [[ -n "${k}" ]] && owned["${k}"]=1
  done < <(current_version_installed_kernels)

  local -A depmod_needed=()
  local ko ko_kernel
  for ko in "/lib/modules/"*"/extra/${dkms_name}.ko" \
            "/lib/modules/"*"/extra/${dkms_name}.ko."* \
            "/lib/modules/"*"/updates/dkms/${dkms_name}.ko" \
            "/lib/modules/"*"/updates/dkms/${dkms_name}.ko."*; do
    [[ -f "${ko}" ]] || continue
    ko_kernel="${ko#/lib/modules/}"
    ko_kernel="${ko_kernel%%/*}"
    [[ -n "${owned[${ko_kernel}]:-}" ]] && continue
    echo "${prefix}remove orphaned module: ${ko}"
    if [[ "${prune_dry_run}" != "true" ]]; then
      rm -f -- "${ko}"
      depmod_needed["${ko_kernel}"]=1
    fi
    did_remove="true"
  done

  # depmod per affected kernel: the unconditional `depmod -a` further down only
  # rebuilds the running kernel's dependency files.
  for k in "${!depmod_needed[@]}"; do
    echo "${prefix}depmod for ${k}"
    "${depmod_bin}" -a "${k}" || true
  done

  if [[ "${did_remove}" != "true" ]]; then
    echo "  nothing to prune"
  fi
}

# ---------------------------------------------------------------------------

if [[ "${dkms_install_all_kernels}" == "true" ]]; then
  mapfile -t installed_kernels < <(find /lib/modules -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort)
  if [[ "${#installed_kernels[@]}" -eq 0 ]]; then
    echo "ERROR: no kernels found under /lib/modules" >&2
    exit 1
  fi
  # Target kernel first, so the kernel this host is actually running is
  # converged before any slower back-fill can go wrong.
  ordered_kernels=()
  for kernel_release in "${installed_kernels[@]}"; do
    [[ "${kernel_release}" == "${dkms_target_kernel}" ]] && ordered_kernels+=("${kernel_release}")
  done
  for kernel_release in "${installed_kernels[@]}"; do
    [[ "${kernel_release}" == "${dkms_target_kernel}" ]] && continue
    ordered_kernels+=("${kernel_release}")
  done
  for kernel_release in "${ordered_kernels[@]}"; do
    build_install_for_kernel "${kernel_release}"
  done
else
  build_install_for_kernel "${dkms_target_kernel}"
fi

compute_unconverged_kernels

if [[ "${prune_old_dkms_versions}" != "true" ]]; then
  echo "Skipping prune (PRUNE_OLD_DKMS_VERSIONS=false)"
elif [[ "${#unconverged_kernels[@]}" -gt 0 ]]; then
  # Refuse to remove the old version while any bootable kernel is still
  # relying on it. A noisy `dkms status` is a far better outcome than a
  # fallback kernel that boots without a capture driver.
  echo "Skipping prune: ${dkms_name}/${dkms_version} is not installed for:" >&2
  for kernel_release in "${unconverged_kernels[@]}"; do
    echo "  - ${kernel_release}" >&2
  done
  echo "Removing older versions now would leave those kernels with no ${dkms_name}." >&2
else
  echo "Pruning stale ${dkms_name} DKMS state (keeping ${dkms_version})..."
  prune_stale_dkms_state

  # Post-prune safety net. Removing an old version touches shared paths --
  # /lib/modules/<k>/extra/${dkms_name}.ko* is a single file that whichever
  # version installed last owns -- so re-derive convergence from scratch and
  # confirm a module actually exists on disk for every bootable kernel. Captures
  # are irreplaceable; a prune that quietly took the driver with it must be a
  # loud failure, not a surprise at the next boot.
  if [[ "${prune_dry_run}" != "true" ]]; then
    compute_unconverged_kernels
    if [[ "${#unconverged_kernels[@]}" -gt 0 ]]; then
      echo "ERROR: prune left these kernels without ${dkms_name}/${dkms_version}:" >&2
      for kernel_release in "${unconverged_kernels[@]}"; do
        echo "  - ${kernel_release}" >&2
      done
      echo "Re-run this helper to rebuild them before booting those kernels." >&2
      exit 1
    fi

    missing_modules=()
    while IFS= read -r kernel_release; do
      [[ -n "${kernel_release}" ]] || continue
      if ! compgen -G "/lib/modules/${kernel_release}/extra/${dkms_name}.ko*" >/dev/null \
         && ! compgen -G "/lib/modules/${kernel_release}/updates/dkms/${dkms_name}.ko*" >/dev/null; then
        missing_modules+=("${kernel_release}")
      fi
    done < <(bootable_kernels)
    if [[ "${#missing_modules[@]}" -gt 0 ]]; then
      echo "ERROR: ${dkms_name} reports installed but no module file exists for:" >&2
      for kernel_release in "${missing_modules[@]}"; do
        echo "  - ${kernel_release}" >&2
      done
      exit 1
    fi
    echo "Verified ${dkms_name}/${dkms_version} present for every bootable kernel."
  fi
fi

"${depmod_bin}" -a

echo "Installing modprobe and udev config..."
make -C "${repo_dir}" install-config

echo "Reloading udev rules for cxadc devices..."
udevadm control --reload-rules
udevadm trigger -c add -s cxadc || true

# Failing here rather than mid-flight: everything above is idempotent and
# leaves the host coherent, so a partial deploy still has working tools, config
# and udev rules. Exiting non-zero keeps deploy.yml from writing its stamp
# file, so the next run retries instead of skipping.
#
# Only fatal when we set out to converge every kernel. A deliberately narrow
# DKMS_INSTALL_ALL_KERNELS=false run is expected to leave other kernels alone;
# it still declines to prune (above), but it is not a failure.
if [[ "${#unconverged_kernels[@]}" -gt 0 ]]; then
  if [[ "${dkms_install_all_kernels}" == "true" ]]; then
    echo "ERROR: ${dkms_name}/${dkms_version} is not installed for every bootable kernel:" >&2
    for kernel_release in "${unconverged_kernels[@]}"; do
      echo "  - ${kernel_release}" >&2
    done
    echo "Install the matching kernel-devel package and re-run this helper." >&2
    echo "Until then those kernels have no ${dkms_name}: do not boot them expecting" >&2
    echo "a capture card." >&2
    exit 1
  fi
  echo "NOTE: ${dkms_name}/${dkms_version} is not installed for:" >&2
  for kernel_release in "${unconverged_kernels[@]}"; do
    echo "  - ${kernel_release}" >&2
  done
fi

echo "Root deploy helper completed"
