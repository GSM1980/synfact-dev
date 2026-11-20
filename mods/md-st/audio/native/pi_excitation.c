/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_excitation.c - mdkit_pi_<voice>_modal_excitation, word for word mdkit's (the
 * same code in every PI voice, mdkit/src/pi/*): the excitation read from the ROM at a
 * 48-bit phase, between two words.
 *
 * - The phase is its two 24-bit halves, t (high, signed) and l (A0); a step adds them with
 *   the carry, as md_acc_add does. to48 limits t to a word; the complement 0x7fffffffffff
 *   - phase has the low half 0xffffff - l (no borrow: l <= 0xffffff).
 * - The end test, s56(phase) >= s56(from24(end)), is t >= end, as l >= 0.
 * - A sample is acc_to_word(mpy_su(first, ~l) + mpy_su(next, l)): the word of two products
 *   of a word by a 24-bit unsigned, floor((x y + u v) / 2^23) limited (md_mid_mpy2_x, exact
 *   under every MD_AP_*: MD_AP_MID's form there measured -72 dBFS of error in PI-BD).
 *   The reads keep mdkit's order (a ROM read may be of the workspace).
 * Included by the generated voice in place of the function (tools/md_port.py, NATIVE):
 * MD_EXC names it, MD_EXC_T is the voice's modal type and MD_EXC_X its modal_x.
 * tests/test_md_st.py compares every word with mdkit's.
 */
/* MD_EXC_BASE, MD_EXC_COUNT, MD_EXC_SAMPLE (md_port.py PI_SAMPLES): a read in the voice's
 * sample table is that table's word (modal_x -> rom -> sample, the range checks
 * before it never match there); any other address goes the whole way. */
/* the interpolation's two products: exact (MD_EXC_EXACT: PI-BD), else md_mid_mpy2_exact, which a
 * voice's `mid` form makes the EMAC's read alone */
#ifdef MD_EXC_EXACT
#define MD_EXC_PAIR md_mid_mpy2_x
#else
#define MD_EXC_PAIR md_mid_mpy2_exact
#endif
#ifdef MD_EXC_BASE
/* MD_EXC_GAP, MD_EXC_GAP_COUNT (md_port.py PI_SAMPLE_GAP): the ROM's empty words past the
 * sample, where modal_x gives 0 */
#define MD_EXC_READ(c, a) ((uint32_t)(a) - MD_EXC_BASE < MD_EXC_COUNT ? MD_EXC_SAMPLE((uint32_t)(a) - MD_EXC_BASE) : \
                           (uint32_t)(a) - MD_EXC_GAP < MD_EXC_GAP_COUNT ? 0u : MD_EXC_X(c, (a)))
#else
#define MD_EXC_READ(c, a) MD_EXC_X(c, (a))
#endif
static inline __attribute__((unused)) void MD_EXC(MD_EXC_T *c, uint32_t *hi, uint32_t *lo, uint64_t step, uint32_t end)
{
    int32_t t = md_s(*hi);
    uint32_t l = *lo & 0xffffffu, first;
    const int32_t st = MD_T(step);
    const uint32_t sl = MD_L(step);
    unsigned i;
    c->work[64] = 0x7fffff;
    c->fraction[64] = 0xffffff;
    /* t moves by st or st + 1 a sample: when its 32 steps cannot leave the 24 bits
     * either way, no word is limited and the loop needs no test (the same words) */
    if (st >= -0x10000 && st <= 0x10000 && t >= -0x800000 + 0x210000 && t <= 0x7fffff - 0x210000)
        for (i = 0; i < 32; ++i) {
            l += sl; t += st + (int32_t)(l >> 24); l &= 0xffffffu;
            c->work[i] = (uint32_t)t & 0xffffffu; c->fraction[i] = l;
            c->fraction[32 + i] = 0xffffffu - l;
        }
    else
        for (i = 0; i < 32; ++i) {
            l += sl; t += st + (int32_t)(l >> 24); l &= 0xffffffu;
            if (t < -0x800000) { c->work[i] = 0x800000u; c->fraction[i] = 0; }
            else if (t > 0x7fffff) { c->work[i] = 0x7fffffu; c->fraction[i] = 0xffffffu; }
            else { c->work[i] = (uint32_t)t & 0xffffffu; c->fraction[i] = l; }
            c->fraction[32 + i] = 0xffffffu - l;
        }
    {
        const int32_t e = md_s(end);
        if (t >= e) { t = e; l = 0; }
    }
    *hi = (uint32_t)t & 0xffffffu;
    *lo = l;
    first = MD_EXC_READ(c, c->work[0]);
    for (i = 0; i < 32; ++i) {
        const uint32_t index = c->work[i];
        const uint32_t next = MD_EXC_READ(c, index + 1);
        const uint32_t word = md_w24(md_sat(MD_EXC_PAIR(md_s(first), (int32_t)c->fraction[32 + i], md_s(next), (int32_t)c->fraction[i])));
        first = MD_EXC_READ(c, c->work[i + 1]);
        c->work[64 + i] = word;
    }
}
#undef MD_EXC_PAIR
#undef MD_EXC_EXACT
#undef MD_EXC_READ
#undef MD_EXC_BASE
#undef MD_EXC_COUNT
#undef MD_EXC_SAMPLE
#undef MD_EXC_GAP
#undef MD_EXC_GAP_COUNT
#undef MD_EXC
#undef MD_EXC_T
#undef MD_EXC_X
