#include "utils.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

#define SYSFS_PARAM_FMT "/sys/class/cxadc/%s/device/parameters/%s"

static int all_digits(const char *s)
{
	if (s == NULL || *s == '\0')
		return 0;

	for (const char *p = s; *p; p++) {
		if (!isdigit((unsigned char)*p))
			return 0;
	}
	return 1;
}

static int copy_string(char *dst, size_t dst_len, const char *src)
{
	if (dst == NULL || src == NULL || dst_len == 0)
		return -1;

	if (snprintf(dst, dst_len, "%s", src) >= (int)dst_len)
		return -1;

	return 0;
}

static int read_device_numbers(const char *path, unsigned int *maj, unsigned int *min)
{
	FILE *f = fopen(path, "r");
	if (f == NULL)
		return -1;

	unsigned int major_num = 0;
	unsigned int minor_num = 0;
	int ok = fscanf(f, "%u:%u", &major_num, &minor_num);
	fclose(f);
	if (ok != 2)
		return -1;

	*maj = major_num;
	*min = minor_num;
	return 0;
}

static int find_cxadc_name_by_rdev(dev_t rdev, char *canonical, size_t canonical_len)
{
	DIR *dir = opendir("/sys/class/cxadc");
	if (dir == NULL)
		return -1;

	unsigned int dev_major = major(rdev);
	unsigned int dev_minor = minor(rdev);
	int found = -1;

	struct dirent *entry;
	while ((entry = readdir(dir)) != NULL) {
		if (entry->d_name[0] == '.')
			continue;

		char dev_path[PATH_MAX];
		if (snprintf(dev_path, sizeof(dev_path), "/sys/class/cxadc/%s/dev",
			    entry->d_name) >= (int)sizeof(dev_path)) {
			continue;
		}

		unsigned int maj = 0;
		unsigned int min = 0;
		if (read_device_numbers(dev_path, &maj, &min) != 0)
			continue;

		if (maj == dev_major && min == dev_minor) {
			if (copy_string(canonical, canonical_len, entry->d_name) == 0)
				found = 0;
			break;
		}
	}

	closedir(dir);
	return found;
}

static int try_candidate(const char *candidate,
	char *canonical_device,
	size_t canonical_device_len,
	char *device_path,
	size_t device_path_len)
{
	struct stat st;
	if (stat(candidate, &st) != 0)
		return -1;

	if (!S_ISCHR(st.st_mode))
		return -1;

	char resolved[PATH_MAX];
	const char *final_path = candidate;
	if (realpath(candidate, resolved) != NULL)
		final_path = resolved;

	if (find_cxadc_name_by_rdev(st.st_rdev, canonical_device,
	    canonical_device_len) != 0) {
		return -1;
	}

	return copy_string(device_path, device_path_len, final_path);
}

int cxadc_resolve_device(
	const char *input,
	char *canonical_device,
	size_t canonical_device_len,
	char *device_path,
	size_t device_path_len)
{
	if (input == NULL || *input == '\0') {
		fprintf(stderr, "device input must not be empty\n");
		return -1;
	}

	char candidate[PATH_MAX];

	/* Numeric shorthand: 0 -> /dev/cxadc0 */
	if (all_digits(input)) {
		if (snprintf(candidate, sizeof(candidate), "/dev/cxadc%s", input) <
		    (int)sizeof(candidate) &&
		    try_candidate(candidate, canonical_device, canonical_device_len,
			device_path, device_path_len) == 0) {
			return 0;
		}
	}

	/* Absolute device path, e.g. /dev/cxadc0 or /dev/cx/vcr0-video */
	if (strncmp(input, "/dev/", 5) == 0 &&
	    try_candidate(input, canonical_device, canonical_device_len,
		device_path, device_path_len) == 0) {
		return 0;
	}

	/* Direct class device name, e.g. cxadc0 */
	if (cxadc_valid_device_name(input) &&
	    snprintf(candidate, sizeof(candidate), "/dev/%s", input) <
		(int)sizeof(candidate) &&
	    try_candidate(candidate, canonical_device, canonical_device_len,
		device_path, device_path_len) == 0) {
		return 0;
	}

	/* Relative /dev fragment, e.g. cx/vcr0-video */
	if (input[0] != '/' && strchr(input, '/') != NULL &&
	    snprintf(candidate, sizeof(candidate), "/dev/%s", input) <
		(int)sizeof(candidate) &&
	    try_candidate(candidate, canonical_device, canonical_device_len,
		device_path, device_path_len) == 0) {
		return 0;
	}

	/* Bare alias under /dev/cx, e.g. vcr0-video */
	if (input[0] != '/' && strchr(input, '/') == NULL &&
	    snprintf(candidate, sizeof(candidate), "/dev/cx/%s", input) <
		(int)sizeof(candidate) &&
	    try_candidate(candidate, canonical_device, canonical_device_len,
		device_path, device_path_len) == 0) {
		return 0;
	}

	/* Last-chance relative /dev lookup for names outside /dev/cx. */
	if (input[0] != '/' && strchr(input, '/') == NULL &&
	    snprintf(candidate, sizeof(candidate), "/dev/%s", input) <
		(int)sizeof(candidate) &&
	    try_candidate(candidate, canonical_device, canonical_device_len,
		device_path, device_path_len) == 0) {
		return 0;
	}

	fprintf(stderr,
		"failed to resolve '%s' as a cxadc device (expected cxadcN, N, /dev/*, or bare alias under /dev/cx)\n",
		input);
	return -1;
}

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
