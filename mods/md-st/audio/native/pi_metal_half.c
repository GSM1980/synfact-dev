/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_metal_half.c - PI-RC, CC and HH at half rate (MD_PI_HALF under MD_AP_STRUCT;
 * judged by the spectrum and by ear, not mdkit's word for word). Included by each voice's
 * pi_<voice>_half_block.c, whose block() calls these in its voice's order. MD_PM_T is the
 * voice's state type.
 *
 * The voice runs at 22.05 kHz: a block is 16 samples, each two of mdkit's, and the output
 * stage makes the block's 32 words from them (each sample and its mean with the one before,
 * Y 0x1ff keeping the last). Stage by stage, against mdkit's:
 * - noise: half the pairs (the voice's metal_noise);
 * - window: the excitation sample read every other word (the cursor's stride doubled) into
 *   16 words, its budget still counted in mdkit's samples;
 * - excitation: its envelope's decay squared, its step doubled (it is added when the noise
 *   crosses the threshold, half as often);
 * - the three filters, y(n) = (1 + 2 c1) y(n-1) + c0 y(n-2) + c2 (x(n) + x(n+2)) + c3 x(n+1):
 *   their poles squared (as pi_sd_half_modes.c's modes); their zeros, a symmetric pair at
 *   q = c3 / c2, keep their place under 11 kHz (q' = 2 - q^2 when q <= 0) and go to 11 kHz
 *   from above it (q' = 2 when q > 0); the gain keeps the resonance's height:
 *   c2' = 2 a c2 / |a - q| (q <= 0) or 2 (a + q) c2 / a (q > 0), a = 1 + 2 c1;
 * - amplitude: the attack's step doubled, the decay's factor squared;
 * - coupling: two passes of eight (mdkit: four), the lines' strides halved (their delays in
 *   these samples), their one-pole 2 g - g^2;
 * - output: the pulse half as long; the word 2 (C + W) a sample, as mdkit's.
 */
#define MD_MH_N 16u
static inline int32_t md_mh_mul(int32_t x, int32_t y) { return md_mul_shift_exact(x, y, 23); }
static inline int32_t md_mh_scale16(int32_t x, int32_t k16)   /* x * k / 2^16, |k| < 2^20 */
{
    return (int32_t)(((x >> 7) * (k16 >> 3)) >> 6);
}
#include "pi_metal_half_rows.h"
/* a filter row's coefficients for two steps at once, as the EMAC's operands (MD_EY): c0' and
 * c1' from the row's half-rate form (tools/pi_metal_half_rows.py), c2' = k2 c2 and c3' = k3 c2
 * from the coefficients as read (the output stage's gain in them) */
static void md_mh_coeffs(const uint32_t coeff[5], const int32_t row[4], int32_t out[5])
{
    const int32_t c2 = md_s(coeff[2]);
    const int32_t c2h = row[2] ? md_sat(md_mh_scale16(c2, row[2])) : 0;
    const int32_t c3h = md_sat(md_mh_scale16(row[2] ? c2 : md_s(coeff[3]), row[3]));
    out[0] = MD_EY(row[0]); out[1] = MD_EY(row[1]); out[2] = MD_EY(c2h); out[3] = MD_EY(c3h); out[4] = out[2];
}
/* the struct filter (pi_metal.c part 0) over 16 samples with the half-rate coefficients */
static __attribute__((noinline)) void md_mh_filter(MD_PM_T *s, unsigned base, uint32_t *history, uint32_t *fraction, uint32_t *previous, const uint32_t coeff[5], const int32_t row[4])
{
    int32_t k[5];
    uint32_t *w = s->work;
    unsigned output = base + 2, i;
    for (i = 0; i < 5; ++i) s->fraction[40 + i] = coeff[i];
    md_mh_coeffs(coeff, row, k);
    {
        const int32_t c0 = k[0], c1 = k[1], c2 = k[2], c3 = k[3], c4 = k[4];
        int32_t a = (int32_t)((uint32_t)md_s(*history) << 6) | (int32_t)((*fraction & 0xffffffu) >> 18);
        uint32_t h = (*fraction >> 17) & 1u, low = 0;
        int32_t cached = (int32_t)(w[base + 1] << 8);
        int32_t x0 = (int32_t)(w[output] << 8), x1 = (int32_t)(w[output + 1] << 8);
        a += md_ar2s((int32_t)(w[base] << 8), c0, cached, c1) + (int32_t)h; h ^= 1u;
        for (i = 0; i < MD_MH_N; ++i) {
            const int32_t x2 = (int32_t)(w[output + 2] << 8), before = cached;
            int32_t word;
            uint32_t f;
            a += md_ar4s(cached, c1, x0, c2, x1, c3, x2, c4) + 1;
            word = a >> 6;
            low = (((uint32_t)a & 63u) << 18) | (h << 17);
            if ((uint32_t)word + 0x800000u < 0x1000000u) f = low;
            else if (word > 0) { word = 0x7fffff; f = 0xffffffu; }
            else { word = -0x800000; f = 0u; }
            w[output] = md_w24(word); s->fraction[output] = f;
            cached = (int32_t)((uint32_t)word << 8);
            a += md_ar2s(before, c0, cached, c1) + (int32_t)h; h ^= 1u;
            ++output; x0 = x1; x1 = x2;
        }
        *history = w[output - 1]; *fraction = low; *previous = w[output - 2];
    }
}
static void md_mh_window(MD_PM_T *s, uint32_t step, uint32_t gain, int ride)
{
    uint64_t budget = mdkit_from24(s->storage.voice.window_budget), limit = mdkit_from24(step);
    if (mdkit_s56(budget) >= mdkit_s56(limit)) return;
    budget = mdkit_add(budget, mdkit_from24(32)); s->storage.voice.window_budget = mdkit_acc_to_word(budget);
    uint64_t remaining = mdkit_sub(limit, budget);
    if (mdkit_s56(remaining) > ((int64_t)32 << 24)) remaining = mdkit_from24(32);
    if (mdkit_s56(remaining) < 0) remaining = mdkit_sub(0, remaining);
    unsigned pairs = mdkit_mid(mdkit_asr(remaining, 1)) & 0xffff;
    if (!pairs || pairs > MD_MH_N) pairs = MD_MH_N;          /* a sample here for each of mdkit's pairs */
    const unsigned base = ride ? MD_MH_N : 0;
    uint32_t cursor = s->storage.voice.excitation_cursor;
    const int stride2 = 2 * (int)mdkit_s24(s->storage.voice.window_step);
    for (unsigned i = 0; i < pairs; ++i) {
        const uint32_t sample = metal_read(s, !ride, cursor);
        cursor = metal_advance(cursor, stride2, 0x7fff);
        metal_write(s, ride, base + i, mdkit_acc_to_word(mdkit_mac(mdkit_from24(metal_read(s, ride, base + i)), sample, gain)));
    }
    s->storage.voice.excitation_cursor = cursor;
}
static void md_mh_excitation(MD_PM_T *s, uint32_t increment, uint32_t threshold)
{
    uint64_t envelope = mdkit_from24(s->storage.voice.excitation_envelope);
    const uint32_t decay2 = metal_product(s->storage.voice.excitation_decay, s->storage.voice.excitation_decay);
    const uint32_t step2 = md_w24(md_sat(2 * md_s(increment)));
    unsigned i;
    uint32_t noise;
    for (i = 0; i < MD_MH_N; ++i) {
        if (mdkit_s24(s->fraction[i]) > mdkit_s24(threshold)) envelope = mdkit_add(envelope, mdkit_from24(step2));
        const uint32_t word = mdkit_acc_to_word(envelope);
        s->work[8 + i] = word; envelope = mdkit_mpy(word, decay2);
    }
    s->storage.voice.excitation_envelope = mdkit_acc_to_word(envelope);
    noise = s->fraction[MD_MH_N];
    for (i = 0; i < MD_MH_N; ++i) { s->work[8 + i] = metal_product(noise, s->work[8 + i]); noise = s->fraction[i]; }
    if (s->machine == 72 && mdkit_s24(noise) >= 0x79999a)
        s->storage.voice.excitation_envelope = mdkit_acc_to_word(mdkit_add(mdkit_from24(s->storage.voice.excitation_envelope), mdkit_from24(0x333333)));
}
static void md_mh_body_filters(MD_PM_T *s)
{
    uint32_t coeff[5];
    const unsigned row = 4 * s->storage.voice.brightness;
    unsigned i;
    s->work[4] = s->storage.voice.first_previous; s->work[5] = s->storage.voice.first_history;
    s->work[6] = s->storage.voice.first_input_previous; s->work[7] = s->storage.voice.first_input;
    for (i = 0; i < 4; ++i) coeff[i] = metal_read(s, 0, 0x145224 + row + i);
    coeff[4] = coeff[2];
    s->storage.voice.first_input = s->work[8 + MD_MH_N - 1]; s->storage.voice.first_input_previous = s->work[8 + MD_MH_N - 2];
    md_mh_filter(s, 4, &s->storage.voice.first_history, &s->storage.voice.first_fraction, &s->storage.voice.first_previous, coeff,
                 md_mh_rows_a[s->storage.voice.brightness & 127u]);
    s->work[0] = s->storage.voice.second_previous; s->work[1] = s->storage.voice.second_history;
    s->work[2] = s->storage.voice.second_input_previous; s->work[3] = s->storage.voice.second_input;
    const unsigned second_row = s->machine == 70 ? 4 * s->storage.voice.coupling_angle
        : mdkit_mid(mdkit_asl(mdkit_mpy(s->storage.voice.close_memory, s->storage.voice.coupling_angle), 2)) & 0xfffffc;
    for (i = 0; i < 4; ++i) coeff[i] = metal_read(s, 0, 0x145424 + second_row + i);
    coeff[4] = coeff[2];
    s->storage.voice.second_input = s->work[4 + MD_MH_N - 1]; s->storage.voice.second_input_previous = s->work[4 + MD_MH_N - 2];
    md_mh_filter(s, 0, &s->storage.voice.second_history, &s->storage.voice.second_fraction, &s->storage.voice.second_previous, coeff,
                 md_mh_rows_b[(second_row >> 2) & 127u]);
}
static void md_mh_amplitude(MD_PM_T *s)
{
    uint32_t *w = s->work;
    int32_t running = md_s(s->storage.voice.amplitude), gain = running;
    int32_t product = md_mul_shift_exact(gain, md_s(w[0]), 23);
    uint32_t next = w[1];
    unsigned i;
    if (s->storage.voice.attack_gate) {
        const int32_t step = 2 * md_s(s->storage.voice.attack_gate);
        running += step;
        for (i = 0; i < MD_MH_N - 1; ++i) {
            w[i] = md_w24(md_sat(product));
            product = md_mul_shift_exact(gain, md_s(next), 23);
            next = w[i + 2];
            gain = md_sat(running);
            running += step;
        }
        w[MD_MH_N - 1] = md_w24(md_sat(product));
        s->storage.voice.amplitude = md_w24(md_sat(running));
        if (running >= 0x7fffff) s->storage.voice.attack_gate = 0;
    } else {
        uint32_t decay = s->storage.voice.decay;
        int32_t d, r;
        if (md_s(s->storage.voice.ring_amplitude) < 84) decay = s->machine == 72 ? 0x7f8d50u : 0x666666u;
        d = md_sat(md_mul_shift_exact(md_s(decay), md_s(decay), 23));
        r = md_mul_shift_exact(gain, d, 23);
        for (i = 0; i < MD_MH_N - 1; ++i) {
            const int32_t old = md_sat(r);
            w[i] = md_w24(md_sat(product));
            product = md_mul_shift_exact(gain, md_s(next), 23);
            next = w[i + 2];
            r = md_mul_shift_exact(gain, d, 23);
            gain = old;
        }
        w[MD_MH_N - 1] = md_w24(md_sat(product));
        s->storage.voice.amplitude = md_w24(r);
    }
}
static void md_mh_coefficients(MD_PM_T *s)
{
    unsigned i;
    metal_coefficients(s);
#ifndef MD_MH_FULLSTRIDE
    for (i = 2; i < 6; ++i) s->fraction[i] = md_w24(md_s(s->fraction[i]) >> 1);
#endif
    {
        const int32_t g = md_s(s->fraction[7]), g2 = 2 * g - md_mh_mul(g, g);
        s->fraction[7] = md_w24(md_sat(g2));
    }
}
static void md_mh_coupling(MD_PM_T *s)
{
    unsigned source = 0, audio = 0x100, pass, i;
    const uint32_t delay_gain = s->machine == 70 ? 0x733333u : 0x7eb852u, excitation_gain = s->machine == 72 ? 0x400000u : 0xccccdu;
    s->fraction[6] = 0;
    for (pass = 0; pass < MD_MH_N / 8; ++pass) {
        uint32_t prior;
        metal_delay_read(s, s->storage.voice.page_a, s->storage.voice.page_d, s->fraction[2], s->fraction[5],
                         &s->storage.voice.delay_history_a, &s->storage.voice.delay_history_d, 0x89, 0xc9);
        metal_delay_read(s, s->storage.voice.page_c, s->storage.voice.page_b, s->fraction[4], s->fraction[3],
                         &s->storage.voice.delay_history_c, &s->storage.voice.delay_history_b, 0xc0, 0x80);
        audio = metal_advance(audio, -1, 31);
        prior = s->fraction[audio];
        for (i = 0; i < 8; ++i) {
            const int32_t sum = ((int32_t)(s->work[0xc9 + i] << 8) >> 2) + ((int32_t)(s->work[0xc0 + i] << 8) >> 2);
            const uint32_t mixed = md_w24(md_sat((2 * md_ar2s(MD_EX((int32_t)delay_gain), sum, MD_EX((int32_t)excitation_gain),
                                                              (int32_t)(s->work[source + i] << 8) >> 2) + 3) >> 7));
            s->fraction[audio] = prior; audio = metal_advance(audio, 1, 31); prior = mixed;
        }
        s->fraction[audio] = prior; audio = metal_advance(audio, 1, 31);
        metal_delay_write(s, 0x80, 0xc0, source, &s->storage.voice.page_a, &s->storage.voice.page_d, 1);
        metal_delay_write(s, 0x89, 0xc9, source, &s->storage.voice.page_b, &s->storage.voice.page_c, 0);
        source += 8; s->fraction[6] = source;
    }
}
static void md_mh_output(MD_PM_T *s, uint32_t gain)
{
    const unsigned duration = (s->machine == 70 ? 16 : s->machine == 71 ? 6 : 8) / 2;
    uint32_t pulse = s->machine == 71 ? 0x333333u : 0x666666u, coeff[5], *f = s->fraction;
    const unsigned row = 4 * s->storage.voice.brightness;
    unsigned table, i;
    int32_t last, z[MD_MH_N];
    if (s->machine == 72 ? s->storage.voice.gate != 0x7fffff : !s->storage.voice.gate) pulse = 0;
    for (i = 0; i < MD_MH_N; ++i) s->work[4 + i] = i < duration ? pulse : 0;
    s->work[0] = s->storage.voice.output_previous; s->work[1] = s->storage.voice.output_history;
    s->work[2] = s->storage.voice.output_input_previous; s->work[3] = s->storage.voice.output_input;
    table = s->machine == 72 ? 0x145420 : 0x145224 + row;
    coeff[0] = metal_read(s, 0, table); coeff[1] = metal_read(s, 0, table + 1);
    coeff[2] = metal_product(gain, metal_read(s, 0, table + 2)); coeff[3] = metal_product(gain, metal_read(s, 0, table + 3));
    coeff[4] = coeff[2];
    s->storage.voice.output_input = s->work[4 + MD_MH_N - 1]; s->storage.voice.output_input_previous = s->work[4 + MD_MH_N - 2];
    md_mh_filter(s, 0, &s->storage.voice.output_history, &s->storage.voice.output_fraction, &s->storage.voice.output_previous, coeff,
                 md_mh_rows_a[s->machine == 72 ? 127u : s->storage.voice.brightness & 127u]);
    for (i = 0; i < MD_MH_N; ++i) z[i] = md_sat(2 * (md_s(f[0x100 + i]) + md_s(s->work[4 + i])));
    last = md_s(f[0x1ff]);
    for (i = 0; i < MD_MH_N; ++i) {
        f[0x100 + 2 * i] = md_w24((last + z[i]) >> 1);
        f[0x101 + 2 * i] = md_w24(z[i]);
        last = z[i];
    }
    f[0x1ff] = md_w24(last);
    s->storage.voice.gate = 0;
}
