// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 GDH Technologies

#include "adc_power.h"
#include "clock_gen.h"
#include "dbg.h"

static volatile bool run_request = false;
static bool restore_on_resume = false;

void adc_power_set(bool on)
{
	if( on )
	{
		// clock first, so the PCM1802 sees a stable SCKI by the time core1 releases PDWN
		clock_gen_adc_clock_enable(true);
		run_request = true;
	}
	else
	{
		run_request = false;
		// core1 pulls PDWN low on its next loop iteration; gating SCKI right away is
		// harmless, the PIO just stops seeing edges
		clock_gen_adc_clock_enable(false);
	}
}

bool adc_power_requested()
{
	return run_request;
}

void adc_power_suspend()
{
	restore_on_resume = run_request;
	adc_power_set(false);
	dbg_say("adc suspend\n");
}

void adc_power_resume()
{
	if( restore_on_resume )
		adc_power_set(true);
	restore_on_resume = false;
	dbg_say("adc resume\n");
}
