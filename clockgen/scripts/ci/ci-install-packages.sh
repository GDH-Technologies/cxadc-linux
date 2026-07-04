#!/bin/bash

# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2023 Rene Wolf

function die
{
	echo "$@" >&2
	exit 1
}

function run_privileged
{
	if [[ "$(id -u)" == 0 ]] ; then
		"$@"
	else
		sudo "$@"
	fi
}

function install_debian
{
	export DEBIAN_FRONTEND=noninteractive

	run_privileged apt update
	run_privileged apt install -y \
		git \
		cmake \
		make \
		gcc-arm-none-eabi \
		gcc \
		g++ \
		python3 \
		zip \
		alsa-utils \
		usbutils \
		sox \
		pv

	# maybe a bit overkill but we also directly cleanup some unneeded things to not waste space on ci runners
	run_privileged apt clean -y
	# https://github.com/tianon/docker-brew-ubuntu-core/blob/f2f3f01ed67bab2e24b8c4fda60ef035a871b4c7/xenial/Dockerfile
	run_privileged rm -rf /var/lib/apt/lists/*
}

function install_fedora
{
	local dnf_cmd=dnf
	if command -v dnf5 > /dev/null 2>&1 ; then
		dnf_cmd=dnf5
	elif ! command -v dnf > /dev/null 2>&1 ; then
		die "Neither dnf5 nor dnf was found"
	fi

	run_privileged "$dnf_cmd" install -y \
		git \
		cmake \
		make \
		gcc \
		gcc-c++ \
		python3 \
		zip \
		arm-none-eabi-binutils \
		arm-none-eabi-gcc-cs \
		arm-none-eabi-gcc-cs-c++ \
		arm-none-eabi-newlib \
		alsa-utils \
		usbutils \
		sox \
		pv
}

if [[ -f /etc/fedora-release ]] || command -v dnf5 > /dev/null 2>&1 || command -v dnf > /dev/null 2>&1 ; then
	install_fedora
elif command -v apt > /dev/null 2>&1 ; then
	install_debian
else
	die "Unsupported package manager. Install git, cmake, make, ARM none-eabi GCC/G++, python3, zip, alsa-utils, usbutils, sox, and pv."
fi

