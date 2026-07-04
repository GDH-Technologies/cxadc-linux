#ifndef CXADC_COMMON_CLOCKGEN_H
#define CXADC_COMMON_CLOCKGEN_H

#include <stddef.h>

/*
 * Helpers for the CXADC clock generator (an RP2040 USB-audio device that
 * exposes clock selection through ALSA mixer controls, ALSA card name
 * "CXADCADCClockGe").
 *
 * Frequencies are the mixer enum labels: "20MHz", "28.63MHz", "40MHz", "50MHz".
 */

/*
 * Detect the clockgen ALSA card by scanning /proc/asound/cards. On success
 * writes an ALSA device string usable with amixer -D (e.g.
 * "hw:CARD=CXADCADCClockGe") into "device" and returns 0. Returns -1 if no
 * clockgen card is present.
 */
int cx_clockgen_detect(char *device, size_t device_sz);

/*
 * Set clock output "out_number" (0 or 1) to "freq_label" on the given ALSA
 * device via amixer. Arguments are passed to amixer with execvp (no shell), so
 * they are not subject to shell injection. Returns 0 on success, -1 on error.
 */
int cx_clockgen_set_clock(const char *device, int out_number,
			  const char *freq_label);

#endif /* CXADC_COMMON_CLOCKGEN_H */
