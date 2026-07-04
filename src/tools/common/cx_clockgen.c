#include "cx_clockgen.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* ALSA card name reported by the clockgen firmware (truncated to 15 chars). */
#define CX_CLOCKGEN_CARD_MATCH "CXADC"

int cx_clockgen_detect(char *device, size_t device_sz)
{
	FILE *f;
	char line[256];
	int found = -1;

	if (device == NULL || device_sz == 0)
		return -1;

	f = fopen("/proc/asound/cards", "r");
	if (f == NULL)
		return -1;

	/*
	 * Lines look like:
	 *   2 [CXADCADCClockGe]: USB-Audio - CXADC ADC Clock Generator
	 * Extract the bracketed card id.
	 */
	while (fgets(line, sizeof(line), f) != NULL) {
		char *open = strchr(line, '[');
		char *close = open ? strchr(open, ']') : NULL;
		if (open == NULL || close == NULL || close <= open + 1)
			continue;

		size_t id_len = (size_t)(close - open - 1);
		char id[64];
		if (id_len == 0 || id_len >= sizeof(id))
			continue;
		memcpy(id, open + 1, id_len);
		id[id_len] = '\0';

		/* Trim trailing spaces from the id. */
		while (id_len > 0 && id[id_len - 1] == ' ')
			id[--id_len] = '\0';

		if (strncmp(id, CX_CLOCKGEN_CARD_MATCH,
			    strlen(CX_CLOCKGEN_CARD_MATCH)) == 0) {
			int n = snprintf(device, device_sz, "hw:CARD=%s", id);
			if (n > 0 && (size_t)n < device_sz)
				found = 0;
			break;
		}
	}

	fclose(f);
	return found;
}

int cx_clockgen_set_clock(const char *device, int out_number,
			  const char *freq_label)
{
	char control[96];
	char value[64];
	pid_t pid;
	int status;

	if (device == NULL || freq_label == NULL)
		return -1;
	if (out_number < 0 || out_number > 1)
		return -1;

	if (snprintf(control, sizeof(control),
		     "CXADC-Clock %d Select Playback Source,0",
		     out_number) >= (int)sizeof(control))
		return -1;

	if (snprintf(value, sizeof(value), "CXADC-%s", freq_label) >=
	    (int)sizeof(value))
		return -1;

	pid = fork();
	if (pid < 0)
		return -1;

	if (pid == 0) {
		/* Child: silence amixer's stdout, keep stderr for diagnostics. */
		int devnull = open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			dup2(devnull, STDOUT_FILENO);
			close(devnull);
		}
		char *const argv[] = {
			(char *)"amixer", (char *)"-D", (char *)device,
			(char *)"sset", control, value, NULL
		};
		execvp("amixer", argv);
		_exit(127); /* exec failed */
	}

	if (waitpid(pid, &status, 0) < 0)
		return -1;

	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		return 0;

	fprintf(stderr, "amixer failed to set clock %d to %s on %s\n",
		out_number, freq_label, device);
	return -1;
}
