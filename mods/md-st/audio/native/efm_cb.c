/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/efm_cb.c - EFM-CB's cb_core, word for word mdkit's (mdkit/src/efm/cb/
 * mdkit_efm_cb.c), in 32-bit integers. Two pairs of operators (A then B, C then D),
 * EFM-CP's steps (native/efm_cp.c says why each is the same):
 * - A's and C's steps, asr(mpy(pitch, MFRQ), 5), are the same all block: made once;
 *   their phases read the old phase, then add the step, masked to 0x7fff in A1.
 * - B's and D's phases are EFM-CP's operator 1 (with the self modulation word added
 *   and taken off around the mask), stepped by y1 * 2^16 + floor(x1 MOD / 16).
 * - mdkit adds the phases as pack48 leaves them; only the 24 bits kept are read,
 *   so they are added modulo 2^24.
 * - the two envelopes' mpyr words are added on A1 (A0 = 0) and read as a word.
 * Included by the generated voice in place of the function (tools/md_port.py,
 * NATIVE). tests/test_md_st.py compares every word.
 */
#ifdef MD_AP_FM
/* MD_AP_FM: the same four operators with 32-bit phases (efm_fm.h): A and C read their
 * phase, then step; B and D read phase + modulation, then step by
 * floor(x1 MOD / 16) + pitch * 2^16 (in 48-bit units) */
