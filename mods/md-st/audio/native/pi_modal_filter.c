/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_modal_filter.c - mdkit_pi_<voice>_modal_filter, word for word mdkit's
 * (the same code in every PI voice, mdkit/src/pi/*): the eight resonant modes.
 *
 * The coefficient pointer is a DSP modulo register over three words (wrap step 1,
 * modulus 3, from 0x64 + 4 mode, a multiple of 4), read three times a sample: it
 * comes back to its start each sample, so the three coefficients are fixed words
 * Y0, Y1, Y2. A sample's three MACs into a (x1 Y0, old Y1, Y2 x0, old = a's word
 * before them) are one exact sum (md_acc_mac3); a keeps its A0 from block to
 * block, so it stays an md_acc. b and the workspace keep mdkit's order.
 * Included by the generated voice in place of the function (tools/md_port.py,
 * NATIVE); MD_MODAL names it and MD_MODAL_T is the voice's modal state type.
 */
#if defined(MD_AP_ACC) && !defined(MD_MODAL_EXACT)
/* MD_AP_ACC (MD_ST_APPROX): a held as one word, s = floor(a / 2^18) (its high word and
 * A0's top 6 bits), stepped by the EMAC's read of the three products (md_ar3, the same
 * unit) and half a unit a sample (one every other sample) so that the floor has no mean.
 * a's high word is s >> 6; |a| < 2^25 words, as a block's start leaves it under one word
 * (mdkit stores acc_to_word(a)). Not mdkit's word for word: tests/md_st_voice_ser.c.
 * MD_MODAL_EXACT (PI-BD: its long low modes took this at -72 dBFS) keeps the exact form.
 * The bus stays limited after each mode on the mode's unlimited word, as mdkit's: PI-ML
 * at full level clips its partial sums, and its modes pass full scale on their own
 * (limited once at the end: -16 dBFS of error; adding the limited word: -46 dBFS).
 * MD_AP_STRUCT (a voice judged by its spectrum, tools/md_port.py PI_STRUCT): the modes in
 * MD_PI_SKIP_MODES, the ones that weigh least on its sound, are left out. */
static inline __attribute__((unused)) void MD_MODAL(MD_MODAL_T *c)
{
    uint32_t *w = c->work, *y = c->fraction;
    int32_t in[32], bus[32];           /* X 0x60.. (the EMAC's y operands) and 0x40.., read once */
    unsigned i;
    for (i = 0; i < 32; ++i) { in[i] = MD_EY(md_s(w[0x60 + i])); bus[i] = md_s(w[0x40 + i]); }
    for (unsigned mode = 0; mode < 8; ++mode) {
#if defined(MD_AP_STRUCT) && defined(MD_PI_SKIP_MODES)
        if ((MD_PI_SKIP_MODES >> mode) & 1u) continue;
#endif
        const unsigned coef = 0x64 + mode * 4;
        /* the operands shifted once (md_ar3s); two samples a turn, the second taking the half unit */
        const int32_t y0 = MD_EY(md_s(y[coef])), y1 = MD_EY(md_s(y[coef + 1])), y2 = MD_EX(md_s(y[coef + 2]));
        int32_t s = (int32_t)((uint32_t)md_s(c->history[mode][0]) << 6) | (int32_t)((c->history[mode][1] & 0xffffffu) >> 18);
        int32_t x1 = MD_EX(md_s(c->history[mode][2]));
        for (i = 0; i < 32; i += 2) {
            const int32_t first = MD_EX(md_sat(s >> 6));
            s += md_ar3s(x1, y0, first, y1, y2, in[i]);
            bus[i] = md_sat(bus[i] + (s >> 6));
            x1 = MD_EX(md_sat(s >> 6));
            s += md_ar3s(first, y0, x1, y1, y2, in[i + 1]) + 1;
            bus[i + 1] = md_sat(bus[i + 1] + (s >> 6));
        }
        c->history[mode][0] = md_w24(md_sat(s >> 6));
        c->history[mode][1] = (((uint32_t)s & 63u) << 18) | 0x20000u;
        c->history[mode][2] = md_w24(x1 >> 8);
    }
    for (i = 0; i < 32; ++i) w[0x40 + i] = md_w24(bus[i]);
}
#else
static inline __attribute__((unused)) void MD_MODAL(MD_MODAL_T *c)
{
    uint32_t *w = c->work, *y = c->fraction;
    for (unsigned mode = 0; mode < 8; ++mode) {
        const unsigned coef = 0x64 + mode * 4;
        const int32_t y0 = md_s(y[coef]), y1 = md_s(y[coef + 1]), y2 = md_s(y[coef + 2]);
        unsigned read = 0x60, write = 0x40;
        md_acc a = md_acc_make(md_s(c->history[mode][0]), c->history[mode][1] & 0xffffffu);
        int32_t x1 = md_s(c->history[mode][2]), x0 = md_s(w[read++]);
        int32_t old = md_s(md_acc_acc_to_word(a));
        a = md_acc_mac3_x(a, x1, y0, old, y1, y2, x0);
        x1 = old;
        md_acc b = md_acc_from24(w[write]);
        for (unsigned i = 0; i < 31; ++i) {
            b = md_acc_add(b, a);
            x0 = md_s(w[read++]);
            old = md_s(md_acc_acc_to_word(a));
            a = md_acc_mac3_x(a, x1, y0, old, y1, y2, x0);
            x1 = old;
            w[write++] = md_acc_acc_to_word(b);
            b = md_acc_from24(w[write]);
        }
        b = md_acc_add(b, a);
        w[write] = md_acc_acc_to_word(b);
        c->history[mode][0] = md_acc_acc_to_word(a);
        c->history[mode][1] = md_acc_low(a);
        c->history[mode][2] = (uint32_t)x1 & 0xffffffu;
    }
}
#endif
#undef MD_MODAL
#undef MD_MODAL_T
