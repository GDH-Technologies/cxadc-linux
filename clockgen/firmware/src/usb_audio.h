// SPDX-License-Identifier: MIT
// Copyright (c) 2020 Reinhard Panhuber
// Copyright (c) 2023 Rene Wolf

#ifndef _USB_AUDIO_H
#define _USB_AUDIO_H

#include <stdint.h>

// Currently active alternate setting of the streaming interface.
// 0 = stream closed, 1 = 2 channel, 2 = 3 channel (with head switch).
extern volatile uint8_t usb_audio_active_alt;

// Channel count to interleave right now: 3 only while alt 2 is active, 2 otherwise (also while closed).
uint8_t usb_audio_active_channels();

#endif
