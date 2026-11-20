/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_coefficients.c - mdkit_pi_<voice>_modal_coefficients, word for word
 * mdkit's (the same code in every PI voice with modes, mdkit/src/pi/*): the eight
 * modes' frequency, damping and gain, once a block. Each accumulator mdkit builds is
 * read once, as a word, so it is the limited floor of its products:
 * - word(mpy x, y) = limit(floor(x y / 2^23)); word(asl(mpy x, y, 1)) = limit(floor(x y
 *   / 2^22)): one product (md_mul_shift_exact, the EMAC's exact read of one).
 * - a word on A1 plus products: A + floor(P / 2^24), the word being whole.
 * - 0x7fffff - mpy f, f + mac q, old: 0x7fffff + floor((q old - f f) / 2^23); and
 *   -0x800000 + asl(mpy d, y, 1) - mpy d, d: -0x800000 + floor((2 y d - d d) / 2^23),
 *   two products (2 y is 25 bits, which they take): md_mid_mpy2_exact, which a voice's
 *   `mid` form makes the EMAC's read alone; MD_COEF_EXACT (PI-BD, whose long low modes
 *   amplify any error) keeps md_mid_mpy2_x, exact under every form.
 * The reads are mdkit's (MD_COEF_X, MD_COEF_Y: the voice's modal_x and modal_y).
 * Included by the generated voice in place of the function (tools/md_port.py, NATIVE);
 * MD_COEF names it and MD_COEF_T is the voice's modal state type.
 */
#ifdef MD_COEF_EXACT
#define MD_COEF_PAIR md_mid_mpy2_x
#else
#define MD_COEF_PAIR md_mid_mpy2_exact
#endif
static inline __attribute__((unused)) void MD_COEF(MD_COEF_T *c, uint32_t pitch, uint32_t decay, uint32_t freq_table,
                                                   uint32_t amp_table)
{
    uint32_t *y = c->fraction;
    const int32_t p = md_s(pitch), dec = md_s(decay);
    unsigned i;
    /* mdkit's first two loops as one: each mode's frequency, then its damping from it */
    for (i = 0; i < 8; ++i) {
#if defined(MD_AP_STRUCT) && defined(MD_PI_SKIP_MODES)
        if ((MD_PI_SKIP_MODES >> i) & 1u) continue;   /* a mode the filter leaves out */
#endif
        const int32_t f = md_sat(md_mul_shift_exact(md_s(MD_COEF_Y(c, freq_table + i)), p, 23));
        y[0xb4 + i] = md_w24(f);
        const int32_t old = md_sat(md_mul_shift_exact(-f, f, 23));
        const int32_t q = md_sat(md_mul_shift_exact(old, 0x155555, 23));
        y[0xa0 + i] = md_w24(md_sat(0x7fffff + MD_COEF_PAIR(-f, f, q, old)));
    }
    for (i = 0; i < 8; ++i) {
#if defined(MD_AP_STRUCT) && defined(MD_PI_SKIP_MODES)
        if ((MD_PI_SKIP_MODES >> i) & 1u) continue;
#endif
        const uint32_t idx = md_w24(md_sat(md_mul_shift_exact(md_s(MD_COEF_X(c, amp_table - 8 + i)), dec, 23)));
        /* the damping table's word straight from its reader (MD_COEF_DAMP, the voice's <x>_damp:
         * what modal_x reaches at 0x142949 + idx, idx < 512); elsewhere through modal_x */
        const int32_t d = md_s(idx < 512u ? MD_COEF_DAMP(idx) : MD_COEF_X(c, 0x142949 + idx)), ya = md_s(y[0xa0 + i]);
        const int32_t first = md_sat(-0x800000 + md_mul_shift_exact(d, ya, 22));
        const int32_t normalized = md_sat(-0x800000 + MD_COEF_PAIR(-d, d, d, 2 * ya));
        const int32_t gain = md_sat(md_mul_shift_exact(md_s(MD_COEF_X(c, amp_table + i)), normalized, 22));
        y[0x64 + 4 * i] = md_w24(md_sat(md_mul_shift_exact(-d, d, 23)));
        y[0x65 + 4 * i] = md_w24(first);
        y[0x66 + 4 * i] = md_w24(gain);
    }
}
#undef MD_COEF_PAIR
#undef MD_COEF_EXACT
#undef MD_COEF
#undef MD_COEF_T
#undef MD_COEF_X
#undef MD_COEF_Y
#undef MD_COEF_DAMP
