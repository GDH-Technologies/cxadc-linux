## Fedora DKMS Installation (cxadc)

These instructions are specific to Fedora and the current repository layout
(`src/kernel/` module source, DKMS package version `1.2`).

### 1) Prerequisites

Install build dependencies for your running kernel:

```bash
sudo dnf install -y dkms gcc make rsync kernel-devel-$(uname -r) kernel-headers-$(uname -r)
```

Optional but commonly useful capture tools:

```bash
sudo dnf install -y ffmpeg sox pv flac
```

If Secure Boot is enabled, unsigned DKMS modules will not load unless you sign
and enroll keys. Check status:

```bash
mokutil --sb-state
```

### 2) Clone and enter the repo

```bash
git clone https://github.com/GDH-Technologies/cxadc-linux.git cxadc-linux
cd cxadc-linux
```

> [!NOTE]
> `happycube/cxadc-linux3` is useful as upstream historical reference, but this
> installation guide is for `GDH-Technologies/cxadc-linux`.

### 3) Remove older DKMS installs (recommended migration step)

> [!NOTE]
> On the GDH capture rigs this is automatic. `src/scripts/deploy_dkms_install.sh`
> prunes superseded DKMS state on every deploy (`PRUNE_OLD_DKMS_VERSIONS`,
> default on). The manual steps below are for a first-time or off-fleet install.

The automatic prune removes four things, and only once **every** bootable kernel
already has the current version installed:

1. superseded package versions — `dkms remove -m cxadc -v <old> --all`
2. their leftover `/usr/src/cxadc-<old>` trees, which a stray `dkms add` would
   otherwise resurrect
3. DKMS state for kernels no longer present in `/lib/modules` (these accumulate
   as the distro rolls kernels past `installonly_limit`)
4. any `cxadc.ko*` under `/lib/modules/*/extra/` or
   `/lib/modules/*/updates/dkms/` that no live DKMS entry owns, followed by a
   `depmod` for that kernel

> [!IMPORTANT]
> The ordering is the safety property, not an implementation detail. Pruning the
> old version *before* the new one reaches every kernel would strip the driver
> from your fallback kernels — the ones you boot when the newest kernel misbehaves.
> The helper refuses to prune while any bootable kernel is still missing the
> current version, and says which kernels blocked it.

To do it by hand, repeat this for each version you previously installed
(`0.1`, `0.5`, `1.0` and `1.1`, which is what the fleet ran before `1.2`):

```bash
sudo dkms remove -m cxadc -v 0.1 --all || true
sudo rm -rf /usr/src/cxadc-0.1
```

### 4) Stage source for DKMS

Copy this repository into DKMS source path:

```bash
sudo mkdir -p /usr/src/cxadc-1.2
sudo rsync -a --delete --exclude '.git' --exclude 'build' ./ /usr/src/cxadc-1.2/
```

### 5) Build and install with DKMS

```bash
sudo dkms add -m cxadc -v 1.2
sudo dkms build -m cxadc -v 1.2
sudo dkms install -m cxadc -v 1.2
sudo depmod -a
```

### 6) Install runtime config files

```bash
sudo cp config/cxadc.conf /etc/modprobe.d/
sudo cp config/cxadc.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
sudo udevadm trigger
```

### 7) Load the driver

Unload potentially conflicting stock cx88 modules, then load `cxadc`:

```bash
sudo modprobe -r cx88_alsa cx8800 cx88xx cx8802 cx88_blackbird cx2341x || true
sudo modprobe cxadc
```

### 8) Verify installation

```bash
dkms status | grep cxadc
lsmod | grep cxadc
ls -l /dev/cxadc*
```

Expected: `cxadc/1.2` shown by DKMS and at least `/dev/cxadc0` present when
supported hardware is installed.

If you define host-specific udev aliases (for example `/dev/cx/vcr0-video`),
you can validate both alias path and bare alias resolution:

```bash
cxresolve /dev/cx/vcr0-video
cxresolve vcr0-video
```

### 9) Optional: install userland tools

DKMS installs only the kernel module. Install CLI tools separately:

```bash
sudo make install
```

This installs `leveladj`, `levelmon`, `cx-capture`, `cxadc-status`, and helper
scripts into `/usr/local/bin` by default.

### 10) After kernel updates

`AUTOINSTALL="YES"` in `dkms.conf` means a **newly installed** kernel gets the
current version built for it automatically at kernel install/boot. To force a
refresh manually:

