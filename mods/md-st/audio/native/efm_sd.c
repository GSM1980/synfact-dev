/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/efm_sd.c - EFM-SD's render, word for word mdkit's
 * mdkit_efm_sd_render and sd_resonance (mdkit/src/efm/sd/mdkit_efm_sd.c),
 * in 32-bit integers and the accumulator helpers (md-acc.h, which use the
 * EMAC on the Syntakt).
 *
 * Included by the generated audio/voices/mdkit_efm_sd.c in place of
 * md_st_efm_sd_render_words (tools/md_port.py, NATIVE). What changes:
 * - mpy(PTCH, MFRQ) >> 5, the phase step of operator 1, is the same all
 *   block: it is made once.
 * - "mpy then mid" is md_mid_mpy_exact; "mpyr then mid" is the rounded high
 *   word, md_mpyr_t; a sum of two products keeps both products' low words.
 * - The 48-bit phases keep A1 & 0x7fff : A0 only where the DSP masks them;
 *   A2 never reaches A1 (it is masked off before any use).
 * tests/test_md_st.py compares every word with mdkit's, on the host and,
 * with the EMAC, in analog-emu.
 */
#include "../voices/md-native.h"

/* sd_resonance on signed words: out = word(floor((in g + prev c) / 2^23) - hist_old) */
static void efm_sd_resonance(const int32_t in[32], int32_t out[32], uint32_t coefficient, uint32_t *history, uint32_t *delay)
{
    const uint32_t gain = mdkit_mid(mdkit_add(mdkit_asr(mdkit_from24(coefficient), 1), mdkit_from24(0x400000)));
    const int32_t g = md_s(gain), c = md_s(coefficient);
    int32_t hist = md_s(*history), previous = md_s(*delay);
    for (unsigned i = 0; i < 32; ++i) {
        int32_t first, sum = md_mid_mpy2_first(in[i], g, previous, c, &first);
        previous = md_sat(sum - hist);
        hist = md_wrap(first);
        out[i] = previous;
    }
    *history = md_w24(hist);
    *delay = md_w24(previous);
}

