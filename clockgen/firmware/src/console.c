// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 GDH Technologies

#include <stdio.h>
#include <string.h>

#include "tusb.h"
#include "console.h"
#include "build_info.h"
#include "clock_gen.h"
#include "adc_power.h"
#include "usb_audio.h"
#include "global_status.h"

#define CONSOLE_LINE_MAX 80

static char line_buffer[CONSOLE_LINE_MAX];
static uint32_t line_len = 0;

static const char* cxadc_clock_labels[] =
{
	CLOCK_GEN_CXADC_CLOCK_F0_STR,
	CLOCK_GEN_CXADC_CLOCK_F1_STR,
	CLOCK_GEN_CXADC_CLOCK_F2_STR,
	CLOCK_GEN_CXADC_CLOCK_F3_STR,
};

static void put_str(const char* s)
{
	tud_cdc_write_str(s);
}

static void put_line(const char* s)
{
	tud_cdc_write_str(s);
	tud_cdc_write_str("\r\n");
}

static void put_kv_u32(const char* key, uint32_t value)
{
	char tmp[48];
	snprintf(tmp, sizeof(tmp), "%s=%lu\r\n", key, (unsigned long)value);
	put_str(tmp);
}

static void cmd_help()
{
	put_line("commands:");
	put_line("  help     this text");
	put_line("  version  firmware version and build");
	put_line("  clocks   clock generator configuration");
	put_line("  power    ADC power / stream state");
	put_line("  status   global status counters");
}

static void cmd_version()
{
	put_line("CXADC+ADC-ClockGen");
	put_line("version " NFO_SEMVER_STR);
	put_line("build " NFO_GIT_SHA);
}

static void cmd_clocks()
{
	char tmp[64];

	for(uint8_t output = 0; output < 2; ++output)
	{
		uint8_t option = clock_gen_get_cxadc_sample_rate(output);
		const char* label = (option < 4) ? cxadc_clock_labels[option] : "?";
		snprintf(tmp, sizeof(tmp), "cxadc_clk%u=%s\r\n", output, label);
		put_str(tmp);
	}

	snprintf(tmp, sizeof(tmp), "adc_sample_rate=%lu\r\n", (unsigned long)clock_gen_get_adc_sample_rate());
	put_str(tmp);
	snprintf(tmp, sizeof(tmp), "adc_master_clock=%s\r\n", adc_power_requested() ? "on" : "off");
	put_str(tmp);
}

static void cmd_power()
{
	global_status_fields snapshot;
	global_status_access( snapshot = global_status );

	char tmp[48];
	snprintf(tmp, sizeof(tmp), "adc_powered=%u\r\n", snapshot.adc_powered);
	put_str(tmp);
	snprintf(tmp, sizeof(tmp), "usb_alt_setting=%u\r\n", snapshot.usb_alt_setting);
	put_str(tmp);
	snprintf(tmp, sizeof(tmp), "channels=%u\r\n", usb_audio_active_channels());
	put_str(tmp);
	snprintf(tmp, sizeof(tmp), "adc_power_cycles=%lu\r\n", (unsigned long)snapshot.adc_power_cycles);
	put_str(tmp);
}

static void cmd_status()
{
	global_status_fields snapshot;
	global_status_access( snapshot = global_status );

	put_kv_u32("si5351_init_success", snapshot.si5351_init_success);
	put_kv_u32("pcm1802_activity_lrck", snapshot.pcm1802_activity_lrck);
	put_kv_u32("pcm1802_activity_bck", snapshot.pcm1802_activity_bck);
	put_kv_u32("pcm1802_activity_data", snapshot.pcm1802_activity_data);
	put_kv_u32("pcm1802_out_of_sync_drops", snapshot.pcm1802_out_of_sync_drops);
	put_kv_u32("pcm1802_overrun_count", snapshot.pcm1802_rch_tmo_count);
	put_kv_u32("pcm1802_overrun_lag", snapshot.pcm1802_rch_tmo_value);
	put_kv_u32("main1_rxsample_tmo", snapshot.main1_rxsample_tmo);
	put_kv_u32("adc_powered", snapshot.adc_powered);
	put_kv_u32("usb_alt_setting", snapshot.usb_alt_setting);
	put_kv_u32("adc_power_cycles", snapshot.adc_power_cycles);
}

static void dispatch_line(const char* line)
{
	if( strcmp(line, "help") == 0 )
		cmd_help();
	else if( strcmp(line, "version") == 0 )
		cmd_version();
	else if( strcmp(line, "clocks") == 0 )
		cmd_clocks();
	else if( strcmp(line, "power") == 0 )
		cmd_power();
	else if( strcmp(line, "status") == 0 )
		cmd_status();
	else if( line[0] != 0 )
		put_line("?"); // unknown input (tolerates e.g. ModemManager AT probes)

	tud_cdc_write_flush();
}

void console_task()
{
	if( ! tud_cdc_available() )
		return;

	uint8_t chunk[64];
	uint32_t count = tud_cdc_read(chunk, sizeof(chunk));

	for(uint32_t i = 0; i < count; ++i)
	{
		char c = (char)chunk[i];

		if( c == '\r' || c == '\n' )
		{
			line_buffer[line_len] = 0;
			line_len = 0;
			dispatch_line(line_buffer);
			continue;
		}

		if( line_len < (CONSOLE_LINE_MAX - 1) )
			line_buffer[line_len++] = c;
		// overlong lines are silently truncated, dispatch happens on the line end either way
	}
}
