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

# Local patches applied to the pico-sdk checkout (idempotent, skipped when
# already applied). See patches/ for what each one fixes and why.
function(apply_local_patch target_repo patch_file)
    execute_process(
        COMMAND git -C "${target_repo}" apply --reverse --check "${patch_file}"
        RESULT_VARIABLE PATCH_ALREADY_APPLIED
        ERROR_QUIET OUTPUT_QUIET)
    if (NOT PATCH_ALREADY_APPLIED EQUAL 0)
        execute_process(
            COMMAND git -C "${target_repo}" apply "${patch_file}"
            RESULT_VARIABLE PATCH_RESULT)
        if (NOT PATCH_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to apply ${patch_file} to ${target_repo}")
        endif()
        message(STATUS "Applied ${patch_file}")
    endif()
endfunction()

# TinyUSB rp2040 dcd: DPRAM allocation caching + no device-bricking panics
apply_local_patch("${PICO_SDK_PATH}/lib/tinyusb"
    "${CMAKE_CURRENT_LIST_DIR}/patches/0001-rp2040-usb-no-panic-on-iso-close-race.patch")
# pioasm declares cmake_minimum_required 3.4, which CMake >= 4 refuses to configure
apply_local_patch("${PICO_SDK_PATH}"
    "${CMAKE_CURRENT_LIST_DIR}/patches/0002-pico-sdk-pioasm-cmake4-compat.patch")

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
