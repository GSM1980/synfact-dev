/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/efm_rs.c - EFM-RS's render, word for word mdkit's mdkit_efm_rs_render
 * (mdkit/src/efm/rs/mdkit_efm_rs.c), in 32-bit integers. Two operator pairs (A,
 * the snare, and B), each as EFM-CP's (native/efm_cp.c says why each step is the
 * same), then out = acc_to_word(mpyr(b_out, sine B) + mpyr(SNAR, A's stage)):
 * - operator 1's step, asr(mpy(rate, const), 5), is the same all block: made once.
 * - mdkit adds the phases as pack48 leaves them (no sign extension); only the 24
 *   bits kept are read, so they are added modulo 2^24 as EFM-CP's are.
 * - the tail is the two-pole resonator, its first two samples with the rounded MAC
 *   (md_round_sum2_less), the rest with the plain one.
 * - The coefficients are made again only when the clock's current values move
 *   (efm_clock.c's boundary): they are a function of them alone.
 * Included by the generated voice in place of md_st_efm_rs_render_words
 * (tools/md_port.py, NATIVE). tests/test_md_st.py compares every word.
 */
#include "../voices/md-native.h"
#ifdef MD_AP_FM
#include "efm_fm.h"
/* MD_AP_FM: a pair with 32-bit phases (efm_fm.h): phase 1 p, its modulation m, phase 2 q */
struct efm_rs_fm { uint32_t p, m, q; };
static inline int32_t efm_rs_fm_pair(struct efm_rs_fm *o, uint32_t step1, int32_t rate, int32_t index, int32_t mod)
{
    const int32_t x0 = md_fm_sine(o->p + o->m);
    int32_t x1, s2;
    o->p += step1;
    o->m = md_fm_step(index, x0, 7);
    x1 = md_wrap(md_mpyr(x0, rate));
    s2 = md_fm_sine(o->q);
    o->q += md_fm_step(x1, mod, 4) + ((uint32_t)rate << 9);
    return s2;
}
#endif

/* one operator pair: phase 1 (p, its modulation m), phase 2 (q); returns the
 * second sine's word and leaves the first sine's carrier word in *x1 */
struct efm_rs_pair { uint32_t ph, pl, mh, ml, qh, ql; };
static inline int32_t efm_rs_pair(struct efm_rs_pair *p, int32_t s1h, uint32_t s1l, int32_t rate, int32_t index, int32_t mod)
{
    int32_t hi, lo, x0, x1;
    uint32_t sl = p->pl + p->ml, st = p->ph + p->mh + (sl >> 24), idx;
    sl &= 0xffffffu;
    idx = st & 0x7fffu;
    lo = (int32_t)sl - (int32_t)p->ml + (int32_t)s1l;
    st = (uint32_t)((int32_t)idx - (int32_t)p->mh + s1h + (lo >> 24));
    p->ph = st & 0xffffffu; p->pl = (uint32_t)lo & 0xffffffu;
    x0 = md_s(rs_sine((uint16_t)idx) & 0xffffffu);
    p->ml = md_mul_floor48(index, x0, 7, &hi); p->mh = (uint32_t)hi & 0xffffffu;
    x1 = md_wrap(md_mpyr(x0, rate));
    idx = p->qh;
    {
        const uint32_t ql = md_mul_floor48(x1, mod, 4, &hi), l2 = p->ql + ql + (((uint32_t)rate & 255u) << 16);
        p->qh = ((uint32_t)((int32_t)p->qh + hi + (rate >> 8) + (int32_t)(l2 >> 24))) & 0x7fffu;
        p->ql = l2 & 0xffffffu;
    }
    return md_s(rs_sine((uint16_t)idx) & 0xffffffu);
}

void md_st_efm_rs_render_words(void *state, int32_t *out, unsigned frames)
{
    mdkit_efm_rs *s = (mdkit_efm_rs *)state;
    while (frames) {
        if (s->pos == 0u) {
            if (efm_clock_boundary(&s->clock)) rs_build_controls(s);
            s->have_block = 0;
        }
        if (!s->have_block) {
            int32_t ring[32];
            unsigned i;
#ifdef MD_AP_FM
            {
                struct efm_rs_fm A = {md_fm_from48(s->a_phase), md_fm_from48(s->a_modacc), md_fm_from48(s->a_phase2)};
                struct efm_rs_fm B = {md_fm_from48(s->b_phase), md_fm_from48(s->b_modacc), md_fm_from48(s->b_phase2)};
                const int32_t RA = md_s(s->a_rate), RB = md_s(s->b_rate), IA = md_s(s->a_index), IB = md_s(s->b_index),
                              MA = md_s(s->a_mod), MB = md_s(s->b_mod), DA = md_s(s->a_out_decay), DB = md_s(s->b_out_decay),
                              SN = md_s(s->snare_gain);
                int32_t AO = md_s(s->a_out), BO = md_s(s->b_out), AS = md_s(s->a_stage);
                const uint32_t sa = md_fm_step(RA, md_s(s->a_const), 4), sb = md_fm_step(RB, md_s(s->b_const), 4);
                for (i = 0; i < 32; ++i) {
                    int32_t x0 = efm_rs_fm_pair(&A, sa, RA, IA, MA), n;
                    n = md_wrap(md_mid_mpy_exact(DA, AO));
                    AS = md_wrap(md_mpyr(AO, x0)); AO = n;
                    x0 = efm_rs_fm_pair(&B, sb, RB, IB, MB);
                    n = md_wrap(md_mid_mpy_exact(DB, BO));
                    ring[i] = md_sat(md_mpyr(BO, x0) + md_mpyr(SN, AS));
                    BO = n;
                }
                s->a_phase = md_fm_to48(A.p); s->a_modacc = md_fm_to48(A.m); s->a_phase2 = md_fm_to48(A.q);
                s->b_phase = md_fm_to48(B.p); s->b_modacc = md_fm_to48(B.m); s->b_phase2 = md_fm_to48(B.q);
                s->a_out = md_w24(AO); s->b_out = md_w24(BO); s->a_stage = md_w24(AS);
            }
#else
            {
                struct efm_rs_pair A = {(uint32_t)MD_T(s->a_phase) & 0xffffffu, MD_L(s->a_phase),
                                        (uint32_t)MD_T(s->a_modacc) & 0xffffffu, MD_L(s->a_modacc),
                                        (uint32_t)MD_T(s->a_phase2) & 0xffffffu, MD_L(s->a_phase2)};
                struct efm_rs_pair B = {(uint32_t)MD_T(s->b_phase) & 0xffffffu, MD_L(s->b_phase),
                                        (uint32_t)MD_T(s->b_modacc) & 0xffffffu, MD_L(s->b_modacc),
                                        (uint32_t)MD_T(s->b_phase2) & 0xffffffu, MD_L(s->b_phase2)};
                const int32_t RA = md_s(s->a_rate), RB = md_s(s->b_rate), IA = md_s(s->a_index), IB = md_s(s->b_index),
                              MA = md_s(s->a_mod), MB = md_s(s->b_mod), DA = md_s(s->a_out_decay), DB = md_s(s->b_out_decay),
                              SN = md_s(s->snare_gain);
                int32_t sah, sbh, AO = md_s(s->a_out), BO = md_s(s->b_out), AS = md_s(s->a_stage);
                const uint32_t sal = md_mul_floor48(RA, md_s(s->a_const), 4, &sah), sbl = md_mul_floor48(RB, md_s(s->b_const), 4, &sbh);
                for (i = 0; i < 32; ++i) {
                    int32_t x0 = efm_rs_pair(&A, sah, sal, RA, IA, MA), n;
                    n = md_wrap(md_mid_mpy_exact(DA, AO));
                    AS = md_wrap(md_mpyr(AO, x0)); AO = n;
                    x0 = efm_rs_pair(&B, sbh, sbl, RB, IB, MB);
                    n = md_wrap(md_mid_mpy_exact(DB, BO));
                    ring[i] = md_sat(md_mpyr(BO, x0) + md_mpyr(SN, AS));   /* mpyr leaves A0 = 0 */
                    BO = n;
                }
                s->a_phase = md_acc_make((int32_t)A.ph, A.pl); s->a_modacc = md_acc_make((int32_t)A.mh, A.ml);
                s->a_phase2 = md_acc_make((int32_t)A.qh, A.ql);
                s->b_phase = md_acc_make((int32_t)B.ph, B.pl); s->b_modacc = md_acc_make((int32_t)B.mh, B.ml);
                s->b_phase2 = md_acc_make((int32_t)B.qh, B.ql);
                s->a_out = md_w24(AO); s->b_out = md_w24(BO); s->a_stage = md_w24(AS);
            }
#endif
            {
                const uint32_t coef = s->tail_coef;
                const uint32_t gain = mdkit_mid(mdkit_add(mdkit_asr(mdkit_from24(coef), 1), mdkit_from24(0x400000)));
                const int32_t g = md_s(gain), c = md_s(coef);
                int32_t hist = md_s(s->res_hist), previous = md_s(s->res_delay);
                for (i = 0; i < 32; ++i) {
                    const int32_t old = hist;
                    if (i < 2) {
                        hist = md_wrap(md_mid_mpy_exact(ring[i], g));
                        previous = md_round_sum2_less(ring[i], g, previous, c, old);
                    } else {
                        int32_t first, sum = md_mid_mpy2_first(ring[i], g, previous, c, &first);
                        hist = md_wrap(first);
                        previous = md_sat(sum - old);
                    }
                    s->block[i] = md_w24(previous);
                }
                s->res_hist = md_w24(hist); s->res_delay = md_w24(previous);
            }
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
