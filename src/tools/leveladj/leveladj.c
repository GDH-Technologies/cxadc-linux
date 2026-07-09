/*
 * leveladj - auto-adjust the cxadc fixed gain ("level") for a good signal range.
 *
 * Historically this scanned the level linearly with a 65 MiB static buffer. It
 * now uses the shared single-pass histogram analyser (see cx_analyze) over a
 * modest heap buffer and converges on the highest non-clipping level. The
 * command-line interface and printed output columns are unchanged.
 */

#include "utils.h"
#include "cx_analyze.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define READ_LEN (2 * 1024 * 1024)
#define MAX_ITERS 48

int main(int argc, char *argv[])
{
	int fd;
	int level;
	char device_input[128];
	char device[64];
	char device_path[128];

	int tenbit = -1;
	int tenxfsc = -1;
	int c;

	opterr = 0;
	snprintf(device_input, sizeof(device_input), "cxadc0");
	snprintf(device, sizeof(device), "cxadc0");
	snprintf(device_path, sizeof(device_path), "/dev/cxadc0");

	while ((c = getopt(argc, argv, "d:bx")) != -1) {
		switch (c) {
		case 'b':
			tenbit = 1;
			break;
		case 'x':
			tenxfsc = 1;
			break;
		case 'd':
			if (snprintf(device_input, sizeof(device_input), "%s", optarg) >=
			    (int)sizeof(device_input)) {
				fprintf(stderr, "device input too long: %s\n", optarg);
				return -1;
			}
			break;
		}
	}

	if (cxadc_resolve_device(device_input, device, sizeof(device), device_path,
	    sizeof(device_path)) != 0)
		return -1;

	/* Check the device exists without opening it (open can disturb capture). */
	if (access(device_path, F_OK) != 0) {
		fprintf(stderr, "%s not found\n", device_path);
		return -1;
	}

	if (tenbit >= 0 && set_cxadc_param("tenbit", device, tenbit))
		return -1;
	if (read_cxadc_param("tenbit", device, &tenbit))
		return -1;

	if (tenxfsc >= 0 && set_cxadc_param("tenxfsc", device, tenxfsc))
		return -1;
	if (read_cxadc_param("tenxfsc", device, &tenxfsc))
		return -1;

	/* Positional argument: set a fixed level and exit (unchanged behaviour). */
	if (argc > optind) {
		level = atoi(argv[optind]);
		if (set_cxadc_param("level", device, level))
			return -1;
		return 0;
	}

	uint8_t *buf = malloc(READ_LEN);
	if (buf == NULL) {
		fprintf(stderr, "failed to allocate %d bytes\n", READ_LEN);
		return -1;
	}

	/* Start from the current level if readable, otherwise the historic default. */
	if (read_cxadc_param("level", device, &level) || level < 0 || level > 31)
		level = 20;

	int best_safe = -1;   /* highest level observed with no clipping */
	int visited[32] = {0};
	int ret = 0;

	for (int iter = 0; iter < MAX_ITERS; iter++) {
		cx_stats st;
		ssize_t got;

		if (set_cxadc_param("level", device, level)) {
			ret = -1;
			break;
		}

		fd = open(device_path, O_RDONLY);
		if (fd < 0) {
			fprintf(stderr, "failed to open %s: %s\n", device_path,
				strerror(errno));
			ret = -1;
			break;
		}

		printf("testing level %d\n", level);

		got = read(fd, buf, READ_LEN);
		close(fd);
		if (got < 0) {
			printf("failed to read from device %s\n", device);
			ret = -1;
			break;
		}

		if (cx_analyze(buf, (size_t)got, tenbit, 0.0, &st)) {
			fprintf(stderr, "no samples read from %s\n", device);
			ret = -1;
			break;
		}

		printf("low %d high %d clipped %d nsamp %d\n", (int)st.min,
		       (int)st.max, (int)(st.clip_low + st.clip_high),
		       (int)st.nsamp);

		int clipping = (st.clip_fraction > 0.001) || (st.utilization >= 1.0);
		if (!clipping && level > best_safe)
			best_safe = level;

		visited[level] = 1;

		int next = cx_suggest_level(&st, level, 0.0);
		if (next == level)
			break; /* converged */
		if (visited[next]) {
			/* Oscillating between two levels: settle on the safe one. */
			break;
		}
		level = next;
	}

	/* Ensure we leave the card at the highest known non-clipping level. */
	if (ret == 0 && best_safe >= 0 && best_safe != level)
		ret = set_cxadc_param("level", device, best_safe);

	free(buf);
	return ret;
}

