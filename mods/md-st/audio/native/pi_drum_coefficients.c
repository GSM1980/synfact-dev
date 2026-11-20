/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_drum_coefficients.c - drum_coefficients of PI-SD and PI-RS, word for word
 * mdkit's (mdkit/src/pi/sd/mdkit_pi_sd.c, the same in PI-RS): the eight modes' frequency,
 * damping and gain, once a block, as native/pi_coefficients.c makes them for the modal
 * voices (each accumulator read once as a word: the limited floor of its products).
 *
 * - Frequencies: modes 0 to 3 from the pitch, which the tension envelope moves every
 *   block; modes 4 to 7 from PTCH (parameter 0). Dampings from DEC (parameter 1), gains
 *   of modes 4 to 7 from parameter 7. So modes 4 to 7's words depend on the controls and
 *   the variant alone: they are made again only when those change (s->coef_key, which
 *   init's zeroing leaves unmade; tools/md_port.py, PI_COEF_KEY). Nothing else writes
 *   their words (Y 0x64.., 0xa0.., 0xb4..). The gains' step through X 0..7 is not kept:
 *   the block writes X 0..31 before it reads them.
 * - word(asl(mpy d, a, 1) - 0x400000 << 1) (the variant) and word(asl(mpy d, a, 1) +
 *   from24(0x800000)) are the same word, -0x800000 + floor(d a / 2^22) limited:
 *   from24(0x800000) is -0x800000.
 * - The carry out (the last mode's next amplitude by DEC) is mdkit's 48-bit product:
 *   drum_modes' exact form starts from it.
 * MD_AP_STRUCT with MD_PI_SKIP_MODES (tools/md_port.py PI_STRUCT): modes drum_modes leaves out.
 * Included by the generated voice in place of the function (tools/md_port.py, NATIVE);
 * MD_PI_T is the voice's type, MD_PI_X its modal_x and MD_PI_Y its modal_y.
 * tests/test_md_st.py compares every word with mdkit's.
 */
static inline void md_pdc_mode(MD_PI_T *s, unsigned i, uint32_t base, int32_t frequency, int32_t amplitude)
{
    uint32_t *y = s->body.fraction;
    const int32_t f = md_sat(md_mul_shift_exact(md_s(MD_PI_X(&s->body, base + i)), frequency, 23));
    const int32_t old = md_sat(md_mul_shift_exact(-f, f, 23));
    const int32_t q = md_sat(md_mul_shift_exact(old, 0x155555, 23));
    const int32_t ya = md_sat(0x7fffff + md_mid_mpy2_x(-f, f, q, old));
    const uint32_t idx = md_w24(md_sat(md_mul_shift_exact(md_s(MD_PI_X(&s->body, base + 8 + i)), md_s(s->parameters[1]), 23)));
    const int32_t d = md_s(MD_PI_X(&s->body, 0x142949 + idx));
    const int32_t first = md_sat(-0x800000 + md_mul_shift_exact(d, ya, 22));
    const int32_t normalized = md_sat(-0x800000 + md_mid_mpy2_x(-d, d, d, 2 * ya));
    y[0xb4 + i] = md_w24(f);
    y[0xa0 + i] = md_w24(ya);
    y[0x64 + 4 * i] = md_w24(md_sat(md_mul_shift_exact(-d, d, 23)));
    y[0x65 + 4 * i] = md_w24(first);
    y[0x66 + 4 * i] = md_w24(md_sat(md_mul_shift_exact(amplitude, normalized, 22)));
}
static __attribute__((unused)) uint64_t drum_coefficients(MD_PI_T *s, uint32_t pitch)
{
    const uint32_t base = s->variant ? 0x142f33 : 0x144c9a;
    const uint32_t *p = s->parameters;
    unsigned i;
    for (i = 0; i < 4; ++i) {
#if defined(MD_AP_STRUCT) && defined(MD_PI_SKIP_MODES)
        if ((MD_PI_SKIP_MODES >> i) & 1u) continue;
#endif
        md_pdc_mode(s, i, base, md_s(pitch), md_s(MD_PI_Y(&s->body, base + 16 + i)));
    }
    if (s->coef_key[0] != 1u + s->variant || s->coef_key[1] != p[0] || s->coef_key[2] != p[1] || s->coef_key[3] != p[7]) {
        s->coef_key[0] = 1u + s->variant; s->coef_key[1] = p[0]; s->coef_key[2] = p[1]; s->coef_key[3] = p[7];
        for (i = 4; i < 8; ++i) {
#if defined(MD_AP_STRUCT) && defined(MD_PI_SKIP_MODES)
            if ((MD_PI_SKIP_MODES >> i) & 1u) continue;
#endif
            md_pdc_mode(s, i, base, md_s(p[0]),
                        md_sat(md_mul_shift_exact(md_s(p[7]), md_s(MD_PI_Y(&s->body, base + 16 + i)), 23)));
        }
    }
    return md_acc_mpy_x(MD_PI_X(&s->body, base + 16), p[1]);
}
#undef MD_PI_X
#undef MD_PI_Y
#undef MD_PI_T
