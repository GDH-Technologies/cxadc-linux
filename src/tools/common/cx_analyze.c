#include "cx_analyze.h"

#include <string.h>

/* Number of histogram bins (over the most significant 8 bits of each sample). */
#define CX_HIST_BINS 256

#define CX_DEFAULT_TAIL      0.0005 /* 0.05% / 99.95% percentiles          */
#define CX_DEFAULT_CLIP      0.001  /* 0.1% clipped samples is "too hot"   */
#define CX_UTIL_TARGET_HI    0.92   /* aim to fill up to ~92% of the range */
#define CX_UTIL_TARGET_LO    0.70   /* below this, gain is stepped up      */
#define CX_DEFAULT_DEADBAND  0.01   /* DC error deadband                   */

int cx_analyze(const uint8_t *buf, size_t len, int tenbit,
	       double tail_fraction, cx_stats *out)
{
	uint64_t hist[CX_HIST_BINS];
	uint64_t nsamp;
	uint64_t sum = 0;
	unsigned int full_scale;
	unsigned int min_v, max_v;

	if (buf == NULL || out == NULL)
		return -1;

	memset(hist, 0, sizeof(hist));
	memset(out, 0, sizeof(*out));

	if (tail_fraction <= 0.0)
		tail_fraction = CX_DEFAULT_TAIL;

	if (tenbit) {
		const uint16_t *w = (const uint16_t *)buf;
		nsamp = len / 2;
		full_scale = 65536;
		min_v = full_scale;
		max_v = 0;
		for (uint64_t i = 0; i < nsamp; i++) {
			unsigned int v = w[i];
			sum += v;
			if (v < min_v)
				min_v = v;
			if (v > max_v)
				max_v = v;
			if (v == 0)
				out->clip_low++;
			else if (v == 0xffff)
				out->clip_high++;
			hist[v >> 8]++;
		}
	} else {
		nsamp = len;
		full_scale = 256;
		min_v = full_scale;
		max_v = 0;
		for (uint64_t i = 0; i < nsamp; i++) {
			unsigned int v = buf[i];
			sum += v;
			if (v < min_v)
				min_v = v;
			if (v > max_v)
				max_v = v;
			if (v == 0)
				out->clip_low++;
			else if (v == 0xff)
				out->clip_high++;
			hist[v]++;
		}
	}

	if (nsamp == 0)
		return -1;

	out->tenbit = tenbit;
	out->full_scale = full_scale;
	out->nsamp = nsamp;
	out->min = min_v;
	out->max = max_v;
	out->mean = (double)sum / (double)nsamp;
	out->clip_fraction =
		(double)(out->clip_low + out->clip_high) / (double)nsamp;

	/* Tail percentiles from the histogram (bins are the top 8 bits). */
	uint64_t tail = (uint64_t)(tail_fraction * (double)nsamp);
	unsigned int bin_shift = tenbit ? 8 : 0;

	uint64_t acc = 0;
	unsigned int lo_bin = 0;
	for (unsigned int b = 0; b < CX_HIST_BINS; b++) {
		acc += hist[b];
		if (acc > tail) {
			lo_bin = b;
			break;
		}
	}

	acc = 0;
	unsigned int hi_bin = CX_HIST_BINS - 1;
	for (int b = CX_HIST_BINS - 1; b >= 0; b--) {
		acc += hist[b];
		if (acc > tail) {
			hi_bin = (unsigned int)b;
			break;
		}
	}

	out->p_low = lo_bin << bin_shift;
	out->p_high = ((hi_bin + 1) << bin_shift) - 1;
	if (out->p_high >= full_scale)
		out->p_high = full_scale - 1;

	double span = (double)out->p_high - (double)out->p_low;
	out->utilization = span / (double)full_scale;

	double centre = (double)full_scale / 2.0;
	double midpoint = ((double)out->p_low + (double)out->p_high) / 2.0;
	out->dc_error = (midpoint - centre) / centre;

	return 0;
}

static int clampi(int v, int lo, int hi)
{
	if (v < lo)
		return lo;
	if (v > hi)
		return hi;
	return v;
}

int cx_suggest_level(const cx_stats *s, int current_level, double clip_target)
{
	if (s == NULL)
		return current_level;

	if (clip_target <= 0.0)
		clip_target = CX_DEFAULT_CLIP;

	/* Too hot: back the gain off. Step harder the more severe the clipping. */
	if (s->clip_fraction > clip_target || s->utilization >= 1.0) {
		int step = (s->clip_fraction > clip_target * 10.0) ? 2 : 1;
		return clampi(current_level - step, 0, 31);
	}

	/* Under-filling the range: bring the gain up. */
	if (s->utilization < CX_UTIL_TARGET_LO)
		return clampi(current_level + 1, 0, 31);

	/* Comfortable but with headroom below the top target: nudge up gently. */
	if (s->utilization < CX_UTIL_TARGET_HI && s->clip_fraction == 0.0)
		return clampi(current_level + 1, 0, 31);

	return clampi(current_level, 0, 31);
}

void cx_offset_ctl_init(cx_offset_ctl *ctl)
{
	if (ctl == NULL)
		return;
	ctl->have_prev = 0;
	ctl->prev_abs_err = 0.0;
	ctl->dir = 1;
}

int cx_offset_step(cx_offset_ctl *ctl, const cx_stats *s, int current_offset,
		   double deadband)
{
	double abs_err;

	if (ctl == NULL || s == NULL)
		return current_offset;

	if (deadband <= 0.0)
		deadband = CX_DEFAULT_DEADBAND;

	abs_err = s->dc_error < 0.0 ? -s->dc_error : s->dc_error;

	/* Centered closely enough: leave the offset alone. */
	if (abs_err <= deadband) {
		ctl->have_prev = 1;
		ctl->prev_abs_err = abs_err;
		return current_offset;
	}

	/*
	 * If the previous move made the error worse, the polarity assumption was
	 * wrong for this direction, so reverse the search direction.
	 */
	if (ctl->have_prev && abs_err > ctl->prev_abs_err)
		ctl->dir = -ctl->dir;

	/* Step size scales with the error magnitude for faster convergence. */
	int step = (int)(abs_err * 32.0);
	if (step < 1)
		step = 1;
	if (step > 8)
		step = 8;

	ctl->have_prev = 1;
	ctl->prev_abs_err = abs_err;

	return clampi(current_offset + ctl->dir * step, 0, 255);
}
