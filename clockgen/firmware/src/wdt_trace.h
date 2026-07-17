// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 GDH Technologies

#ifndef _WDT_TRACE_H
#define _WDT_TRACE_H

#include <stdint.h>
#include "hardware/structs/watchdog.h"

// Hardware watchdog + crash breadcrumbs.
//
// main0 pets the watchdog only while core1's heartbeat is advancing, so a hang
// on either core reboots the device within a few seconds instead of leaving it
// wedged until someone physically replugs it. Each core drops a one-byte trace
// marker into a watchdog scratch register before/after operations that could
// block; the scratch registers survive a watchdog reset, so after the reboot
// the last markers pinpoint where each core was. Exposed via global_status
// (wdt_* fields) and the console 'status' command.

// core0 trace points
#define WDT_TRACE0_IDLE          0x00
// loop-position markers: written every main0 iteration, so a stale
// non-marker value means core0 died inside the op that value names,
// while a marker value means it died in the phase FOLLOWING the marker
#define WDT_TRACE0_LOOP_TOP      0x01  // died inside tud_task
#define WDT_TRACE0_AFTER_TUD     0x02  // died inside console_task
#define WDT_TRACE0_SET_ITF       0x10
#define WDT_TRACE0_SET_ITF_DONE  0x11
#define WDT_TRACE0_CLOSE_EP      0x20
#define WDT_TRACE0_CLOSE_EP_DONE 0x21
#define WDT_TRACE0_ADC_CLK_I2C   0x30
#define WDT_TRACE0_ADC_CLK_DONE  0x31
#define WDT_TRACE0_RATE_I2C      0x50
#define WDT_TRACE0_RATE_I2C_DONE 0x51
#define WDT_TRACE0_TX_DONE       0x90  // died inside tud_audio_tx_done_pre_load_cb
#define WDT_TRACE0_TX_DONE_DONE  0x91

// core1 trace points
#define WDT_TRACE1_IDLE          0x00
#define WDT_TRACE1_LOOP_TOP      0x05  // died between loop top and the next traced op
#define WDT_TRACE1_PARKED        0x06  // died inside the parked sleep branch
#define WDT_TRACE1_TAKE_EMPTY    0x07  // died taking/waiting for an empty buffer
#define WDT_TRACE1_PCM_START     0x60
#define WDT_TRACE1_PCM_START_DONE 0x61
#define WDT_TRACE1_PCM_STOP      0x70
#define WDT_TRACE1_DMA_ABORT     0x72
#define WDT_TRACE1_PCM_STOP_DONE 0x71
#define WDT_TRACE1_FILLING       0x80
#define WDT_TRACE1_FILL_DONE     0x81
#define WDT_TRACE1_STATUS_LOCK   0x82  // died inside a global_status critical section

static inline void wdt_trace_core0(uint8_t point) { watchdog_hw->scratch[0] = point; }
static inline void wdt_trace_core1(uint8_t point) { watchdog_hw->scratch[1] = point; }

// Reads any breadcrumbs from a previous watchdog reboot into global_status,
// clears them and arms the watchdog. Call once from main0 after global_status_init().
void wdt_trace_init();

// Pets the watchdog if core1's heartbeat is still advancing. Call from the main0 loop.
void wdt_trace_task();

#endif
