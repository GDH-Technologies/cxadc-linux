# Clockgen Firmware Build and Flashing

This directory contains the clock generator firmware and helper scripts used by synchronized multi-card CXADC capture setups.

## Scope

Included in this repository:
- RP2040/Pico firmware source in firmware/
- capture and helper scripts in scripts/

Not included here:
- Hardware design files and mechanical parts from cx-clockgen upstream (see Links section)

## Prerequisites

Install required build tools.

Fedora:

```bash
sudo dnf install -y git cmake make gcc gcc-c++ arm-none-eabi-binutils arm-none-eabi-gcc-cs arm-none-eabi-gcc-cs-c++ arm-none-eabi-newlib
```

Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y git cmake make gcc g++ gcc-arm-none-eabi
```

## Fetch Pico SDK

The firmware builds against Pico SDK **1.5.1** (pin the tag — the SDK's
default branch is 2.x and is not compatible) and needs the SDK's TinyUSB
submodule.

Option 1 (manual clone):

```bash
cd clockgen/firmware
git clone --depth 1 -b 1.5.1 https://github.com/raspberrypi/pico-sdk.git
git -C pico-sdk submodule update --init --depth 1 lib/tinyusb
```

Option 2 (helper script):

```bash
cd clockgen/scripts
source ./source-common.sh
source ./source-pi.sh
pico_get_sdk 1.5.1
```

## Build Firmware

Use an out-of-source build directory.

```bash
cd clockgen/firmware
export PICO_SDK_PATH="$(pwd)/pico-sdk"   # optional when the SDK lives at firmware/pico-sdk
cmake -S . -B build
cmake --build build --parallel
```

Expected artifact:
- firmware UF2 at firmware/build/build/firmware.uf2

Notes:
- At configure time, CMake applies the fixes in firmware/patches/ to the SDK
  checkout (idempotent). This requires the SDK to be a git checkout, which
  both fetch options above produce. The patches fix device-bricking bugs in
  the SDK's RP2040 USB driver and make the bundled pioasm configure under
  CMake >= 4.
- Older checkouts of this repo (before firmware/patches/ existed) fail to
  configure with CMake >= 4 ("Compatibility with CMake < 3.5 has been
  removed" from the pioasm sub-build). Workaround there:
  `export CMAKE_POLICY_VERSION_MINIMUM=3.5` before building.

## Flash to Raspberry Pi Pico

First flash (blank Pico, or firmware without the serial console):

1. Hold BOOTSEL on the Pico.
2. Connect USB.
3. Release BOOTSEL when the RPI-RP2 mass-storage device appears.
4. Copy firmware/build/build/firmware.uf2 onto the mounted device.

Reflash (firmware with the serial console, no button needed): send
`bootsel` to the device's CDC serial console (`/dev/ttyACM*`) — it reboots
into the RPI-RP2 mass-storage bootloader — then copy the UF2 as above.

## Validate Integration Quickly

```bash
cd clockgen/scripts/ci
bash ./ci-build.sh
```

If successful, the script writes logs and build outputs under clockgen/firmware/build.

## Links

- Clockgen upstream repository: https://github.com/GDH-Technologies/cx-clockgen
- Upstream hardware/mechanical content: https://github.com/GDH-Technologies/cx-clockgen/tree/main/mechanical
