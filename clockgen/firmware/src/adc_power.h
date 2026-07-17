// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 GDH Technologies

#ifndef _ADC_POWER_H
#define _ADC_POWER_H

#include <stdbool.h>

// Desired power state of the PCM1802 capture path. core0 (USB callbacks) sets the
// desired state and gates the ADC master clock; core1 owns the PDWN pin and the PIO
// state machine and follows the request from its fill loop (see main1.c).

// Request the ADC path on or off. Safe to call redundantly. core0 only (does I2C).
void adc_power_set(bool on);

// Desired state, polled by core1
bool adc_power_requested();

// USB bus suspend/resume: powers down, remembering whether a stream was live,
// and restores that state on resume. core0 only.
void adc_power_suspend();
void adc_power_resume();

#endif
