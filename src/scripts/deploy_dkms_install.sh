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
dkms_install_all_kernels="$(to_bool "${DKMS_INSTALL_ALL_KERNELS:-false}")"
prune_old_dkms_versions="$(to_bool "${PRUNE_OLD_DKMS_VERSIONS:-false}")"

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
if [[ "${dkms_version}" != "${kernel_module_version}" || "${dkms_version}" != "${readme_version}" ]]; then
  echo "ERROR: version mismatch detected:" >&2
  echo "  dkms.conf: ${dkms_version}" >&2
  echo "  src/kernel/cxadc.c: ${kernel_module_version}" >&2
  echo "  README.md: ${readme_version}" >&2
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
    echo "Skipping kernel ${kernel_release}: headers/build tree missing" >&2
    return 0
  fi
  echo "Building DKMS module for kernel ${kernel_release}..."
  "${dkms_bin}" build -m "${dkms_name}" -v "${dkms_version}" -k "${kernel_release}"
  echo "Installing DKMS module for kernel ${kernel_release}..."
  "${dkms_bin}" install -m "${dkms_name}" -v "${dkms_version}" -k "${kernel_release}" --force
}

if [[ "${dkms_install_all_kernels}" == "true" ]]; then
  mapfile -t installed_kernels < <(find /lib/modules -mindepth 1 -maxdepth 1 -type d -printf '%f\n' | sort)
  if [[ "${#installed_kernels[@]}" -eq 0 ]]; then
    echo "ERROR: no kernels found under /lib/modules" >&2
    exit 1
  fi
  for kernel_release in "${installed_kernels[@]}"; do
    build_install_for_kernel "${kernel_release}"
  done
else
  build_install_for_kernel "${dkms_target_kernel}"
fi

if [[ "${prune_old_dkms_versions}" == "true" ]]; then
  echo "Pruning stale DKMS versions for ${dkms_name} (keeping ${dkms_version})..."
  while IFS= read -r stale_version; do
    [[ -n "${stale_version}" ]] || continue
    [[ "${stale_version}" == "${dkms_version}" ]] && continue
    echo "Removing stale version ${dkms_name}/${stale_version}"
    "${dkms_bin}" remove -m "${dkms_name}" -v "${stale_version}" --all || true
  done < <(
    "${dkms_bin}" status -m "${dkms_name}" \
      | awk -F'[/,]' -v name="${dkms_name}" '
          $1 == name {
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", $2)
            if ($2 != "") print $2
          }
        ' \
      | sort -u
  )
fi

"${depmod_bin}" -a

echo "Installing modprobe and udev config..."
make -C "${repo_dir}" install-config

echo "Reloading udev rules for cxadc devices..."
udevadm control --reload-rules
udevadm trigger -c add -s cxadc || true

echo "Root deploy helper completed"
