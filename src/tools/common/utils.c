#include "utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

#define SYSFS_PARAM_FMT "/sys/class/cxadc/%s/device/parameters/%s"

int cxadc_valid_device_name(const char *device)
{
	if (device == NULL)
		return 0;

	size_t len = strlen(device);
	if (len == 0 || len > 31)
		return 0;

	for (size_t i = 0; i < len; i++) {
		char ch = device[i];
		if (!(isalnum((unsigned char)ch) || ch == '_' || ch == '-'))
			return 0;
	}
	return 1;
}

int set_cxadc_param(const char *param_name, const char *device, int param_value)
{
	char path[256];
	char value[32];
	int fd;
	int n;

	if (!cxadc_valid_device_name(device)) {
		fprintf(stderr, "invalid cxadc device name: %s\n",
			device ? device : "(null)");
		return -1;
	}

	n = snprintf(path, sizeof(path), SYSFS_PARAM_FMT, device, param_name);
	if (n < 0 || (size_t)n >= sizeof(path)) {
		fprintf(stderr, "sysfs path too long for %s/%s\n", device, param_name);
		return -1;
	}

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		fprintf(stderr, "failed to open %s: %s\n", path, strerror(errno));
		return -1;
	}

	n = snprintf(value, sizeof(value), "%d\n", param_value);
	if (n < 0 || (size_t)n >= sizeof(value)) {
		close(fd);
		return -1;
	}

	if (write(fd, value, (size_t)n) != n) {
		fprintf(stderr, "failed to set parameter %s: %s\n", path, strerror(errno));
		close(fd);
		return -1;
	}

	close(fd);
	return 0;
}

int read_cxadc_param(const char *param_name, const char *device, int *param_value)
{
	char path[256];
	FILE *sysfs;
	int n;

	if (!cxadc_valid_device_name(device)) {
		fprintf(stderr, "invalid cxadc device name: %s\n",
			device ? device : "(null)");
		return -1;
	}

	n = snprintf(path, sizeof(path), SYSFS_PARAM_FMT, device, param_name);
	if (n < 0 || (size_t)n >= sizeof(path)) {
		fprintf(stderr, "sysfs path too long for %s/%s\n", device, param_name);
		return -1;
	}

	sysfs = fopen(path, "r");
	if (sysfs == NULL) {
		fprintf(stderr, "failed to open %s: %s\n", path, strerror(errno));
		return -1;
	}

	if (fscanf(sysfs, "%d", param_value) != 1) {
		fprintf(stderr, "failed to read %s\n", param_name);
		fclose(sysfs);
		return -1;
	}

	fclose(sysfs);
	return 0;
}
