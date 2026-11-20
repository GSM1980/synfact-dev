/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/efm_cp.c - EFM-CP's render, word for word mdkit's mdkit_efm_cp_render,
 * its clipper and cp_resonance (mdkit/src/efm/cp/mdkit_efm_cp.c), in 32-bit
 * integers, as native/efm_sd.c does for EFM-SD (whose operators these are, with
 * operator 1's feedback and step read from the knobs' words):
 * - "mpy then mid" is md_mid_mpy_exact, "mpyr then mid" md_mpyr; asl(mpy(y1, PTCH), 3)
 *   + PTCH, read as its middle word, is floor(y1 PTCH / 2^20) + PTCH.
 * - The 48-bit phases keep A1 & 0x7fff : A0 only where the DSP masks them; A2 never
 *   reaches A1. The steps are 48-bit products as md_mul_floor48 gives them.
 * - cp_resonance is EFM-SD's sd_resonance (efm_sd.c): one sum of two products.
 * - The coefficients are made again only when the clock's current values move
 *   (efm_clock.c's boundary): they are a function of them alone.
 * Included by the generated audio/voices/mdkit_efm_cp.c in place of
 * md_st_efm_cp_render_words (tools/md_port.py, NATIVE). tests/test_md_st.py compares
 * every word with mdkit's, on the host and, with the EMAC, in analog-emu.
 */
#include "../voices/md-native.h"

/* cp_resonance on signed words: out = word(floor((in g + prev c) / 2^23) - hist_old) */
static void efm_cp_resonance(const int32_t in[32], uint32_t out[32], uint32_t coefficient, uint32_t *history, uint32_t *delay)
{
    const uint32_t gain = mdkit_mid(mdkit_add(mdkit_asr(mdkit_from24(coefficient), 1), mdkit_from24(0x400000)));
    const int32_t g = md_s(gain), c = md_s(coefficient);
    int32_t hist = md_s(*history), previous = md_s(*delay);
    for (unsigned i = 0; i < 32; ++i) {
        int32_t first, sum = md_mid_mpy2_first(in[i], g, previous, c, &first);
        previous = md_sat(sum - hist);
        hist = md_wrap(first);
        out[i] = md_w24(previous);
    }
    *history = md_w24(hist);
    *delay = md_w24(previous);
}

void md_st_efm_cp_render_words(void *state, int32_t *out, unsigned frames)
{
    mdkit_efm_cp *s = (mdkit_efm_cp *)state;
    while (frames) {
        if (s->pos == 0u) {
            if (efm_clock_boundary(&s->clock)) cp_build_controls(s);
            s->have_block = 0;
        }
        if (!s->have_block) {
            int32_t ring[32];
            unsigned i;
            {
                uint32_t ph1 = (uint32_t)MD_T(s->l_c) & 0xffffffu, pl1 = MD_L(s->l_c),
                         ph2 = (uint32_t)MD_T(s->l_e) & 0xffffffu, pl2 = MD_L(s->l_e),
                         t13 = (uint32_t)MD_T(s->l_13) & 0xffffffu, l13l = MD_L(s->l_13);
                const int32_t FBC = md_s(s->fb_coef), R = md_s(s->op1_rate), MR = md_s(s->mod_rate),
                              MI = md_s(s->mod_index), FD = md_s(s->fb_decay), MA = md_s(s->mod_amount);
                int32_t O1 = md_s(s->op1_out), O2 = md_s(s->op2_out);
                for (i = 0; i < 32; ++i) {
                    int32_t hi, lo, y1, x0, x1, n;
                    uint32_t sl, st, idx, s1l;
                    /* operator 1's feedback and its step: y1 = mid(asl(mpy(op1, PTCH), 3) + PTCH) */
                    n = md_wrap(md_mid_mpy_exact(FBC, O1));
                    y1 = md_wrap(md_mul_shift_exact(O1, R, 20) + R);
                    O1 = n;
                    s1l = md_mul_floor48(y1, MR, 4, &hi);                 /* asr(mpy(y1, MFRQ), 5) */
                    /* index = ((lc + l13) & 0x7fff in A1), lc = that - l13 + step */
                    sl = pl1 + l13l; st = ph1 + t13 + (sl >> 24); sl &= 0xffffffu;
                    idx = st & 0x7fffu;
                    lo = (int32_t)sl - (int32_t)l13l + (int32_t)s1l;
                    st = (uint32_t)((int32_t)idx - (int32_t)t13 + hi + (lo >> 24));
                    ph1 = st & 0xffffffu; pl1 = (uint32_t)lo & 0xffffffu;
                    x0 = md_s(cp_sine((uint16_t)idx) & 0xffffffu);
                    /* l13 = to48(asr(mpy(MI, x0), 8)) = floor(MI x0 / 2^7), 48 bits */
                    l13l = md_mul_floor48(MI, x0, 7, &hi); t13 = (uint32_t)hi & 0xffffffu;
                    n = md_wrap(md_mid_mpy_exact(FD, O2));
                    x0 = md_wrap(md_mpyr(O2, x0)); O2 = n;
                    x1 = md_wrap(md_mpyr(x0, y1));
                    /* operator 2's step: asr(asl(mpy(x1, MOD), 3) + y1, 8) = y1 * 2^16 + floor(x1 MOD / 16);
                     * le = (le + step) & 0x7fff in A1, read before the add */
                    idx = ph2;
                    {
                        const uint32_t ql = md_mul_floor48(x1, MA, 4, &hi), l2 = pl2 + ql + (((uint32_t)y1 & 255u) << 16);
                        ph2 = ((uint32_t)((int32_t)ph2 + hi + (y1 >> 8) + (int32_t)(l2 >> 24))) & 0x7fffu;
                        pl2 = l2 & 0xffffffu;
                    }
                    ring[i] = md_s(cp_sine((uint16_t)idx) & 0xffffffu);
                }
                s->l_c = md_acc_make((int32_t)ph1, pl1); s->l_e = md_acc_make((int32_t)ph2, pl2);
                s->l_13 = md_acc_make((int32_t)t13, l13l);
                s->op1_out = md_w24(O1); s->op2_out = md_w24(O2);
            }
            /* the clipper, in place: x0 = CDEC (DEC once the counter is 0), b = CLPS, y0 = clip_env */
            {
                uint32_t b = s->clip_b, y0 = s->clip_env;
                int32_t X0 = md_s(b == 0 ? s->op1_decay : s->clipper_decay);
                for (i = 0; i < 32; ++i) {
                    const int32_t Y0 = md_s(y0);
                    y0 = md_w24(md_mid_mpy_exact(Y0, X0));
                    if (b != 0 && y0 < 0x66666u) {
                        b = (b - 1) & 0xffffffu;
                        y0 = 0x50c335u;
                        if (b == 0) X0 = md_s(s->op1_decay);
                    }
                    ring[i] = md_wrap(md_mpyr(ring[i], Y0));
                }
                s->clip_b = b; s->clip_env = y0;
            }
            efm_cp_resonance(ring, s->block, s->tail_coef, &s->res_hist, &s->res_delay);
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
