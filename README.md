# CXADC (CX - Analogue-Digital Converter)

Alternative Linux driver for the Conexant CX2388x series video decoder/encoder chips, configured to capture raw 8-bit or 16-bit unsigned samples (28–54 MHz) from video input ports. Enables low-cost ADC use for SDR and similar applications.

> [!WARNING]
> - **Secure Boot must be disabled** before driver installation
> - **Do not load both `cxadc` and the regular `cx88` driver simultaneously**
> - Kernel updates require driver reinstallation unless **DKMS is configured**

> [!NOTE]
> - Compatible: CX2388x, CX25800
> - Incompatible: CX23885-xx, CX23888-xx
> - See the [wiki](https://github.com/happycube/cxadc-linux3/wiki) for hardware variants, modifications, and cabling

## Hardware Notes

- **CX25800** (White cards): Lowest self-noise; recommended for best results
- **Stock crystal**: 28.6 MHz (28.636 MSPS); outputs ~14 MHz bandwidth
- **Upgrades**: `ABLS2-40.000MHZ-D4YF-T` crystal replacement for 40 MHz capability; see [crystal upgrade list](https://github.com/happycube/cxadc-linux3/wiki/Crystal-Upgrades)
- **Cooling**: Important for 40–54 MHz crystals; keep ≤10°C above ambient
- **MSPS terminology**: When 28 MHz is specified, effective bandwidth is 14 MHz (Nyquist: 2:1 ratio)
- **System resources**: Lower-end systems (Pentium 4 era) may drop samples; SoX/FLAC encoding in real-time requires adequate resources
- **PCIe bridge**: Asmedia chips may have issues on older Intel PCH (3rd gen); ITE chips are more reliable
- **Purchasing**: White CX25800 cards (~16–30 USD) from [AliExpress](https://github.com/happycube/cxadc-linux3/wiki/Hardware-and-Purchasing)

## Quick Links

- [Wiki](https://github.com/happycube/cxadc-linux3/wiki): Card variants, modifications, and cabling
- [Crystal upgrades](https://github.com/happycube/cxadc-linux3/wiki/Crystal-Upgrades): Higher-frequency replacements
- [Utils README](utils/README.md): Scripted tools for configuration and monitoring
- [Clockgen README](clockgen/README.md): Synchronized multi-card clock generator and audio ADC overview
- [Clockgen BUILDING](clockgen/BUILDING.md): Firmware build and flashing instructions

## Optional Clockgen Integration

This repository also contains the clockgen firmware and helper scripts in clockgen/.
Use this when you run synchronized multi-card capture rigs and want shared sample clocks plus auxiliary linear audio capture.

- Firmware sources: clockgen/firmware/
- Capture/diagnostic scripts: clockgen/scripts/
- Build and flashing guide: [clockgen/BUILDING.md](clockgen/BUILDING.md)

Hardware design and mechanical files are maintained upstream:
- [GDH-Technologies/cx-clockgen](https://github.com/GDH-Technologies/cx-clockgen)


## Installation

### Prerequisites (Fedora)

**Disable Secure Boot** before proceeding. Check status:

```bash
mokutil --sb-state
```

Install dependencies:

```bash
sudo dnf install -y dkms gcc make kernel-devel-$(uname -r) kernel-headers rsync
```

Optional (for capture and monitoring):

```bash
sudo dnf install -y ffmpeg sox pv flac
```

### Installation (Fedora with DKMS – Recommended)

This setup automatically rebuilds the driver when the kernel updates.

1. **Clone the repository** (or [download the ZIP](https://github.com/happycube/cxadc-linux3/archive/refs/heads/master.zip)):

```bash
git clone https://github.com/happycube/cxadc-linux3 cxadc
cd cxadc
```

2. **Copy source to DKMS directory** and register:

```bash
sudo dkms remove -m cxadc -v 0.1 --all || true
sudo rm -rf /usr/src/cxadc-0.1
sudo mkdir -p /usr/src/cxadc-0.1
sudo rsync -a --delete --exclude '.git' ./ /usr/src/cxadc-0.1/
```

3. **Build and install** via DKMS:

```bash
sudo dkms add -m cxadc -v 0.1
sudo dkms build -m cxadc -v 0.1
sudo dkms install -m cxadc -v 0.1
sudo depmod -a
```

4. **Install configuration files**:

```bash
sudo cp cxadc.conf /etc/modprobe.d/
sudo cp cxadc.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
```

5. **Load driver and verify**:

```bash
sudo modprobe -r cx88_alsa cx8800 cx88xx cx8802 cx88_blackbird cx2341x || true
sudo modprobe cxadc
```

Verify installation:

```bash
dkms status | grep cxadc
lsmod | grep cxadc
ls -l /dev/cxadc*
```

Success: Device node `/dev/cxadc0` is present.

### After Kernel Updates (DKMS)

When the kernel is updated, DKMS automatically rebuilds the driver at next boot. To manually rebuild:

```bash
sudo rsync -a --delete --exclude '.git' ./ /usr/src/cxadc-0.1/
sudo dkms build -m cxadc -v 0.1
sudo dkms install -m cxadc -v 0.1 --force
sudo depmod -a
```

### Remove DKMS Installation

```bash
sudo dkms remove -m cxadc -v 0.1 --all
```

### Alternative Installation (Manual – Not Recommended)

If you prefer a one-time build without DKMS:

```bash
cd cxadc
make
sudo make modules_install
sudo depmod -a
sudo cp cxadc.conf /etc/modprobe.d/
sudo cp cxadc.rules /etc/udev/rules.d/
```

**Note:** Manual builds require reinstallation after every kernel update.

### Other Linux Distributions

<details>
<summary><b>Ubuntu 22.04 & Debian</b></summary>

Install dependencies:

```bash
sudo apt update
sudo apt install dkms build-essential linux-headers-$(uname -r) rsync ffmpeg sox pv
```

For **FLAC 1.5.0+** (required for real-time multi-threaded encoding):

```bash
sudo apt install -y build-essential cmake libogg-dev
wget https://github.com/xiph/flac/releases/download/1.5.0/flac-1.5.0.tar.xz
tar -xf flac-1.5.0.tar.xz
cd flac-1.5.0
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
sudo make install
sudo ldconfig
```

Then follow the **Installation (DKMS)** steps above.

</details>

<details>
<summary><b>Raspberry Pi OS (Raspberry Pi 4 or 5 with PCIe adapter)</b></summary>

Install dependencies:

```bash
sudo apt update
sudo apt install dkms build-essential raspberrypi-kernel-headers rsync ffmpeg sox pv flac
```

Enable PCIe 32-bit DMA in `/boot/firmware/config.txt`:

```
[all]
dtoverlay=pcie-32bit-dma
```

Then follow the **Installation (DKMS)** steps above.

</details>


## Configuration

### User Permissions

To change module parameters without `sudo`, add your user to the `video` group:

```bash
sudo usermod -a -G video $USER
```

Log out and back in for the change to take effect.

### Module Parameters

Module parameters control driver behavior. Most can be changed at runtime via `sysfs` without reloading the driver.

**Change a parameter:**

```bash
echo <value> >/sys/class/cxadc/cxadc0/device/parameters/<param>
```

**Multi-card usage:** Replace `cxadc0` with `cxadc1`, `cxadc2`, etc.

**Persistent configuration:** Edit `/etc/modprobe.d/cxadc.conf` to set parameters at boot, or edit `/etc/udev/rules.d/cxadc.rules` for device-specific settings.

> [!NOTE]
> Use `cxvalues` (from utils) to display current configuration at any time.

### Input Selection

#### `vmux` (0–3, default: 2) – Physical input port

Selects which video input to capture. Most TV cards have:
- 0: Tuner
- 1: Composite (RCA/BNC)
- 2: S-Video  
- 3: Alternative input (varies by card)

**Find the correct input:**

```bash
# Attach a live signal and change vmux until you see a response
for i in 0 1 2 3; do
  echo "Testing vmux=$i..."
  echo $i >/sys/class/cxadc/cxadc0/device/parameters/vmux
  sudo ffplay -hide_banner -async 1 -f rawvideo -pixel_format gray8 \
    -video_size 1832x625 -i /dev/cxadc0 -vf scale=1135x625,eq=gamma=0.5:contrast=1.5 &
  sleep 2
  pkill ffplay
done
```

See [Card Types Wiki](https://github.com/happycube/cxadc-linux3/wiki/Types-Of-CX2388x-Cards) for optimal connections.

#### `audsel` (0–3, default: none) – Audio multiplexer

For TV cards with external audio selection (e.g., PixelView PlayTV Pro Ultra):
- 0: Tuner TV audio
- 1: Silence
- 2: FM stereo tuner
- 3: Audio input to output

### Gain and Signal Conditioning

#### `sixdb` (0 or 1, default: 1) – 6 dB gain boost

- `1`: Enabled (default; +6 dB gain)
- `0`: Disabled (cleaner signal; may require external amplifier)

**Use case:** Disable for [raw CVBS](https://github.com/oyvindln/vhs-decode/wiki/CVBS-Composite-Decode) or with external [AD4857](https://github.com/happycube/cxadc-linux3/wiki/Modifications#external-amplification) amplifiers.

#### `level` (0–31, default: 16) – Fixed digital gain

Applies fixed gain (0–31) to the input signal. Adjust to minimize clipping.

Use the `leveladj` tool to auto-adjust:

```bash
./leveladj           # For device 0
./leveladj -d 1      # For device 1
```

#### `center_offset` (0–255, default: 2) – DC offset adjustment

Manually adjust DC centering of the RF signal.

**Check centering:** If highest and lowest values are equidistant from 0 and 255, the signal is centered.

```bash
./leveladj
```

Example output:
```
low 121 high 133 clipped 0 nsamp 2097152
```

Analysis: `121 + 133 = 254` ≈ centered.

### Sampling Rate Control

#### `tenxfsc` (default: 0) – Sampling rate

Sets the ADC sampling rate based on crystal frequency.

**Stock 28.6 MHz crystal:**
- `0`: 28.6 MHz 8-bit (default)
- `1`: 35.8 MHz 8-bit (1.25× upsampled)
- `2`–`99`: Treated as MHz (e.g., `20` = 20 MSPS)
- Custom: Enter exact frequency (e.g., `14318181`)

**Constraints:**
- Minimum: `(HW crystal / 40) × 14`
- Maximum: `(HW crystal / 8) × 10`

For 40 MHz crystal:
- Minimum: ~14 MSPS
- Maximum: ~50 MSPS

> [!TIP]
> For 40 MHz 8-bit or 20 MHz 16-bit reliable operation, use [crystal replacement](https://github.com/happycube/cxadc-linux3/wiki/Crystal-Upgrades) or [clock gen modification](https://github.com/happycube/cxadc-linux3/wiki/Modifications).

#### `tenbit` (0 or 1, default: 0) – Sample bit depth

- `0`: 8-bit (8× Fsc, raw unsigned data)
- `1`: 16-bit (4× Fsc, filtered VBI data; downsampled 50%)

**Practical rates (stock 28.6 MHz crystal):**
- 8-bit: 28.6 MHz
- 16-bit: 14.3 MHz

#### `crystal` (default: 28636363) – Physical crystal frequency

Specifies the XTAL crystal frequency in Hz on your CX card. Used only to compute custom `tenxfsc` rates. Common values:
- Stock: 28636363 (28.6 MHz)
- Upgraded: 40000000 (40 MHz)
- High-end: 54000000 (54 MHz, requires cooling)

### Other Parameters

#### `latency` (0–255, default: 255)

PCI latency timer. Rarely needs adjustment; use default unless experiencing buffer underruns.


## Capture

### Signal Detection

Create a video preview to confirm signal reception. Depending on the RF signal type, expect either unstable video or a white flash.

**PAL (28.6 MHz, 8-bit):**

```bash
sudo ffplay -hide_banner -async 1 -f rawvideo -pixel_format gray8 \
  -video_size 1832x625 -i /dev/cxadc0 \
  -vf scale=1135x625,eq=gamma=0.5:contrast=1.5
```

**NTSC (28.6 MHz, 8-bit):**

```bash
sudo ffplay -hide_banner -async 1 -f rawvideo -pixel_format gray8 \
  -video_size 1820x525 -i /dev/cxadc0 \
  -vf scale=910x525,eq=gamma=0.5:contrast=1.5
```

### Gain Adjustment

#### Automatic Level Adjustment

```bash
./leveladj           # Device 0
./leveladj -d 1      # Device 1, etc.
```

#### Real-Time Level Monitoring

```bash
./levelmon -d cxadc0
```

Output is printed every 0.25 seconds. Example:

```
lo |0| [  3.906%] ( 33.429%) center -0.54% hi ( 65.764%) [ 95.312%] |0|  nsamp 10000000  rate 44.58
   ^    clipped%    neg avg           offset            pos avg    clipped%              MSPS
```

For device 1: `./levelmon -d 1`

#### Manual Fixed Gain

Set internal and digital gain without `sudo` (after adding user to `video` group):

```bash
# Internal gain (0–31)
echo 10 >/sys/class/cxadc/cxadc0/device/parameters/level

# Digital gain boost (1=on, 0=off)
echo 0 >/sys/class/cxadc/cxadc0/device/parameters/sixdb
```

### Raw Data Capture

#### Basic Capture (10 seconds)

```bash
timeout 10s cat /dev/cxadc0 | pv > capture.u8
```

Options:
- **Duration:** Replace `10s` with `5s`, `1m`, `2h`, etc.
- **Ctrl+C:** Stop capture manually
- **pv:** Shows data rate and elapsed time (optional; remove if not installed)
- **Extensions:** Use `.u8` (8-bit) or `.u16` (16-bit) for proper codec detection

#### Continuous Capture (until stopped)

```bash
cat /dev/cxadc0 | pv > capture.u8
```

#### Resample to Specific Rate

```bash
cat /dev/cxadc0 | sox -r 28636363 -t u8 -c 1 - -r 44100 -t u8 output.u8
```

### Real-Time FLAC Compression

> [!NOTE]
> Requires FLAC 1.5.0+ for real-time multi-threaded encoding
> 40–60% file size reduction vs. raw 8-bit or 16-bit

**8-bit (stock 28.6 MSPS):**

```bash
cat /dev/cxadc0 | flac --threads=64 -6 \
  --sample-rate=28636 --sign=unsigned --channels=1 \
  --endian=little --bps=8 --blocksize=65535 --lax -f - \
  -o capture-28msps-8bit.flac
```

**16-bit (stock 17.8 MSPS):**

```bash
cat /dev/cxadc0 | flac --threads=64 -6 \
  --sample-rate=17898 --sign=unsigned --channels=1 \
  --endian=little --bps=16 --blocksize=65535 --lax -f - \
  -o capture-17.8msps-16bit.flac
```

### Multi-Card Capture

For multiple cards, use `/dev/cxadc0`, `/dev/cxadc1`, etc., and adjust parameters per card:

```bash
# Configure card 1
echo 2 >/sys/class/cxadc/cxadc1/device/parameters/vmux
echo 20 >/sys/class/cxadc/cxadc1/device/parameters/level

# Capture from both simultaneously
cat /dev/cxadc0 | pv > card0.u8 &
cat /dev/cxadc1 | pv > card1.u8 &
wait
```

> [!NOTE]
> Each card has a separate set of entries in `/etc/udev/rules.d/cxadc.rules` for persistent settings.


## Troubleshooting

### Secure Boot Issues

> [!WARNING]
> Secure Boot is the **most common** cause of driver load failures, especially with PCIe devices.

**Check status:**
```bash
mokutil --sb-state
```

**Disable Secure Boot:**
- Reboot into BIOS/UEFI setup (usually F2, F10, or Del during boot)
- Find **Secure Boot** setting (usually under "Security" or "Boot")
- Set to **Disabled**
- Save and exit

### Kernel Updates Break Driver

> [!NOTE]
> If using **DKMS**, the driver automatically rebuilds on kernel update. Manual installs require reinstallation.

**If driver stops loading after kernel update:**

1. Check if DKMS is active:
   ```bash
   dkms status | grep cxadc
   ```

2. **If DKMS is installed:** Rebuild automatically or manually:
   ```bash
   sudo rsync -a --delete --exclude '.git' ./ /usr/src/cxadc-0.1/
   sudo dkms build -m cxadc -v 0.1
   sudo dkms install -m cxadc -v 0.1 --force
   sudo depmod -a
   ```

3. **If manual installation:** Reinstall the driver:
   ```bash
   cd cxadc
   make clean
   make
   sudo make modules_install
   sudo depmod -a
   ```

### `CONFIG_X86_X32` / Binutils Error

<details>
<summary><b>Expand: "CONFIG_X86_X32 enabled but no binutils support" error</b></summary>

This error appears during `make` or module signing and indicates a kernel/toolchain mismatch:

```
arch/x86/Makefile:142: CONFIG_X86_X32 enabled but no binutils support
  SIGN    /lib/modules/.../extra/cxadc.ko
  DEPMOD  ...
Warning: modules_install: missing 'System.map' file. Skipping depmod.
```

**Solution 1: Install binutils**

```bash
sudo apt install binutils  # Debian/Ubuntu
sudo dnf install binutils  # Fedora
```

**Solution 2: Use Xanmod kernel (verified working)**

Xanmod kernels have been tested as a reliable workaround:

```bash
# Fedora
sudo dnf copr enable rmnscnce/xanmod
sudo dnf install kernel-xanmod kernel-xanmod-devel

# Then reboot into Xanmod kernel and rebuild
```

The warning about `System.map` is non-fatal; the module still loads successfully.

</details>

### Device Node Not Appearing

If `/dev/cxadc0` doesn't exist after loading the driver:

1. **Verify the module loaded:**
   ```bash
   lsmod | grep cxadc
   ```

2. **Check kernel logs:**
   ```bash
   dmesg | grep cxadc
   ```

3. **Verify card is detected:**
   ```bash
   lspci | grep -i "cx"
   ```

4. **Reload udev rules:**
   ```bash
   sudo udevadm control --reload-rules
   sudo modprobe -r cxadc
   sudo modprobe cxadc
   ls -l /dev/cxadc*
   ```

### Cannot Read from Device

If you see "Permission denied" or cannot read `/dev/cxadc0`:

1. **Add user to `video` group:**
   ```bash
   sudo usermod -a -G video $USER
   ```

2. **Log out and back in**, then retry:
   ```bash
   cat /dev/cxadc0 | pv | head -c 100K > /dev/null
   ```

3. **Check device permissions:**
   ```bash
   ls -l /dev/cxadc*
   # Should show: crw-rw---- root video
   ```

### Configuration Persistence

> [!NOTE]
> Module parameters reset on reboot unless configured persistently.

**Method 1: Edit `/etc/modprobe.d/cxadc.conf`**

```bash
# Example persistent configuration
options cxadc vmux=2 level=16 sixdb=1 crystal=28636363
```

**Method 2: Device-specific settings via udev**

Edit `/etc/udev/rules.d/cxadc.rules` to apply settings per card on plugin. See file for examples.

**Method 3: Apply at runtime (resets on reboot)**

```bash
echo 2 >/sys/class/cxadc/cxadc0/device/parameters/vmux
echo 16 >/sys/class/cxadc/cxadc0/device/parameters/level
```


## History

Version history and development timeline:

### 2005-09-25 – v0.2

Originally written by **Hew How Chee** (<how_chee@yahoo.com>).
See [SDR using a CX2388x TV+FM card](http://web.archive.org/web/20091027150612/http://geocities.com/how_chee/cx23881fc6.htm) for original project.

- I2C support for tuning via `i2c.c`
- Optimized register settings for reduced gain and centered signal (≈128)
- Added `vmux` and `audsel` runtime parameters

### 2007-03-24 – v0.3

- Kernel 2.6.18 (Fedora Core 6) support
- Code cleanup and simplification

### 2013-12-18 – v0.4

Retargeted for **Ubuntu 13.10 / Linux 3.11** by [Chad Page](https://github.com/happycube/)

- Standard `read()` semantics for data capture (no external capture program needed)
- 64-bit Linux support
- SMP (multi-processor) compatibility improvements

### 2019-06-09 – v0.5

- **Linux 5.1+** compatibility
- Code style improvements per kernel `checkpatch` standards
- Optional `audsel` parameter
- Single-open enforcement (prevent multiple `/dev/cxadc` opens)
- AGC register reset on unload (enables cx88/cxadc switching without reboot)

### 2021-12-14 – Documentation Update

By [Harry Munday](https://github.com/harrypm)

- Corrected sample format documentation: 8-bit and 16-bit (not 10-bit)
- Module parameter examples and real-time monitoring
- `ABLS2-40.000MHZ-D4YF-T` crystal replacement documentation
- `sixdb` mode (gain boost) configuration guide
- Updated hardware purchasing links
- Crystal upgrade compatibility list

### 2022-01-21 – v0.6: Usability

By [Tony Anderson](https://github.com/tandersn)

- Fixed `leveladj` parameter reset bug
- Command-line utility scripts
- Additional level adjustment tools
- Dedicated utils README

### 2022-04-26 – Usability & Tools

- `cxlevel`: Real-time level monitoring
- `cxfreq`: Frequency control utility
- `cxvalues`: Display current driver configuration
- `fortycryst`: 40 MHz crystal mode control
- High/low gain warning messages
- Documentation cleanup

### 2023-01-12 – v0.7: Multi-Card Support

By [Adam R](https://github.com/AR1972)

- Support for up to 256 cards per system
- Per-card configuration (vmux, level, etc.)
- Updated scripts for multi-card operations

### 2024 – Clockgen Mod & Pi Support

[Clockgen Mod](https://github.com/happycube/cxadc-linux3/wiki/Modifications#clockgen-mod---external-clock) established

- Software-defined sample rates: 20/28.6/40/50 MSPS modes
- Shared synchronized clock for multi-card setups
- **Raspberry Pi 4 & 5 support** by [Alistair Buxton](https://github.com/ali1234)

### 2024-12-11

[Windows Port](https://github.com/JuniorIsAJitterbug/cxadc-win) established by JuniorIsAJitterbug

### 2025-02-11

[**FLAC 1.5.0 released**](https://github.com/xiph/flac/releases/tag/1.5.0)

- Multi-threaded FLAC encoding support
- Enables real-time compressed RF archival capture

### 2025-10-04 – Tools & Kernel Support

By [Ethan Halsall](https://github.com/eshaz)

- `levelmon`: Real-time level and clipping monitor
- **Linux 6.12+** compatibility
