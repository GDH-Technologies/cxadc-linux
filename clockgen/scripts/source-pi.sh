# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2023 Rene Wolf

SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

if [[ "$DIR_FIRMWARE" == "" ]] ; then
	source "$SCRIPT_DIR/source-common.sh"
fi

function _pico_get_repo
{
	# adapted from https://raw.githubusercontent.com/raspberrypi/pico-setup/master/pico_setup.sh

	local REPO="$1"
	local BRANCH=$2
	local DEST="$3"

	if [[ "$BRANCH" == "" ]] ; then
		BRANCH=master
	fi

	if [[ "$DEST" == "" ]] ; then
		die "Need a destination"
	fi

	if [[ -d "$DEST" ]] ; then
		echo "$REPO '$DEST' already exists so skipping"
	else
		local REPO_URL="https://github.com/raspberrypi/${REPO}.git"
		echo "Cloning $REPO_URL into '$DEST'"
		git clone --depth 1 -b $BRANCH $REPO_URL "$DEST" || die "git clone failed"
	fi

	# Any submodules
	local owd="$(pwd)"
	cd "$DEST" || die "can't cd to '$DEST'"
	git submodule update --init --depth 1 || die "git submodule update failed"
	cd "$owd"
}

PICO_SDK_PATH="$DIR_FIRMWARE/pico-sdk"

function pico_get_sdk
{
	_pico_get_repo pico-sdk $1 "$PICO_SDK_PATH"
}

PICO_EXTRAS_PATH="$DIR_FIRMWARE/pico-extras"

function pico_get_extras
{
	_pico_get_repo pico-extras $1 "$PICO_EXTRAS_PATH"
}

PICO_EXAMPLES_PATH="$DIR_FIRMWARE/pico-examples"

function pico_get_examples
{
	_pico_get_repo pico-examples $1 "$PICO_EXAMPLES_PATH"
}

PICO_PLAYGROUND_PATH="$DIR_FIRMWARE/pico-playground"

function pico_get_playground
{
	_pico_get_repo pico-playground $1 "$PICO_PLAYGROUND_PATH"
}
