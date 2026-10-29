/* SPDX-License-Identifier: GPL-2.0-or-later
 * Stored RATIO B: index 19*slow+fast (0..360) * FM_RATIO_STEP. 88 spreads
 * the 361 combinations over the native 0..7f00 range, so a full-depth LFO
 * or modulation sweeps all of them. AUDIO (audio.s) decodes the same way. */
#ifndef FM_RATIO_H
#define FM_RATIO_H
#define FM_RATIO_STEP 88u
#define FM_RATIO_MAX 360u
#define FM_RATIO_DEFAULT 60u      /* B1 = B2 = 3 (ratio 1.00) */
static __inline__ unsigned int fm_ratio_index(unsigned int stored)
{unsigned int i=(stored&65535u)/FM_RATIO_STEP;return i>FM_RATIO_MAX?FM_RATIO_MAX:i;}
#endif
