## Fedora DKMS Installation (cxadc)

These instructions are specific to Fedora and the current repository layout
(`src/kernel/` module source, DKMS package version `0.5`).

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

If you previously installed `cxadc/0.1`, remove it first:

```bash
sudo dkms remove -m cxadc -v 0.1 --all || true
sudo rm -rf /usr/src/cxadc-0.1
```

Also clear any prior `0.5` source copy before refreshing:

```bash
sudo dkms remove -m cxadc -v 0.5 --all || true
sudo rm -rf /usr/src/cxadc-0.5
```

### 4) Stage source for DKMS

Copy this repository into DKMS source path:

```bash
sudo mkdir -p /usr/src/cxadc-0.5
sudo rsync -a --delete --exclude '.git' --exclude 'build' ./ /usr/src/cxadc-0.5/
```

### 5) Build and install with DKMS

```bash
sudo dkms add -m cxadc -v 0.5
sudo dkms build -m cxadc -v 0.5
sudo dkms install -m cxadc -v 0.5
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

Expected: `cxadc/0.5` shown by DKMS and at least `/dev/cxadc0` present when
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

DKMS should auto-rebuild at kernel install/boot. To force a refresh manually:

```bash
sudo rsync -a --delete --exclude '.git' --exclude 'build' ./ /usr/src/cxadc-0.5/
sudo dkms build -m cxadc -v 0.5
sudo dkms install -m cxadc -v 0.5 --force
sudo depmod -a
```

### 11) Uninstall

Remove DKMS module:

```bash
sudo dkms remove -m cxadc -v 0.5 --all
```

Optional cleanup of staged source:

```bash
sudo rm -rf /usr/src/cxadc-0.5
```

