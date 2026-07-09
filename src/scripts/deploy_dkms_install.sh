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

dkms_bin="$(command -v dkms || true)"
depmod_bin="$(command -v depmod || true)"

if [[ -z "${dkms_name}" || -z "${dkms_version}" ]]; then
  echo "ERROR: failed to parse PACKAGE_NAME/PACKAGE_VERSION from dkms.conf" >&2
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
"${dkms_bin}" remove -m "${dkms_name}" -v "${dkms_version}" --all || true
"${dkms_bin}" add "${repo_dir}"
"${dkms_bin}" build -m "${dkms_name}" -v "${dkms_version}"
"${dkms_bin}" install -m "${dkms_name}" -v "${dkms_version}" --force
"${depmod_bin}" -a

echo "Installing modprobe and udev config..."
make -C "${repo_dir}" install-config

echo "Reloading udev rules for cxadc devices..."
udevadm control --reload-rules
udevadm trigger -c add -s cxadc || true

echo "Root deploy helper completed"
