#ifndef CXADC_COMMON_UTILS_H
#define CXADC_COMMON_UTILS_H

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>

/*
 * Shared sysfs helpers for cxadc userland tools.
 *
 * "device" is the class device name, e.g. "cxadc0" (no /dev prefix).
 * Parameters live under /sys/class/cxadc/<device>/device/parameters/<name>.
 * All helpers return 0 on success and -1 on error (message printed to stderr).
 */

/* Write an integer value to a cxadc sysfs parameter. */
int set_cxadc_param(const char *param_name, const char *device, int param_value);

/* Read an integer value from a cxadc sysfs parameter. */
int read_cxadc_param(const char *param_name, const char *device, int *param_value);

/*
 * Resolve user-provided device input to a canonical cxadc class name plus an
 * openable device node path.
 *
 * Accepted input forms:
 * - class name: cxadc0
 * - numeric index: 0 (maps to cxadc0)
 * - absolute device path: /dev/cxadc0 or /dev/cx/vcr0-video
 * - bare alias under /dev/cx: vcr0-video
 */
int cxadc_resolve_device(
	const char *input,
	char *canonical_device,
	size_t canonical_device_len,
	char *device_path,
	size_t device_path_len);

/*
 * Validate a cxadc device name: must be non-empty, <= 31 chars and contain
 * only [A-Za-z0-9_-] so it cannot be used for path traversal. Returns 1 if the
 * name is safe, 0 otherwise.
 */
int cxadc_valid_device_name(const char *device);

#endif /* CXADC_COMMON_UTILS_H */