#include "efm_fm.h"
static void cb_core(mdkit_efm_cb *s, uint32_t out[32])
{
    const int32_t PA = md_s(s->pitch_a), PC = md_s(s->pitch_c), MF = md_s(s->mod_freq), FI = md_s(s->fm_index),
                  D1 = md_s(s->dec), SP = md_s(s->fb_index), A1 = md_s(s->env1_amp), A2 = md_s(s->env2_amp),
                  D2 = md_s(s->env2_decay), MD = md_s(s->mdec);
    uint32_t b = md_fm_from48(s->phase_b), d = md_fm_from48(s->phase_d), f = md_fm_from48(s->phase_f),
             e = md_fm_from48(s->phase_11), modd = md_fm_from48(s->mod_d), mode = md_fm_from48(s->mod_11);
    int32_t FB = md_s(s->fb), E1 = md_s(s->env1), E2 = md_s(s->env2);
    const uint32_t sa = md_fm_step(PA, MF, 4), sc = md_fm_step(PC, MF, 4);
    for (unsigned i = 0; i < 32; ++i) {
        int32_t x0, x1, sb, sd, n;
        /* A */
        x0 = md_fm_sine(b); b += sa;
        x0 = md_wrap(md_mpyr(FB, x0));
        x1 = md_wrap(md_mpyr(x0, PA));
        /* B */
        sb = md_fm_sine(d + modd); d += md_fm_step(x1, FI, 4) + ((uint32_t)PA << 9);
        modd = md_fm_step(SP, sb, 7);
        /* C */
        x0 = md_fm_sine(f); f += sc;
        n = md_wrap(md_mid_mpy_exact(MD, FB));
        x0 = md_wrap(md_mpyr(FB, x0)); FB = n;
        x1 = md_wrap(md_mpyr(x0, PC));
        /* D */
        sd = md_fm_sine(e + mode); e += md_fm_step(x1, FI, 4) + ((uint32_t)PC << 9);
        mode = md_fm_step(sd, SP, 7);
        /* the mix: (B + D) / 2, by the two envelopes */
        x0 = (sb + sd) >> 1;
        n = md_wrap(md_mid_mpy_exact(D1, E1));
        x1 = md_mpyr(A1, E1); E1 = n;
        n = md_wrap(md_mid_mpy_exact(D2, E2));
        x1 = md_wrap(x1 + md_mpyr(A2, E2)); E2 = n;
        out[i] = md_w24(md_mpyr(x1, x0));
    }
    s->phase_b = md_fm_to48(b); s->phase_d = md_fm_to48(d); s->phase_f = md_fm_to48(f); s->phase_11 = md_fm_to48(e);
    s->mod_d = md_fm_to48(modd); s->mod_11 = md_fm_to48(mode);
    s->fb = md_w24(FB); s->env1 = md_w24(E1); s->env2 = md_w24(E2);
}
#else
static inline uint32_t efm_cb_step(uint32_t *h, uint32_t *l, int32_t sh, uint32_t sl)
{
    const uint32_t old = *h, x = *l + sl;
    *h = (old + (uint32_t)sh + (x >> 24)) & 0x7fffu; *l = x & 0xffffffu;
    return old;
}
/* t = (phase + mod) & 0x7fff in A1; phase = t - mod + (y1 * 2^16 + floor(x1 MOD / 16)) */
static inline uint32_t efm_cb_modulated(uint32_t *ph, uint32_t *pl, uint32_t mh, uint32_t ml, int32_t x1, int32_t mod, int32_t y1)
{
    int32_t hi, lo;
    uint32_t sl = *pl + ml, st = *ph + mh + (sl >> 24), idx;
    sl &= 0xffffffu;
    idx = st & 0x7fffu;
    {
        const uint32_t ql = md_mul_floor48(x1, mod, 4, &hi);
        lo = (int32_t)sl - (int32_t)ml + (int32_t)ql + (int32_t)(((uint32_t)y1 & 255u) << 16);
        *ph = (uint32_t)((int32_t)idx - (int32_t)mh + hi + (y1 >> 8) + (lo >> 24)) & 0xffffffu;
        *pl = (uint32_t)lo & 0xffffffu;
    }
    return idx;
}
static void cb_core(mdkit_efm_cb *s, uint32_t out[32])
{
    const int32_t PA = md_s(s->pitch_a), PC = md_s(s->pitch_c), MF = md_s(s->mod_freq), FI = md_s(s->fm_index),
                  D1 = md_s(s->dec), SP = md_s(s->fb_index), A1 = md_s(s->env1_amp), A2 = md_s(s->env2_amp),
                  D2 = md_s(s->env2_decay), MD = md_s(s->mdec);
    uint32_t bh = (uint32_t)MD_T(s->phase_b) & 0xffffffu, bl = MD_L(s->phase_b), dh = (uint32_t)MD_T(s->phase_d) & 0xffffffu,
             dl = MD_L(s->phase_d), fh = (uint32_t)MD_T(s->phase_f) & 0xffffffu, fl = MD_L(s->phase_f),
             eh = (uint32_t)MD_T(s->phase_11) & 0xffffffu, el = MD_L(s->phase_11),
             mdh = (uint32_t)MD_T(s->mod_d) & 0xffffffu, mdl = MD_L(s->mod_d),
             meh = (uint32_t)MD_T(s->mod_11) & 0xffffffu, mel = MD_L(s->mod_11);
    int32_t FB = md_s(s->fb), E1 = md_s(s->env1), E2 = md_s(s->env2), sah, sch, hi;
    const uint32_t sal = md_mul_floor48(PA, MF, 4, &sah), scl = md_mul_floor48(PC, MF, 4, &sch);
    for (unsigned i = 0; i < 32; ++i) {
        int32_t x0, x1, sb, sd, n;
        /* A */
        x0 = md_s(cb_sine((uint16_t)efm_cb_step(&bh, &bl, sah, sal)) & 0xffffffu);
        x0 = md_wrap(md_mpyr(FB, x0));
        x1 = md_wrap(md_mpyr(x0, PA));
        /* B */
        sb = md_s(cb_sine((uint16_t)efm_cb_modulated(&dh, &dl, mdh, mdl, x1, FI, PA)) & 0xffffffu);
        mdl = md_mul_floor48(SP, sb, 7, &hi); mdh = (uint32_t)hi & 0xffffffu;
        /* C */
        x0 = md_s(cb_sine((uint16_t)efm_cb_step(&fh, &fl, sch, scl)) & 0xffffffu);
        n = md_wrap(md_mid_mpy_exact(MD, FB));
        x0 = md_wrap(md_mpyr(FB, x0)); FB = n;
        x1 = md_wrap(md_mpyr(x0, PC));
        /* D */
        sd = md_s(cb_sine((uint16_t)efm_cb_modulated(&eh, &el, meh, mel, x1, FI, PC)) & 0xffffffu);
        meh = 0; mel = md_mul_floor48(sd, SP, 7, &hi); meh = (uint32_t)hi & 0xffffffu;
        /* the mix: (B + D) / 2, by the two envelopes */
        x0 = (sb + sd) >> 1;
        n = md_wrap(md_mid_mpy_exact(D1, E1));
        x1 = md_mpyr(A1, E1); E1 = n;
        n = md_wrap(md_mid_mpy_exact(D2, E2));
        x1 = md_wrap(x1 + md_mpyr(A2, E2)); E2 = n;
        out[i] = md_w24(md_mpyr(x1, x0));
    }
    s->phase_b = md_acc_make((int32_t)bh, bl); s->phase_d = md_acc_make((int32_t)dh, dl);
    s->phase_f = md_acc_make((int32_t)fh, fl); s->phase_11 = md_acc_make((int32_t)eh, el);
    s->mod_d = md_acc_make((int32_t)mdh, mdl); s->mod_11 = md_acc_make((int32_t)meh, mel);
    s->fb = md_w24(FB); s->env1 = md_w24(E1); s->env2 = md_w24(E2);
}
#endif
