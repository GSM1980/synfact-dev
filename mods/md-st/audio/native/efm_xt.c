/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/efm_xt.c - EFM-XT's xt_core and xt_tail, word for word mdkit's
 * (mdkit/src/efm/xt/mdkit_efm_xt.c), in 32-bit integers: EFM-CP's operators
 * (native/efm_cp.c, which says why each step is the same) with the slot words,
 * and a third stage, operator 2's level: out = mid(mpyr(op2, sine)), op2 decaying
 * by its own mpy. xt_tail is the resonance with a rounded MAC (macr): the sum's
 * high word and its 23 bits below, md_round_sum2_less.
 * MD_EFM_XT_PART 0 is xt_core, 1 xt_tail. Included by the generated voice in place
 * of each function (tools/md_port.py, NATIVE). tests/test_md_st.py compares every word.
 */
#if MD_EFM_XT_PART == 0
static void xt_core(const uint32_t y[9], uint32_t *fb, uint32_t *op1,
                    uint32_t *op2, uint64_t *l1, uint64_t *l2, uint64_t *l3,
                    uint32_t out[32])
{
    const int32_t R = md_s(y[1]), D2 = md_s(y[2]), MR = md_s(y[3]), MA = md_s(y[4]),
                  D1 = md_s(y[5]), MI = md_s(y[6]), FBC = md_s(y[8]);
    int32_t FB = md_s(*fb), O1 = md_s(*op1), O2 = md_s(*op2);
    uint32_t ph1 = (uint32_t)MD_T(*l1) & 0xffffffu, pl1 = MD_L(*l1),
             ph2 = (uint32_t)MD_T(*l2) & 0xffffffu, pl2 = MD_L(*l2),
             t12 = (uint32_t)MD_T(*l3) & 0xffffffu, l12l = MD_L(*l3);
    for (unsigned i = 0; i < 32; ++i) {
        int32_t hi, lo, y1, x0, x1, n;
        uint32_t sl, st, idx, s1l;
        n = md_wrap(md_mid_mpy_exact(FBC, FB));
        y1 = md_wrap(md_mul_shift_exact(FB, R, 20) + R);
        FB = n;
        s1l = md_mul_floor48(y1, MR, 4, &hi);
        sl = pl1 + l12l; st = ph1 + t12 + (sl >> 24); sl &= 0xffffffu;
        idx = st & 0x7fffu;
        lo = (int32_t)sl - (int32_t)l12l + (int32_t)s1l;
        st = (uint32_t)((int32_t)idx - (int32_t)t12 + hi + (lo >> 24));
        ph1 = st & 0xffffffu; pl1 = (uint32_t)lo & 0xffffffu;
        x0 = md_s(xt_sine((uint16_t)idx) & 0xffffffu);
        l12l = md_mul_floor48(MI, x0, 7, &hi); t12 = (uint32_t)hi & 0xffffffu;
        n = md_wrap(md_mid_mpy_exact(D1, O1));
        x0 = md_wrap(md_mpyr(O1, x0)); O1 = n;
        x1 = md_wrap(md_mpyr(x0, y1));
        idx = ph2;
        {
            const uint32_t ql = md_mul_floor48(x1, MA, 4, &hi), l2 = pl2 + ql + (((uint32_t)y1 & 255u) << 16);
            ph2 = ((uint32_t)((int32_t)ph2 + hi + (y1 >> 8) + (int32_t)(l2 >> 24))) & 0x7fffu;
            pl2 = l2 & 0xffffffu;
        }
        x0 = md_s(xt_sine((uint16_t)idx) & 0xffffffu);
        n = md_wrap(md_mid_mpy_exact(D2, O2));
        out[i] = md_w24(md_mpyr(O2, x0)); O2 = n;
    }
    *fb = md_w24(FB); *op1 = md_w24(O1); *op2 = md_w24(O2);
    *l1 = md_acc_make((int32_t)ph1, pl1); *l2 = md_acc_make((int32_t)ph2, pl2); *l3 = md_acc_make((int32_t)t12, l12l);
}
#elif MD_EFM_XT_PART == 1
static void xt_tail(const uint32_t in[32], uint32_t out[32], uint32_t coef,
                    uint32_t *history, uint32_t *delay)
{
    const uint32_t gain = mdkit_mid(mdkit_add(mdkit_asr(mdkit_from24(coef), 1), mdkit_from24(0x400000)));
    const int32_t g = md_s(gain), c = md_s(coef);
    int32_t hist = md_s(*history), previous = md_s(*delay);
    for (unsigned i = 0; i < 32; ++i) {
        const int32_t x = md_s(in[i]), old = hist;
        hist = md_wrap(md_mid_mpy_exact(x, g));
        previous = md_round_sum2_less(x, g, previous, c, old);
        out[i] = md_w24(previous);
    }
    *history = md_w24(hist);
    *delay = md_w24(previous);
}
#endif
#undef MD_EFM_XT_PART
