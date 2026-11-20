/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/efm_bd.h - EFM-BD in fixed point (native/efm_bd.c): mdkit's EFM-BD is a
 * double-precision model, not DSP code, so the Syntakt runs this port of it instead
 * (tests/test_md_st.py measures it against mdkit's by signal-to-error ratio).
 */
#ifndef MD_EFM_BD_H
#define MD_EFM_BD_H
#include "../voices/md-types.h"
#include "../voices/mdkit_voice.h"

/* the coefficients of a block (ptch Q37; dec, rdec, mdec as 1 - decay, Q37; the rest Q31) */
struct md_efm_bd_coeff { int32_t ptch, dec, rdec, mdec, ramp, mod, mfb, mfrq; };

typedef struct md_efm_bd {
    mdkit_control_clock clock;    /* mdkit's control clock, in float32 as mdkit keeps it */
    unsigned pos;
    uint32_t ph1, ph2, mod;       /* phases and the modulation, 32768 to a cycle, Q15.17 */
    /* the envelopes op1, op2 (Q31) and fb (Q27: a retrigger adds to it): each is its base
     * times its decay's k-th power, k the samples since the block began or the note */
    int32_t base1, base2, basef;
    unsigned k;
    int32_t p1[33], p2[33], pf[33];   /* the powers of rdec, mdec, dec (Q31) */
    struct md_efm_bd_coeff c;         /* the coefficients and the powers, made from these */
    uint32_t made[8];                 /* bits of clock.current (a NaN after init: none made) */
} md_efm_bd;

extern const mdkit_voice mdkit_voice_efm_bd;
void md_st_efm_bd_render_words(void *state, int32_t *out, unsigned frames);
#endif
