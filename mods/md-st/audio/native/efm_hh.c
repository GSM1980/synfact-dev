/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/efm_hh.c - EFM-HH's hh_core and hh_resonance, word for word mdkit's
 * (mdkit/src/efm/hh/mdkit_efm_hh.c), in 32-bit integers, with EFM-CP's and EFM-CB's
 * steps (native/efm_cp.c, efm_cb.c):
 * - operator A's step, asr(mpy(pitch, MFRQ), 5), and C's, asr(from24(inc), 8) =
 *   inc * 2^16, are the same all block: made once.
 * - A's and B's phases add their self modulation and take it off around the mask;
 *   C's reads its old phase, then steps. Only the 24 bits kept are read, so the
 *   phases are added modulo 2^24.
 * - mpy_su(env_b, scale) read as a word is the limit of floor(env_b scale / 2^23),
 *   scale unsigned (one product, |scale| < 2^24).
 * - hh_resonance rounds every sample's MAC (macr): md_round_sum2_less.
 * MD_EFM_HH_PART 0 is hh_resonance, 1 hh_core. Included by the generated voice in
 * place of each function (tools/md_port.py, NATIVE). tests/test_md_st.py compares
 * every word.
 */
#if MD_EFM_HH_PART == 0
static void hh_resonance(const uint32_t in[32], uint32_t out[32], uint32_t coefficient,
                         uint32_t *history, uint32_t *delay)
{
    const uint32_t gain = mdkit_mid(mdkit_add(mdkit_asr(mdkit_from24(coefficient), 1), mdkit_from24(0x400000)));
    const int32_t g = md_s(gain), c = md_s(coefficient);
    int32_t hist = md_s(*history), previous = md_s(*delay);
    for (unsigned i = 0; i < 32; ++i) {
        const int32_t x = md_s(in[i]), old = hist;
        hist = md_wrap(md_mid_mpy_exact(x, g));
        previous = md_round_sum2_less(x, g, previous, c, old);
        out[i] = md_w24(previous);
    }
    *history = md_w24(hist); *delay = md_w24(previous);
}
#elif MD_EFM_HH_PART == 1 && defined(MD_AP_FM)
/* MD_AP_FM: the three operators with 32-bit phases (efm_fm.h): A reads phase +
 * modulation and steps by PTCH * MFRQ / 16; B steps by PTCH * 2^16 + floor(x1 MOD / 16)
 * (48-bit units); C reads its phase, then steps by its increment * 2^16 */
