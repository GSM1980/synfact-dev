/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_sd_half_modes.c - PI-SD's drum_modes at half rate (MD_PI_HALF; see
 * pi_sd_half_block.c).
 *
 * A mode is y(n + 1) = a y(n) + b y(n - 1) + g x(n), with a = 1 + c1, b = c0 and g = c2,
 * its poles r e^(+-i theta) (a = 2 r cos theta, b = -r^2). Two steps at once are the poles
 * squared: a' = a^2 + 2 b, b' = -b^2, so c1' = a' - 1 = 2 c1 + c1^2 + 2 b (in (-3, 1): kept
 * as a 32-bit word, shifted once into the EMAC's operand). The gain: driven by the filtered
 * excitation (not an impulse), a mode's peak gain g / ((1 - r) 2 sin theta) has both factors
 * doubled at half rate, so g' = 4 g cos theta, here 2 g a (r near 1; the spectrum test put the
 * body bands 6 dB low with g a). a >= 0 in mdkit's words, so every
 * mode is under 11 kHz and none aliases. Sixteen samples a block as the rest of the voice;
 * the bus's words then make the block's 32 output words, each sample and the mean of it
 * and the one before (X 0x50 keeps the last; the half-rate bus is X 0x40..0x4f).
 * Included by pi_modes.c in place of drum_modes.
 */
static __attribute__((unused)) void drum_modes(MD_PI_T *s, uint64_t carry)
{
    MD_PI_MODAL_T *c = &s->body;
    uint32_t *w = c->work, *y = c->fraction;
    int32_t in[16], bus[16], last;
    unsigned mode, i;
    (void)carry;
    for (i = 0; i < 16; ++i) { in[i] = MD_EY(md_s(w[0x60 + i])); bus[i] = md_s(w[0x40 + i]); }
    for (mode = 0; mode < 8; ++mode) {
        if (mode == 7) w[0x3f] = md_w24(md_sat(bus[15]));
        if (MD_PM_SKIP(mode)) continue;
        const int32_t b = md_s(y[0x64 + 4 * mode]), c1 = md_s(y[0x65 + 4 * mode]), g = md_s(y[0x66 + 4 * mode]);
        const int32_t c1h = 2 * c1 + md_mul_shift_exact(c1, c1, 23) + 2 * b;
        const int32_t bh = -md_mul_shift_exact(b, b, 23), gh = md_sat(2 * (g + md_mul_shift_exact(g, c1, 23)));
        const int32_t c0 = MD_EY(bh), c1s = MD_EY(c1h), c2 = MD_EX(gh);
        int32_t st = (int32_t)((uint32_t)md_s(c->history[mode][0]) << 6) | (int32_t)((c->history[mode][1] & 0xffffffu) >> 18);
        int32_t prev = MD_EX(md_s(c->history[mode][2]));
        for (i = 0; i < 16; i += 2) {
            const int32_t first = MD_EX(md_sat(st >> 6));
            st += md_ar3s(prev, c0, first, c1s, c2, in[i]);
            bus[i] = bus[i] + (st >> 6);
            prev = MD_EX(md_sat(st >> 6));
            st += md_ar3s(first, c0, prev, c1s, c2, in[i + 1]) + 1;
            bus[i + 1] = bus[i + 1] + (st >> 6);
        }
        c->history[mode][0] = md_w24(md_sat(st >> 6));
        c->history[mode][1] = (((uint32_t)st & 63u) << 18) | 0x20000u;
        c->history[mode][2] = md_w24(prev >> 8);
    }
    last = md_s(w[0x50]);
    for (i = 0; i < 16; ++i) {
        const int32_t v = md_sat(bus[i]);
        w[0x40 + i] = md_w24(v);
        s->block[2 * i] = md_w24((last + v) >> 1);
        s->block[2 * i + 1] = md_w24(v);
        last = v;
    }
    w[0x50] = md_w24(last);
}
