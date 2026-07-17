# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
# Copyright (c) 2023 Rene Wolf

cmake_minimum_required(VERSION 3.5)

set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})

if (NOT PICO_SDK_PATH)
    if (EXISTS "${CMAKE_CURRENT_LIST_DIR}/pico-sdk/external/pico_sdk_import.cmake")
        set(PICO_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/pico-sdk")
    else()
        message(FATAL_ERROR "PICO_SDK_PATH is not set and local pico-sdk was not found. See clockgen/BUILDING.md for setup instructions.")
    endif()
endif()

# Apply local TinyUSB fix: the stock RP2040 dcd panics (bricking the device
# until power cycle) when a buffer-done IRQ races an ISO endpoint close during
# rapid SET_INTERFACE alt switches. Idempotent: skipped when already applied.
set(TINYUSB_RACE_PATCH "${CMAKE_CURRENT_LIST_DIR}/patches/0001-rp2040-usb-no-panic-on-iso-close-race.patch")
execute_process(
    COMMAND git -C "${PICO_SDK_PATH}/lib/tinyusb" apply --reverse --check "${TINYUSB_RACE_PATCH}"
    RESULT_VARIABLE TINYUSB_PATCH_ALREADY_APPLIED
    ERROR_QUIET OUTPUT_QUIET)
if (NOT TINYUSB_PATCH_ALREADY_APPLIED EQUAL 0)
    execute_process(
        COMMAND git -C "${PICO_SDK_PATH}/lib/tinyusb" apply "${TINYUSB_RACE_PATCH}"
        RESULT_VARIABLE TINYUSB_PATCH_RESULT)
    if (NOT TINYUSB_PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR "Failed to apply ${TINYUSB_RACE_PATCH} to ${PICO_SDK_PATH}/lib/tinyusb")
    endif()
    message(STATUS "Applied TinyUSB rp2040 ISO close-race patch")
endif()

# Pull in SDK (must be before project)
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(cxadc-clock-generator-audio-adc C CXX ASM)

# Initialize the SDK
pico_sdk_init()

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.3.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.3.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        -Wno-maybe-uninitialized
        -Werror=return-type
        )

add_subdirectory("src" "build")