#include "efm_fm.h"
static void hh_core(mdkit_efm_hh *s, uint32_t ring[32])
{
    const int32_t PA = md_s(s->op_a_pitch), DB = md_s(s->env_b_decay), MF = md_s(s->op_a_mod_freq), FI = md_s(s->fm_index),
                  AA = md_s(s->env_a_amp), FA = md_s(s->op_a_fb_index), FBB = md_s(s->op_b_fb_index),
                  CA = md_s(s->op_c_amp), CO = md_s(s->op_c_offset), CI = md_s(s->op_c_phase_inc);
    const int32_t CS = (int32_t)(s->op_c_scale & 0xffffffu);
    uint32_t d = md_fm_from48(s->phase_d), f = md_fm_from48(s->phase_f), e = md_fm_from48(s->phase_11),
             ma = md_fm_from48(s->mod_a), mb = md_fm_from48(s->mod_b);
    int32_t EA = md_s(s->env_a), EB = md_s(s->env_b);
    const uint32_t sa = md_fm_step(PA, MF, 4), sc = (uint32_t)CI << 9;
    for (unsigned i = 0; i < 32; ++i) {
        int32_t x0, x1, y0, y1, a, b, c, n;
        /* A */
        a = md_fm_sine(d + ma); d += sa;
        ma = md_fm_step(FA, a, 7);
        n = md_wrap(md_mid_mpy_exact(AA, EA));
        x0 = md_wrap(md_mpyr(EA, a)); EA = n;
        x1 = md_wrap(md_mpyr(x0, PA));
        /* B */
        b = md_fm_sine(f + mb); f += md_fm_step(x1, FI, 4) + ((uint32_t)PA << 9);
        mb = md_fm_step(FBB, b, 7);
        y0 = md_sat(md_mid_mpy_exact(EB, CS));
        n = md_wrap(md_mid_mpy_exact(DB, EB)); EB = n;
        y1 = md_wrap(md_mpyr(y0, b));
        /* C */
        c = md_fm_sine(e); e += sc;
        x0 = md_sat(md_mpyr(CA, c) + CO);
        ring[i] = md_w24(md_mpyr(x0, y1));
    }
    s->phase_d = md_fm_to48(d); s->phase_f = md_fm_to48(f); s->phase_11 = md_fm_to48(e);
    s->mod_a = md_fm_to48(ma); s->mod_b = md_fm_to48(mb);
    s->env_a = md_w24(EA); s->env_b = md_w24(EB);
}
#elif MD_EFM_HH_PART == 1
/* t = (phase + mod) & 0x7fff in A1; phase = t - mod + step (sh * 2^24 + sl) */
static inline uint32_t efm_hh_modulated(uint32_t *ph, uint32_t *pl, uint32_t mh, uint32_t ml, int32_t sh, uint32_t sl)
{
    uint32_t l = *pl + ml, t = *ph + mh + (l >> 24), idx;
    int32_t lo;
    l &= 0xffffffu;
    idx = t & 0x7fffu;
    lo = (int32_t)l - (int32_t)ml + (int32_t)sl;
    *ph = (uint32_t)((int32_t)idx - (int32_t)mh + sh + (lo >> 24)) & 0xffffffu;
    *pl = (uint32_t)lo & 0xffffffu;
    return idx;
}
static void hh_core(mdkit_efm_hh *s, uint32_t ring[32])
{
    const int32_t PA = md_s(s->op_a_pitch), DB = md_s(s->env_b_decay), MF = md_s(s->op_a_mod_freq), FI = md_s(s->fm_index),
                  AA = md_s(s->env_a_amp), FA = md_s(s->op_a_fb_index), FBB = md_s(s->op_b_fb_index),
                  CA = md_s(s->op_c_amp), CO = md_s(s->op_c_offset), CI = md_s(s->op_c_phase_inc);
    const int32_t CS = (int32_t)(s->op_c_scale & 0xffffffu);
    uint32_t dh = (uint32_t)MD_T(s->phase_d) & 0xffffffu, dl = MD_L(s->phase_d),
             fh = (uint32_t)MD_T(s->phase_f) & 0xffffffu, fl = MD_L(s->phase_f),
             eh = (uint32_t)MD_T(s->phase_11) & 0xffffffu, el = MD_L(s->phase_11),
             mah = (uint32_t)MD_T(s->mod_a) & 0xffffffu, mal = MD_L(s->mod_a),
             mbh = (uint32_t)MD_T(s->mod_b) & 0xffffffu, mbl = MD_L(s->mod_b);
    int32_t EA = md_s(s->env_a), EB = md_s(s->env_b), sah, hi;
    const uint32_t sal = md_mul_floor48(PA, MF, 4, &sah), scl = ((uint32_t)CI & 255u) << 16;
    const int32_t sch = CI >> 8;
    for (unsigned i = 0; i < 32; ++i) {
        int32_t x0, x1, y0, y1, sa, sb, sc, n;
        /* A */
        sa = md_s(hh_sine((uint16_t)efm_hh_modulated(&dh, &dl, mah, mal, sah, sal)));
        mal = md_mul_floor48(FA, sa, 7, &hi); mah = (uint32_t)hi & 0xffffffu;
        n = md_wrap(md_mid_mpy_exact(AA, EA));
        x0 = md_wrap(md_mpyr(EA, sa)); EA = n;
        x1 = md_wrap(md_mpyr(x0, PA));
        /* B: stepped by PTCH * 2^16 + floor(x1 MOD / 16) */
        {
            const uint32_t ql = md_mul_floor48(x1, FI, 4, &hi), l = ql + (((uint32_t)PA & 255u) << 16);
            sb = md_s(hh_sine((uint16_t)efm_hh_modulated(&fh, &fl, mbh, mbl, hi + (PA >> 8) + (int32_t)(l >> 24), l & 0xffffffu)));
        }
        mbl = md_mul_floor48(FBB, sb, 7, &hi); mbh = (uint32_t)hi & 0xffffffu;
        y0 = md_sat(md_mid_mpy_exact(EB, CS));
        n = md_wrap(md_mid_mpy_exact(DB, EB)); EB = n;
        y1 = md_wrap(md_mpyr(y0, sb));
        /* C: the old phase, then its step */
        {
            const uint32_t old = eh, l = el + scl;
            eh = (eh + (uint32_t)sch + (l >> 24)) & 0x7fffu; el = l & 0xffffffu;
            sc = md_s(hh_sine((uint16_t)old));
        }
        x0 = md_sat(md_mpyr(CA, sc) + CO);
        ring[i] = md_w24(md_mpyr(x0, y1));
    }
    s->phase_d = md_acc_make((int32_t)dh, dl); s->phase_f = md_acc_make((int32_t)fh, fl);
    s->phase_11 = md_acc_make((int32_t)eh, el);
    s->mod_a = md_acc_make((int32_t)mah, mal); s->mod_b = md_acc_make((int32_t)mbh, mbl);
    s->env_a = md_w24(EA); s->env_b = md_w24(EB);
}
#endif
#undef MD_EFM_HH_PART
