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

Option 1 (manual clone):

```bash
cd clockgen/firmware
git clone --depth 1 https://github.com/raspberrypi/pico-sdk.git
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
export PICO_SDK_PATH="$(pwd)/pico-sdk"
cmake -S . -B build
cmake --build build --parallel
```

Expected artifact:
- firmware UF2 at firmware/build/build/firmware.uf2

## Flash to Raspberry Pi Pico

1. Hold BOOTSEL on the Pico.
2. Connect USB.
3. Release BOOTSEL when the RPI-RP2 mass-storage device appears.
4. Copy firmware/build/firmware.uf2 onto the mounted device.
	In the current layout, this file is generated as firmware/build/build/firmware.uf2.

## Validate Integration Quickly

```bash
cd clockgen/scripts/ci
bash ./ci-build.sh
```

If successful, the script writes logs and build outputs under clockgen/firmware/build.

## Links

- Clockgen upstream repository: https://github.com/GDH-Technologies/cx-clockgen
- Upstream hardware/mechanical content: https://github.com/GDH-Technologies/cx-clockgen/tree/main/mechanical
