/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_sd_half_delays.c - PI-SD's drum_delays at half rate (MD_PI_HALF; see
 * pi_sd_half_block.c). Sixteen samples a block, each two of mdkit's:
 * - the excitation filter and the three lines' one-poles take their two-step gains,
 *   2 g - g^2 (md_pd_gain2); the lines were at half rate already (MD_PD_WHOLE: delays
 *   123, 129 and 166 of these samples), now without the words between;
 * - the noise: eight of mdkit's pairs a block, sixteen words; its leaky sum (y += n - 0.3 y)
 *   takes the pole 0.7 squared, y += 1.7 n - 0.51 y: the input's 1.7 keeps its gain at DC
 *   (1 / 0.3 = 1.7 / 0.51); 1.2, for the power spread over half the band, left PI-RS
 *   1.5 dB low and PI-SD 0.8, against 1.3 and 0.6 with 1.7;
 * - the rattle and the lines' writes per sample as mdkit's, X 0x60.. the excitation filter's
 *   words a sample late (mdkit's carry), the carry in from the bus's last word (X 0x4f here,
 *   X 0x5f there).
 * Included by pi_delays.c in place of drum_delays.
 */
/* a line's 16 words: with MD_PD_WHOLE (PI-SD) its delay in whole words (delay + 1), else
 * (PI-RS) the half word from two taps' mean (delay + 0.5), as md_pd_half's two forms */
static __attribute__((noinline)) int32_t md_pd_q(int32_t a, int32_t g2, const uint32_t *ring, unsigned position, unsigned delay, uint32_t *out)
{
    unsigned j;
#ifdef MD_PD_WHOLE
    unsigned at = position - delay - 1u;
    for (j = 0; j < 16u; ++j, ++at) {
        const int32_t word = md_sat(a >> 6);
        out[j] = md_w24(word);
        a += md_ar1(g2, md_s(ring[at & 511u]) - word) + (int32_t)(j & 1u);
    }
#else
    unsigned at = position - delay;
    for (j = 0; j < 16u; ++j, ++at) {
        const int32_t word = md_sat(a >> 6), x = (md_s(ring[at & 511u]) + md_s(ring[(at - 1u) & 511u])) >> 1;
        out[j] = md_w24(word);
        a += md_ar1(g2, x - word) + (int32_t)(j & 1u);
    }
#endif
    return a;
}
static __attribute__((unused)) void drum_delays(MD_PI_T *s, uint32_t excitation_gain, uint32_t ring_gain, uint32_t noise_gain)
{
    uint32_t *w = s->body.work, *y = s->body.fraction;
    const int32_t eg2 = md_pd_gain2(md_s(excitation_gain)), rg2 = md_pd_gain2(md_s(ring_gain));
    const int32_t ng2 = md_pd_gain2(md_s(noise_gain));
    unsigned i;
    {
        const uint32_t *in = s->variant ? y + 0x40 : w + 0x40;
        int32_t excitation = MD_PD_FROM24(s->source_filter);
        for (i = 0; i < 16; ++i) {
            const int32_t old = md_sat(excitation >> 6);
            w[0x60 + i] = md_w24(old);
            excitation += md_ar1(eg2, md_s(in[i]) - old) + (int32_t)(i & 1u);
        }
        s->source_filter = md_w24(md_sat(excitation >> 6));
    }
    s->delay_filter[0] = md_w24(md_sat(md_pd_q(MD_PD_FROM24(s->delay_filter[0]), rg2, s->delay_x[0], s->delay_position[0], 122, w + 0x80) >> 6));
    s->delay_filter[1] = md_w24(md_sat(md_pd_q(MD_PD_FROM24(s->delay_filter[1]), rg2, s->delay_x[1], s->delay_position[1], 128, w + 0xa0) >> 6));
    {
        md_acc sum = md_acc_from24(s->noise_a);
        uint32_t next = s->noise_b;
        for (i = 0; i < 8; ++i) {
            uint32_t old = next;
            sum = md_acc_add(sum, md_acc_from24(next));
            y[2 * i] = old;
            next = md_acc_mid(sum);
            y[2 * i + 1] = next;
            next = (old + next) & 0xffffffu;
        }
        s->noise_a = next; s->noise_b = md_acc_mid(sum); y[0xfa] = md_acc_mid(sum);
    }
    s->delay_filter[2] = md_w24(md_sat(md_pd_q(MD_PD_FROM24(s->delay_filter[2]), ng2, s->delay_y, s->delay_position[2], 165, w + 0xc0) >> 6));
    {
        uint32_t residual, rattle, small;
        int32_t noise = MD_PD_FROM24(s->noise_filter);
        for (i = 0; i < 16; ++i) {
            const int32_t before = md_sat(noise >> 6);
            const int32_t n = md_s(y[i]);
            y[i] = md_w24(before);
            noise += (int32_t)((uint32_t)n << 6) + md_ar1(0x59999a, n) + md_ar1(-0x4147ae, before) + (int32_t)(i & 1u);
        }
        residual = y[15];
        s->noise_filter = md_w24(md_sat(noise >> 6));
        {
            int32_t absnoise = md_s(y[0xfa]);
            if (absnoise < 0) absnoise = -absnoise;
            if (absnoise >= md_s(s->rattle_threshold)) { residual = 0x7fffffu; s->rattle_gate = residual; }
        }
        rattle = md_w24(md_word_mpy_exact(md_s(s->rattle_threshold), md_s(residual)));
        rattle = md_w24(md_word_mpy_exact(md_s(s->parameters[5]), md_s(rattle)));
        small = md_w24(md_word_mpy_exact(0x266666, md_s(rattle)));
        if (!s->variant) {
            const int32_t sm = md_s(small);
            for (i = 0; i < 16; ++i) {
                const int32_t r = md_mul_shift_exact(sm, md_s(y[i]), 23);
                w[0x40 + i] = md_w24(md_sat(md_s(w[0x40 + i]) + r));
                w[0x60 + i] = md_w24(md_sat(md_s(w[0x60 + i]) + r));
            }
        } else {
            const int32_t large = md_word_mpy_exact(0x59999a, md_s(rattle)), sm = md_s(small);
            uint32_t carry = md_s(w[0x4f]) & 0xffffffu;
            for (i = 0; i < 16; ++i) {
                const int32_t yi = md_s(y[i]);
                w[0x5f + i] = carry;
                w[0x40 + i] = md_w24(md_sat(2 * md_s(y[0x40 + i]) + md_mul_shift_exact(large, yi, 23)));
                carry = md_w24(md_sat(md_s(w[0x60 + i]) + md_mul_shift_exact(sm, yi, 23)));
            }
            w[0x6f] = carry;
        }
    }
    {
        const int32_t gain = md_s(w[0xfb]), cross = md_s(w[0xfc]);
        const unsigned first = s->delay_position[0], second = s->delay_position[1], third = s->delay_position[2];
        for (i = 0; i < 16; ++i) {
            const int32_t a = md_s(w[0x60 + i]), ra = md_s(w[0xa0 + i]), rc = md_s(w[0xc0 + i]);
            const int32_t shared = md_ar1(a, 0x7eb852);
            s->delay_x[1][(second + i) & 511] = md_w24(md_sat((shared + md_ar1(cross, ra + rc)) >> 6));
            s->delay_x[0][(first + i) & 511] = md_w24(md_sat((shared + md_ar1(cross, rc - ra)) >> 6));
            s->delay_y[(third + i) & 511] = md_w24(md_sat((shared + md_ar1(gain, md_s(w[0x80 + i]))) >> 6));
        }
        s->delay_position[0] = (first + 16) & 511; s->delay_position[1] = (second + 16) & 511;
        s->delay_position[2] = (third + 16) & 511;
    }
}