void md_st_efm_sd_render_words(void *state, int32_t *out, unsigned frames)
{
    mdkit_efm_sd *s = (mdkit_efm_sd *)state;
    while (frames) {
        if (s->pos == 0u) {
            if (efm_clock_boundary(&s->clock)) sd_build_controls(s);   /* a function of current alone */
            s->have_block = 0;
        }
        if (!s->have_block) {
            int32_t nring[32], ring1[32], ring2[32], block[32];
            md_acc lb = s->l_b, ld = s->l_d, l12 = s->l_12, step1, a, b;
            uint32_t fb = s->fb, op1 = s->op1_out, op2 = s->op2_out;
            const uint32_t rate = s->op1_rate, mi = s->mod_index, fbd = s->fb_decay, ma = s->mod_amount,
                           d1 = s->op1_decay, d2 = s->op2_decay, na = s->noise_amount;
            unsigned i;
            /* the additive noise, as mdkit (the parallel stores read X1 first) */
            {
                uint32_t old = s->noise15, next = (s->noise14 + old) & 0xffffffu;
                for (i = 0; i < 31; ++i) {
                    const uint32_t sum = (old + next) & 0xffffffu;
                    nring[i] = md_s(next); old = next; next = sum;
                }
                nring[31] = md_s(next);
                s->noise14 = old; s->noise15 = next;
            }
            efm_sd_resonance(nring, ring1, s->noise_coef, &s->ring1_hist, &s->ring1_delay);
            step1 = md_acc_asr(md_acc_mpy_x(rate, s->mod_rate), 5);   /* a phase step: exact (MD_AP_ACC) */
            {
            /* The 48-bit phases as their 24-bit halves: lb and ld as pack48
             * leaves them (high word 0..2^24), l12 as to48 does, the step as
             * an accumulator. A2 is masked off before any use, so it is not
             * kept. */
            uint32_t ph1 = (uint32_t)MD_T(lb) & 0xffffffu, pl1 = MD_L(lb), ph2 = (uint32_t)MD_T(ld) & 0xffffffu, pl2 = MD_L(ld),
                     t12 = (uint32_t)MD_T(l12) & 0xffffffu, l12l = MD_L(l12), s1l = MD_L(step1);
            const int32_t s1t = MD_T(step1), r = md_s(rate), MI = md_s(mi), FBD = md_s(fbd), MA = md_s(ma),
                          D1 = md_s(d1), D2 = md_s(d2), NA = md_s(na);
            int32_t FB = md_s(fb), O1 = md_s(op1), O2 = md_s(op2);
            (void)a; (void)b;
            for (i = 0; i < 32; ++i) {
                uint32_t sl, st, idx;
                int32_t lo, hi, x0, x1, y1, n;
                /* operator 1: index = ((lb + l12) & 0x7fff in A1), lb = that - l12 + step */
                sl = pl1 + l12l; st = ph1 + t12 + (sl >> 24); sl &= 0xffffffu;
                idx = st & 0x7fffu;
                x0 = md_s(sd_sine((uint16_t)idx));
                lo = (int32_t)sl - (int32_t)l12l + (int32_t)s1l;
                hi = (int32_t)idx - (int32_t)t12 + s1t + (lo >> 24);
                ph1 = (uint32_t)hi & 0xffffffu; pl1 = (uint32_t)lo & 0xffffffu;
                /* l12 = to48(asr(mpy(MI, x0), 8)) = floor(MI x0 / 2^7), 48 bits */
                l12l = md_mul_floor48(MI, x0, 7, &hi); t12 = (uint32_t)hi & 0xffffffu;
                n = md_wrap(md_mid_mpy_exact(FBD, FB));
                x0 = md_wrap(md_mpyr(FB, x0)); FB = n;
                x1 = md_wrap(md_mpyr(x0, r));
                /* operator 2's step: (asl(mpy(x1, MOD), 3) + PTCH) >> 8
                 * = PTCH * 2^16 + floor(x1 MOD / 16); ld = (ld + step) & 0x7fff in A1 */
                x0 = md_s(sd_sine((uint16_t)ph2));
                {
                    uint32_t ql = md_mul_floor48(x1, MA, 4, &hi), l2 = pl2 + ql + (((uint32_t)r & 255u) << 16);
                    ph2 = ((uint32_t)((int32_t)ph2 + hi + (r >> 8) + (int32_t)(l2 >> 24))) & 0x7fffu;
                    pl2 = l2 & 0xffffffu;
                }
                n = md_wrap(md_mid_mpy_exact(D1, O1));
                x1 = md_wrap(md_mpyr(O1, x0)); O1 = n;
                n = md_wrap(md_mid_mpy_exact(D2, O2));
                y1 = md_wrap(md_mpyr(O2, ring1[i])); O2 = n;
                /* mpyr has A0 = 0, so + from24(x1) is on the high word */
                ring2[i] = md_sat(md_mpyr(y1, NA) + x1);
            }
            fb = md_w24(FB); op1 = md_w24(O1); op2 = md_w24(O2);
            lb = md_acc_make((int32_t)ph1, pl1); ld = md_acc_make((int32_t)ph2, pl2); l12 = md_acc_make((int32_t)t12, l12l);
            }
            efm_sd_resonance(ring2, block, s->tail_coef, &s->ring2_hist, &s->ring2_delay);
            for (i = 0; i < 32; ++i) s->block[i] = md_w24(block[i]);
            s->l_b = lb; s->l_d = ld; s->l_12 = l12;
            s->fb = fb; s->op1_out = op1; s->op2_out = op2;
            s->have_block = 1;
        }
        {
            unsigned n = 32u - s->pos, i;
            if (n > frames) n = frames;
            for (i = 0; i < n; ++i) out[i] = md_s(s->block[s->pos + i]);
            out += n; frames -= n; s->pos += n;
            if (s->pos >= 32u) s->pos = 0;
        }
    }
}
