/*
 * cx-capture - capture the raw ADC stream from a cxadc device with optional
 * live fan-out and continuous gain/DC auto-adjustment.
 *
 * The cxadc character device only supports blocking read() (no mmap/poll), so
 * capture uses large sequential reads. Each block is fanned out to any number
 * of sinks:
 *   - file / stdout : "reliable" sinks, written with blocking writes so no
 *                     samples are lost (suitable for archival or piping into a
 *                     decoder).
 *   - tcp / fifo    : "monitor" sinks, written non-blocking; data is dropped on
 *                     backpressure so a slow visualiser or netcat client can
 *                     never stall (and thus never overrun) the capture.
 */

#define _GNU_SOURCE

#include "utils.h"
#include "cx_analyze.h"
#include "cx_clockgen.h"

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <netdb.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define CAP_BLOCK (1 << 20) /* 1 MiB read/fan-out block */
#define MAX_SINKS 16
#define ADJUST_INTERVAL_SEC 0.5

enum sink_type { SINK_FILE, SINK_STDOUT, SINK_TCP, SINK_FIFO };

struct sink {
	enum sink_type type;
	int fd;          /* active data fd, -1 if none yet          */
	int listen_fd;   /* for TCP listen mode, else -1            */
	int reliable;    /* 1 = blocking/lossless, 0 = best-effort  */
	const char *name;
	uint64_t bytes;
	uint64_t dropped;
};

static volatile sig_atomic_t g_stop;

static void on_signal(int sig)
{
	(void)sig;
	g_stop = 1;
}

static double now_sec(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static void set_nonblock(int fd)
{
	int fl = fcntl(fd, F_GETFL, 0);
	if (fl >= 0)
		fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/* Write the whole buffer, retrying on EINTR/short writes. Returns 0 or -1. */
static int write_all(int fd, const uint8_t *buf, size_t len)
{
	size_t off = 0;
	while (off < len) {
		ssize_t n = write(fd, buf + off, len - off);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		off += (size_t)n;
	}
	return 0;
}

/* Best-effort write: send what we can, drop the rest on EAGAIN. */
static void write_monitor(struct sink *s, const uint8_t *buf, size_t len)
{
	if (s->fd < 0)
		return;
	ssize_t n = write(s->fd, buf, len);
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			s->dropped += len;
			return;
		}
		if (errno == EPIPE || errno == ECONNRESET) {
			/* Client went away; close and (for listen) await another. */
			close(s->fd);
			s->fd = -1;
			return;
		}
		if (errno == EINTR)
			return;
		return;
	}
	s->bytes += (size_t)n;
	if ((size_t)n < len)
		s->dropped += len - (size_t)n;
}

static int open_output_file(struct sink *s, const char *path)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) {
		fprintf(stderr, "cannot open output file %s: %s\n", path,
			strerror(errno));
		return -1;
	}
	s->type = SINK_FILE;
	s->fd = fd;
	s->listen_fd = -1;
	s->reliable = 1;
	s->name = path;
	return 0;
}

static int open_fifo(struct sink *s, const char *path)
{
	/* Create the FIFO if needed; open non-blocking so we don't wait for a reader. */
	if (mkfifo(path, 0644) < 0 && errno != EEXIST) {
		fprintf(stderr, "cannot create fifo %s: %s\n", path, strerror(errno));
		return -1;
	}
	int fd = open(path, O_WRONLY | O_NONBLOCK);
	if (fd < 0 && errno != ENXIO) {
		fprintf(stderr, "cannot open fifo %s: %s\n", path, strerror(errno));
		return -1;
	}
	s->type = SINK_FIFO;
	s->fd = fd; /* may be -1 (ENXIO: no reader yet) */
	s->listen_fd = -1;
	s->reliable = 0;
	s->name = path;
	if (fd >= 0)
		set_nonblock(fd);
	return 0;
}

