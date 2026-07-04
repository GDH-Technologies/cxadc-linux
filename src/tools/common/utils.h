#ifndef CXADC_COMMON_UTILS_H
#define CXADC_COMMON_UTILS_H

#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

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
 * Validate a cxadc device name: must be non-empty, <= 31 chars and contain
 * only [A-Za-z0-9_-] so it cannot be used for path traversal. Returns 1 if the
 * name is safe, 0 otherwise.
 */
int cxadc_valid_device_name(const char *device);

#endif /* CXADC_COMMON_UTILS_H */
