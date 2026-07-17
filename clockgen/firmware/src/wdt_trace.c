// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 GDH Technologies

#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include "wdt_trace.h"
#include "global_status.h"
#include "main1.h"
#include "dbg.h"

// Reboot if neither core showed life for this long
#define WDT_TIMEOUT_MS        3000
// How stale core1's heartbeat may get before main0 stops petting the watchdog
#define CORE1_STALE_LIMIT_US  (2000 * 1000)

static uint32_t last_heartbeat_value;
static uint32_t last_heartbeat_change_us;

void wdt_trace_init()
{
	if( watchdog_caused_reboot() )
	{
		uint8_t t0 = (uint8_t)watchdog_hw->scratch[0];
		uint8_t t1 = (uint8_t)watchdog_hw->scratch[1];

		global_status_access(
		{
			global_status.wdt_rebooted = true_u8;
			global_status.wdt_trace_core0 = t0;
			global_status.wdt_trace_core1 = t1;
		});

		dbg_say("!! watchdog reboot, trace core0=");
		dbg_u8(t0);
		dbg_say(" core1=");
		dbg_u8(t1);
		dbg_say("\n");
	}

	wdt_trace_core0(WDT_TRACE0_IDLE);
	wdt_trace_core1(WDT_TRACE1_IDLE);

	last_heartbeat_value = main1_heartbeat;
	last_heartbeat_change_us = time_us_32();

	watchdog_enable(WDT_TIMEOUT_MS, true);
}

void wdt_trace_task()
{
	uint32_t now = time_us_32();
	uint32_t beat = main1_heartbeat;

	if( beat != last_heartbeat_value )
	{
		last_heartbeat_value = beat;
		last_heartbeat_change_us = now;
	}

	// main0 reaching this point proves core0 is alive; only pet the watchdog
	// while core1 is alive too, so a core1 hang also ends in a clean reboot
	if( (now - last_heartbeat_change_us) < CORE1_STALE_LIMIT_US )
	{
		watchdog_update();
	}
}