```bash
sudo rsync -a --delete --exclude '.git' --exclude 'build' ./ /usr/src/cxadc-1.2/
sudo dkms build -m cxadc -v 1.2
sudo dkms install -m cxadc -v 1.2 --force
sudo depmod -a
```

Autoinstall does **not** back-fill kernels that were already installed before a
version bump: those keep whatever version they had. That is what
`DKMS_INSTALL_ALL_KERNELS` (default on) is for — it builds and installs the
current version for every kernel in `/lib/modules`, so a fallback boot gets the
same driver as the primary one. To back-fill by hand:

```bash
for k in $(ls -1 /lib/modules); do
  [ -d "/lib/modules/$k/build" ] || { echo "skip $k (no kernel-devel)"; continue; }
  sudo dkms build   -m cxadc -v 1.2 -k "$k"
  sudo dkms install -m cxadc -v 1.2 -k "$k" --force
done
sudo depmod -a
```

A healthy fleet host shows the current version, and only the current version,
installed for every kernel it can boot:

```console
$ dkms status -m cxadc
cxadc/1.2, 7.1.9-200.fc44.x86_64,  x86_64: installed
cxadc/1.2, 7.1.10-200.fc44.x86_64, x86_64: installed
cxadc/1.2, 7.1.12-200.fc44.x86_64, x86_64: installed
```

### 11) Uninstall

Remove DKMS module:

```bash
sudo dkms remove -m cxadc -v 1.2 --all
```

Optional cleanup of staged source:

```bash
sudo rm -rf /usr/src/cxadc-1.2
```

### Sudoers for CI/CD

Create a dedicated sudoers drop-in for the runner user (example user: `rdodge`).

Edit with `visudo`:

```bash
sudo visudo -f /etc/sudoers.d/cxadc-linux-deploy
```

Use this content (update `rdodge` only if your runner user differs):

```sudoers
User_Alias CXADC_RUNNER = rdodge

Cmnd_Alias CXADC_DEPLOY_INSTALL = /usr/bin/bash /home/rdodge/Repos/cxadc-linux/src/scripts/deploy_dkms_install.sh
Cmnd_Alias CXADC_DEPLOY_INSTALL_ENV = /usr/bin/env DKMS_TARGET_KERNEL=* DKMS_INSTALL_ALL_KERNELS=* PRUNE_OLD_DKMS_VERSIONS=* /usr/bin/bash /home/rdodge/Repos/cxadc-linux/src/scripts/deploy_dkms_install.sh
Cmnd_Alias CXADC_MODPROBE_REMOVE = /usr/sbin/modprobe -r cxadc
Cmnd_Alias CXADC_MODPROBE_LOAD = /usr/sbin/modprobe cxadc

CXADC_RUNNER ALL=(root) NOPASSWD: \
	CXADC_DEPLOY_INSTALL, \
	CXADC_DEPLOY_INSTALL_ENV, \
	CXADC_MODPROBE_REMOVE, CXADC_MODPROBE_LOAD
```

Set secure file ownership and permissions:

```bash
sudo chown root:root /etc/sudoers.d/cxadc-linux-deploy
sudo chmod 0440 /etc/sudoers.d/cxadc-linux-deploy
```

Quick verification for the runner user:

```bash
sudo -n -l | grep -E 'deploy_dkms_install\.sh|modprobe'
```

> [!IMPORTANT]
> Use absolute paths exactly as shown. Sudoers command matching is strict; any
> path mismatch (for example omitting `/home/rdodge/Repos/cxadc-linux/...`) will
> cause `sudo: a password is required` in GitHub Actions.

> [!CAUTION]
> `CXADC_DEPLOY_INSTALL_ENV` matches the command line **positionally**. It lists
> exactly three variables — `DKMS_TARGET_KERNEL`, `DKMS_INSTALL_ALL_KERNELS`,
> `PRUNE_OLD_DKMS_VERSIONS` — in that order, and `deploy.yml` passes exactly
> those three, in that order. Adding a fourth variable to the `run_as_root env`
> call, removing one, or reordering them stops matching this alias, and every
> deploy on every rig falls back to a password prompt it cannot answer.
>
> This is why `DKMS_PRUNE_DRY_RUN` is **not** passed through `deploy.yml`. The
> script honours it, but only from a real root shell:
>
> ```bash
> sudo -i
> DKMS_PRUNE_DRY_RUN=true bash /home/rdodge/Repos/cxadc-linux/src/scripts/deploy_dkms_install.sh
> ```
>
> If you ever do need a new variable in the CI path, update this sudoers file on
> **cs0, cs1 and wm** first, then `deploy.yml` — never the other way round.

```

```