static int open_tcp_connect(struct sink *s, const char *hostport)
{
	char host[128];
	const char *colon = strrchr(hostport, ':');
	if (colon == NULL || (size_t)(colon - hostport) >= sizeof(host)) {
		fprintf(stderr, "invalid --tcp target (expected host:port): %s\n",
			hostport);
		return -1;
	}
	size_t hlen = (size_t)(colon - hostport);
	memcpy(host, hostport, hlen);
	host[hlen] = '\0';
	const char *port = colon + 1;

	struct addrinfo hints, *res, *rp;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	if (getaddrinfo(host, port, &hints, &res) != 0) {
		fprintf(stderr, "cannot resolve %s\n", hostport);
		return -1;
	}

	int fd = -1;
	for (rp = res; rp != NULL; rp = rp->ai_next) {
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (fd < 0)
			continue;
		if (connect(fd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);

	if (fd < 0) {
		fprintf(stderr, "cannot connect to %s: %s\n", hostport,
			strerror(errno));
		return -1;
	}

	set_nonblock(fd);
	s->type = SINK_TCP;
	s->fd = fd;
	s->listen_fd = -1;
	s->reliable = 0;
	s->name = hostport;
	return 0;
}

static int open_tcp_listen(struct sink *s, const char *port)
{
	struct addrinfo hints, *res, *rp;
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	if (getaddrinfo(NULL, port, &hints, &res) != 0) {
		fprintf(stderr, "invalid --listen port: %s\n", port);
		return -1;
	}

	int lfd = -1;
	for (rp = res; rp != NULL; rp = rp->ai_next) {
		lfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if (lfd < 0)
			continue;
		int one = 1;
		setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
		if (bind(lfd, rp->ai_addr, rp->ai_addrlen) == 0)
			break;
		close(lfd);
		lfd = -1;
	}
	freeaddrinfo(res);

	if (lfd < 0 || listen(lfd, 1) < 0) {
		fprintf(stderr, "cannot listen on port %s: %s\n", port,
			strerror(errno));
		if (lfd >= 0)
			close(lfd);
		return -1;
	}

	set_nonblock(lfd);
	s->type = SINK_TCP;
	s->fd = -1;
	s->listen_fd = lfd;
	s->reliable = 0;
	s->name = "tcp-listen";
	return 0;
}

/* Accept a pending client on any listening TCP sink (non-blocking). */
static void poll_accept(struct sink *sinks, int nsinks)
{
	for (int i = 0; i < nsinks; i++) {
		struct sink *s = &sinks[i];
		if (s->type != SINK_TCP || s->listen_fd < 0 || s->fd >= 0)
			continue;
		int c = accept(s->listen_fd, NULL, NULL);
		if (c >= 0) {
			set_nonblock(c);
			s->fd = c;
		}
	}
}

/* Retry opening a FIFO whose reader had not yet connected. */
static void poll_fifo(struct sink *sinks, int nsinks)
{
	for (int i = 0; i < nsinks; i++) {
		struct sink *s = &sinks[i];
		if (s->type != SINK_FIFO || s->fd >= 0)
			continue;
		int fd = open(s->name, O_WRONLY | O_NONBLOCK);
		if (fd >= 0) {
			set_nonblock(fd);
			s->fd = fd;
		}
	}
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"Usage: %s [options]\n"
		"\n"
		"Capture the raw ADC stream from a cxadc device.\n"
		"\n"
		"Device / capture:\n"
		"  -d, --device INPUT    cxadc input (name/index/path/alias; default: cxadc0)\n"
		"  -f, --frequency N     set sampling rate (tenxfsc): 0/1, MHz, or Hz\n"
		"  -t, --duration SEC    capture for SEC seconds (default: until Ctrl-C)\n"
		"      --clock LABEL     set clockgen frequency (20MHz|28.63MHz|40MHz|50MHz)\n"
		"      --auto_adjust     continuously adjust gain and DC offset (default off)\n"
		"\n"
		"Device parameters (applied before capture, optional):\n"
		"      --vmux N          input mux (0-3)\n"
		"      --level N         fixed gain (0-31)\n"
		"      --sixdb N         6 dB boost (0/1)\n"
		"      --tenbit N        sample depth (0=8-bit, 1=16-bit)\n"
		"      --center-offset N DC centre (0-255)\n"
		"      --audsel N        audio mux (0-3)\n"
		"\n"
		"Outputs (any combination; fan-out is concurrent):\n"
		"  -o, --output FILE     write raw samples to FILE (lossless)\n"
		"      --stdout          write raw samples to stdout (lossless)\n"
		"      --tcp HOST:PORT   stream to a TCP endpoint (best-effort)\n"
		"      --listen PORT     serve one TCP client (best-effort)\n"
		"      --fifo PATH       write to a named pipe (best-effort)\n"
		"  -h, --help            show this help\n",
		prog);
}

/* Long-only option codes. */
enum {
	OPT_STDOUT = 256, OPT_TCP, OPT_LISTEN, OPT_FIFO, OPT_CLOCK, OPT_AUTO,
	OPT_VMUX, OPT_LEVEL, OPT_SIXDB, OPT_TENBIT, OPT_CENTER, OPT_AUDSEL
};

int main(int argc, char *argv[])
{
	char device_input[128];
	char device[64];
	char device_path[128];
	int frequency = -1;
	double duration = 0.0; /* 0 = unlimited */
	const char *clock_label = NULL;
	int auto_adjust = 0;

	int p_vmux = -1, p_level = -1, p_sixdb = -1, p_tenbit = -1,
	    p_center = -1, p_audsel = -1;

	struct sink sinks[MAX_SINKS];
	int nsinks = 0;
	int ret = 0;

	memset(sinks, 0, sizeof(sinks));

	snprintf(device_input, sizeof(device_input), "cxadc0");
	snprintf(device, sizeof(device), "cxadc0");
	snprintf(device_path, sizeof(device_path), "/dev/cxadc0");

	static const struct option longopts[] = {
		{ "device", required_argument, 0, 'd' },
		{ "frequency", required_argument, 0, 'f' },
		{ "duration", required_argument, 0, 't' },
		{ "clock", required_argument, 0, OPT_CLOCK },
		{ "auto_adjust", no_argument, 0, OPT_AUTO },
		{ "auto-adjust", no_argument, 0, OPT_AUTO },
		{ "vmux", required_argument, 0, OPT_VMUX },
		{ "level", required_argument, 0, OPT_LEVEL },
		{ "sixdb", required_argument, 0, OPT_SIXDB },
		{ "tenbit", required_argument, 0, OPT_TENBIT },
		{ "center-offset", required_argument, 0, OPT_CENTER },
		{ "audsel", required_argument, 0, OPT_AUDSEL },
		{ "output", required_argument, 0, 'o' },
		{ "stdout", no_argument, 0, OPT_STDOUT },
		{ "tcp", required_argument, 0, OPT_TCP },
		{ "listen", required_argument, 0, OPT_LISTEN },
		{ "fifo", required_argument, 0, OPT_FIFO },
		{ "help", no_argument, 0, 'h' },
		{ 0, 0, 0, 0 }
	};

	int c;
	while ((c = getopt_long(argc, argv, "d:f:t:o:h", longopts, NULL)) != -1) {
		switch (c) {
		case 'd':
			if (snprintf(device_input, sizeof(device_input), "%s", optarg) >=
			    (int)sizeof(device_input)) {
				fprintf(stderr, "device input too long: %s\n", optarg);
				return 2;
			}
			break;
		case 'f': frequency = atoi(optarg); break;
		case 't': duration = atof(optarg); break;
		case OPT_CLOCK: clock_label = optarg; break;
		case OPT_AUTO: auto_adjust = 1; break;
		case OPT_VMUX: p_vmux = atoi(optarg); break;
		case OPT_LEVEL: p_level = atoi(optarg); break;
		case OPT_SIXDB: p_sixdb = atoi(optarg); break;
		case OPT_TENBIT: p_tenbit = atoi(optarg); break;
		case OPT_CENTER: p_center = atoi(optarg); break;
		case OPT_AUDSEL: p_audsel = atoi(optarg); break;
		case 'o':
			if (nsinks >= MAX_SINKS) { fprintf(stderr, "too many outputs\n"); return 2; }
			if (open_output_file(&sinks[nsinks], optarg)) return 1;
			nsinks++;
			break;
		case OPT_STDOUT:
			if (nsinks >= MAX_SINKS) { fprintf(stderr, "too many outputs\n"); return 2; }
			sinks[nsinks].type = SINK_STDOUT;
			sinks[nsinks].fd = STDOUT_FILENO;
			sinks[nsinks].listen_fd = -1;
			sinks[nsinks].reliable = 1;
			sinks[nsinks].name = "stdout";
			sinks[nsinks].bytes = 0;
			sinks[nsinks].dropped = 0;
			nsinks++;
			break;
		case OPT_TCP:
			if (nsinks >= MAX_SINKS) { fprintf(stderr, "too many outputs\n"); return 2; }
			if (open_tcp_connect(&sinks[nsinks], optarg)) return 1;
			nsinks++;
			break;
		case OPT_LISTEN:
			if (nsinks >= MAX_SINKS) { fprintf(stderr, "too many outputs\n"); return 2; }
			if (open_tcp_listen(&sinks[nsinks], optarg)) return 1;
			nsinks++;
			break;
		case OPT_FIFO:
			if (nsinks >= MAX_SINKS) { fprintf(stderr, "too many outputs\n"); return 2; }
			if (open_fifo(&sinks[nsinks], optarg)) return 1;
			nsinks++;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 2;
		}
	}

	if (nsinks == 0) {
		fprintf(stderr, "no output specified (use -o, --stdout, --tcp, --listen or --fifo)\n");
		return 2;
	}

	if (cxadc_resolve_device(device_input, device, sizeof(device), device_path,
	    sizeof(device_path)) != 0) {
		return 2;
	}

	if (access(device_path, F_OK) != 0) {
		fprintf(stderr, "%s not found\n", device_path);
		return 1;
	}

	/* Optional clockgen frequency change (best-effort, warn on failure). */
	if (clock_label != NULL) {
		char cgdev[64];
		if (cx_clockgen_detect(cgdev, sizeof(cgdev)) == 0) {
			if (cx_clockgen_set_clock(cgdev, 0, clock_label))
				fprintf(stderr, "warning: failed to set clockgen frequency\n");
			else
				fprintf(stderr, "clockgen %s set to %s\n", cgdev, clock_label);
		} else {
			fprintf(stderr, "warning: --clock given but no clockgen device found\n");
		}
	}

	/* Apply requested sysfs parameters before opening the device. */
	if (p_vmux >= 0 && set_cxadc_param("vmux", device, p_vmux)) return 1;
	if (p_sixdb >= 0 && set_cxadc_param("sixdb", device, p_sixdb)) return 1;
	if (p_tenbit >= 0 && set_cxadc_param("tenbit", device, p_tenbit)) return 1;
	if (p_audsel >= 0 && set_cxadc_param("audsel", device, p_audsel)) return 1;
	if (frequency >= 0 && set_cxadc_param("tenxfsc", device, frequency)) return 1;
	if (p_level >= 0 && set_cxadc_param("level", device, p_level)) return 1;
	if (p_center >= 0 && set_cxadc_param("center_offset", device, p_center)) return 1;

	/* Read back the state we need for analysis / auto-adjust. */
	int tenbit = 0, level = 16, center_offset = 2;
	read_cxadc_param("tenbit", device, &tenbit);
	read_cxadc_param("level", device, &level);
	read_cxadc_param("center_offset", device, &center_offset);

	/* Ignore SIGPIPE so a disconnecting monitor never kills the capture. */
	signal(SIGPIPE, SIG_IGN);
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	uint8_t *buf = NULL;
	if (posix_memalign((void **)&buf, 4096, CAP_BLOCK) != 0 || buf == NULL) {
		fprintf(stderr, "failed to allocate capture buffer\n");
		return 1;
	}

	int fd = open(device_path, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "failed to open %s: %s\n", device_path, strerror(errno));
		free(buf);
		return 1;
	}

	cx_offset_ctl octl;
	cx_offset_ctl_init(&octl);

	double t_start = now_sec();
	double t_last_report = t_start;
	double t_last_adjust = t_start;
	uint64_t total = 0;

	while (!g_stop) {
		if (duration > 0.0 && (now_sec() - t_start) >= duration)
			break;

		ssize_t got = read(fd, buf, CAP_BLOCK);
		if (got < 0) {
			if (errno == EINTR)
				continue;
			fprintf(stderr, "read error on %s: %s\n", device, strerror(errno));
			ret = 1;
			break;
		}
		if (got == 0)
			continue;

		total += (size_t)got;

		poll_accept(sinks, nsinks);
		poll_fifo(sinks, nsinks);

		for (int i = 0; i < nsinks; i++) {
			struct sink *s = &sinks[i];
			if (s->reliable) {
				if (s->fd >= 0 && write_all(s->fd, buf, (size_t)got)) {
					fprintf(stderr, "write error on %s: %s\n",
						s->name, strerror(errno));
					g_stop = 1;
					ret = 1;
					break;
				}
				s->bytes += (size_t)got;
			} else {
				write_monitor(s, buf, (size_t)got);
			}
		}

		double now = now_sec();

		if (auto_adjust && (now - t_last_adjust) >= ADJUST_INTERVAL_SEC) {
			cx_stats st;
			if (cx_analyze(buf, (size_t)got, tenbit, 0.0, &st) == 0) {
				int nl = cx_suggest_level(&st, level, 0.0);
				if (nl != level && set_cxadc_param("level", device, nl) == 0)
					level = nl;
				int no = cx_offset_step(&octl, &st, center_offset, 0.0);
				if (no != center_offset &&
				    set_cxadc_param("center_offset", device, no) == 0)
					center_offset = no;
			}
			t_last_adjust = now;
		}

		if ((now - t_last_report) >= 1.0) {
			double mbps = (double)total / (now - t_start) / (1024.0 * 1024.0);
			uint64_t drops = 0;
			for (int i = 0; i < nsinks; i++)
				drops += sinks[i].dropped;
			fprintf(stderr,
				"\rcaptured %.1f MiB  %.2f MiB/s  level %d  offset %d  dropped %llu KiB   ",
				(double)total / (1024.0 * 1024.0), mbps, level,
				center_offset, (unsigned long long)(drops / 1024));
			fflush(stderr);
			t_last_report = now;
		}
	}

	fprintf(stderr, "\ncapture finished: %.1f MiB total\n",
		(double)total / (1024.0 * 1024.0));
	for (int i = 0; i < nsinks; i++) {
		struct sink *s = &sinks[i];
		if (!s->reliable)
			fprintf(stderr, "  %s: %llu MiB sent, %llu MiB dropped\n",
				s->name, (unsigned long long)(s->bytes / (1024 * 1024)),
				(unsigned long long)(s->dropped / (1024 * 1024)));
		if (s->fd >= 0 && s->type != SINK_STDOUT)
			close(s->fd);
		if (s->listen_fd >= 0)
			close(s->listen_fd);
	}

	close(fd);
	free(buf);
	return ret;
}
