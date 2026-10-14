/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EUCLID: the smallest step-fire mod for the Digitone mk1 (seq-events). Track 1 fires 5 steps of
 * every 16, spread as evenly as they go (a Euclidean rhythm), whatever its own trigs say; a step
 * it adds plays the track's NOTE. Every other track plays as stock. A template for a mod that
 * changes what the sequencer plays (docs/ADAPTING.md, "Step-fire events"). */
#include "digitone-mk1/seq.h"

#define TRACK   0                       /* T1 */
#define PULSES  5
#define STEPS   16

/* Interrupt level, both reads of each of track 1's steps: the rhythm's answer. */
void euclid_trig(int32_t track, int32_t step, int32_t first, uint32_t *trig)
{
    (void)first;
    if (track != TRACK)
        return;
    if ((step % STEPS) * PULSES % STEPS < PULSES)
        *trig |= SQ_TRIG;
    else
        *trig &= ~SQ_TRIG;
}
