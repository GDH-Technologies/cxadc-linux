#!/bin/bash

# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2023 Rene Wolf

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"
CLOCKGEN_SCRIPTS_DIR="$( realpath "$SCRIPT_DIR/.." )"

source "$CLOCKGEN_SCRIPTS_DIR/source-common.sh"
source "$CLOCKGEN_SCRIPTS_DIR/source-pi.sh"

cd "$DIR_FIRMWARE" || die "can't cd to '$DIR_FIRMWARE'"

output_name="cxadc-clock-generator-audio-adc-firmware-${SEMVER}"
output_dir="$DIR_FIRMWARE/$output_name"
mkdir -p "$output_dir"


cmake_exe_name=firmware
uf2_file="$DIR_FIRMWARE_BUILD/${cmake_exe_name}.uf2"
if [[ ! -f "$uf2_file" ]] ; then
	uf2_file="$DIR_FIRMWARE_BUILD/build/${cmake_exe_name}.uf2"
fi
file_or_die "$uf2_file"
mv_or_die "$uf2_file" "$output_dir/firmware-${SEMVER}.uf2"
cp_or_die "$FILE_BUILD_INFO_TXT" "$output_dir/"
cp_or_die "$FILE_BUILD_LOG" "$output_dir/"
cp_or_die "$DIR_FIRMWARE/LICENSE" "$output_dir/LICENSE-firmware.txt"
cp_or_die "$DIR_FIRMWARE/src/libsi5351/LICENSE" "$output_dir/LICENSE-libsi5351.txt"
cp_or_die "$DIR_FIRMWARE/src/libsi5351/README.md" "$output_dir/NOTE-libsi5351.md"
cp_or_die "$DIR_FIRMWARE/pico-sdk/LICENSE.TXT" "$output_dir/LICENSE-pico-sdk.txt"
cp_or_die "$DIR_FIRMWARE/pico-sdk/lib/tinyusb/LICENSE" "$output_dir/LICENSE-tinyusb.txt"

zip -r "${output_name}.zip" "$output_name"
