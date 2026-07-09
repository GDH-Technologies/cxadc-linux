#!/usr/bin/env bash
set -euo pipefail

repo_dir="/opt/cxadc-linux"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "ERROR: deploy_dkms_install.sh must run as root" >&2
  exit 1
fi

if [[ ! -d "${repo_dir}/.git" ]]; then
  echo "ERROR: repo not found at ${repo_dir}" >&2
  exit 1
fi

cd "${repo_dir}"

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

echo "Installing userspace tools/scripts..."
make -C "${repo_dir}" install

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
