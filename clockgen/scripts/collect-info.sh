#!/bin/bash

# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2023 Rene Wolf

# the alsa device we expect the clock gen to be
alsadevice="${CLOCK_GEN_ALSA_DEVICE:-hw:CARD=CXADCADCClockGe}"

tmp="$(mktemp -d /var/tmp/cx-clockgen-info.XXXXXX)"


function debug_mute_switch
{
	amixer -D "$alsadevice" cset name='Audio Control Capture Switch' "$1"
}

function short_record
{
	local start=$(date +%s)
	arecord -D "$alsadevice" -c 3 -r 46875 -f S24_3LE --samples=1000 "$1"
	local e=$?
	local end=$(date +%s)
	echo "exit $e, time $(( $end - $start )) sec" > "${1}.nfo"
}

function capture_cmd
{
	local output="$1"
	shift

	"$@" > "$output" 2>&1
	local e=$?
	echo "exit $e" > "${output}.nfo"
}

function capture_sudo_cmd
{
	local output="$1"
	shift

	sudo "$@" > "$output" 2>&1
	local e=$?
	echo "exit $e" > "${output}.nfo"
}

function capture_optional_file
{
	local source="$1"
	local output="$2"

	if [[ -f "$source" ]] ; then
		capture_sudo_cmd "$output" cat "$source"
	else
		echo "$source does not exist on this system" > "$output"
		echo "exit 0" > "${output}.nfo"
	fi
}

# try a 1000 sample test capture on actual adc data, to see what happens
debug_mute_switch on
short_record $tmp/test.wav

# switch to debug output and get 1000
debug_mute_switch off
short_record $tmp/debug.wav

# switch back to adc data
debug_mute_switch on

# The next few commands collect some general system information that needs elevated privileges
# They mostly aim to identify troubles with usb connections or other such communication problems
capture_sudo_cmd "$tmp/dmesg.txt" dmesg
capture_optional_file /var/log/syslog "$tmp/syslog"
capture_optional_file /var/log/kern.log "$tmp/kern.log"
if command -v journalctl > /dev/null 2>&1 ; then
	capture_sudo_cmd "$tmp/journal-boot.txt" journalctl -b -o short-iso
	capture_sudo_cmd "$tmp/journal-kernel.txt" journalctl -k -b -o short-iso
fi
# This lsusb gives us the usb device details including resolved string descriptors.
# It will contain the build version of the clock gen firmware and also give some insight into some cases of usb troubles
capture_sudo_cmd "$tmp/lsusb" lsusb -v

# Now we check if arecord can see the device, once as root, and once without
# If root works but normal user don't then this may be a permissions problem
capture_sudo_cmd "$tmp/arecord-devices-sudo" arecord -L
capture_cmd "$tmp/arecord-devices-nosudo" arecord -L

# The remaining commands collect some more info that do not require elevated privileges
capture_cmd "$tmp/lsusb-tree" lsusb -t # a nice tree view of your usb
capture_cmd "$tmp/uname" uname -a # what kernel / arch you are running
capture_cmd "$tmp/os-release" cat /etc/os-release # distro version on Fedora and most modern linux systems
if command -v lsb_release > /dev/null 2>&1 ; then
	capture_cmd "$tmp/lsb-release" lsb_release -a
fi
capture_cmd "$tmp/arecord-version" arecord --version # check the version of arecord
capture_cmd "$tmp/groups" groups # see what groups you are in, this also controls access to sound devices (on some distros)
capture_cmd "$tmp/amixer" amixer -D "$alsadevice" # check if amixer can see the device controls 


# this just packs all the files into a tar.gz in your current working dir and removes the temp dir again
tgz="info-$(date +%Y%m%d-%H%M%S).tar.gz"
tar -C "$tmp" -cv . | gzip -9 > "$tgz"
rm -rf "$tmp"

echo "Created $(realpath "$tgz")"
