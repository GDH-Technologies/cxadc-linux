#ifndef CXADC_COMMON_ANALYZE_H
#define CXADC_COMMON_ANALYZE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Single-pass signal statistics for cxadc sample buffers.
 *
 * The cxadc device returns either unsigned 8-bit samples (tenbit == 0) or
 * unsigned 16-bit samples (tenbit == 1, only the top ~10 bits are meaningful).
 * A 256-bin histogram over the most significant 8 bits gives a robust,
 * cache-friendly distribution from which min/max, tail percentiles, the DC
 * midpoint and clipping ratios are derived in a single pass.
 */

typedef struct {
	int tenbit;              /* 0 = 8-bit samples, 1 = 16-bit samples */
	unsigned int full_scale; /* 256 for 8-bit, 65536 for 16-bit       */
	uint64_t nsamp;          /* number of samples analysed            */

	unsigned int min;        /* minimum sample value (native scale)   */
	unsigned int max;        /* maximum sample value (native scale)   */
	double mean;             /* mean sample value (native scale)      */

	unsigned int p_low;      /* low  tail percentile value            */
	unsigned int p_high;     /* high tail percentile value            */

	uint64_t clip_low;       /* samples pinned at the absolute floor  */
	uint64_t clip_high;      /* samples pinned at the absolute ceiling*/
	double clip_fraction;    /* (clip_low + clip_high) / nsamp        */

	double utilization;      /* (p_high - p_low) / full_scale, 0..1   */
	double dc_error;         /* signed midpoint offset from centre,   */
	                         /* normalised to -1..1                   */
} cx_stats;

/*
 * Analyse a raw sample buffer. "tail_fraction" selects the tail percentile
 * used for p_low/p_high (e.g. 0.0005 for the 0.05%/99.95% points); pass 0 to
 * use a sensible default. Returns 0 on success, -1 if no samples were given.
 */
int cx_analyze(const uint8_t *buf, size_t len, int tenbit,
	       double tail_fraction, cx_stats *out);

/*
 * Suggest a new fixed-gain "level" (0..31) given the current level and the
 * measured statistics. Steps gain down when clipping exceeds clip_target, up
 * when the signal under-fills the range, and holds inside the target window.
 * Pass clip_target <= 0 to use the default (0.001 = 0.1%).
 */
int cx_suggest_level(const cx_stats *s, int current_level, double clip_target);

/*
 * Adaptive DC-offset controller. Because the sign of the center_offset->DC
 * transfer is not documented, this uses a sign-search: it nudges the offset in
 * the current direction and reverses direction whenever the measured DC error
 * grows instead of shrinking. Reusable across capture iterations.
 */
typedef struct {
	int have_prev;
	double prev_abs_err;
	int dir; /* current search direction, +1 or -1 */
} cx_offset_ctl;

void cx_offset_ctl_init(cx_offset_ctl *ctl);

/*
 * Return the next center_offset (0..255) given the current offset and stats.
 * "deadband" is the |dc_error| below which no change is made (pass <= 0 for the
 * default 0.01).
 */
int cx_offset_step(cx_offset_ctl *ctl, const cx_stats *s, int current_offset,
		   double deadband);

#endif /* CXADC_COMMON_ANALYZE_H */
