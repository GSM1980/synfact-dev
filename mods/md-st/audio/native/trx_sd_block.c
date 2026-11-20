/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/trx_sd_block.c - TRX-SD's sd_block, word for word mdkit's
 * (mdkit/src/trx/sd/mdkit_trx_sd.c), its sample loops on signed words.
 *
 * Every word the block makes is acc_to_word of a word plus products: from24(a) has
 * A0 = 0, so the high word is a + floor(x y / 2^23) (mpy is 2xy), limited; and
 * acc_to_word(mpy(x, y)) is the limit of floor(x y / 2^23) (md_mul_shift_exact).
 * The shape mix, from24(t) + from24(u) + asl(mpy t,k + mac k,u, 6), has the high word
 * t + u + floor(64 * 2k (t + u) / 2^24) = t + u + floor(k (t + u) / 2^17): one product of
 * a word and a 25-bit sum (the EMAC's read, exact; 64 bits without it). The order of the
 * stores, the envelopes' recursions and the noise LFSR are mdkit's.
 * Included by the generated voice in place of the function (tools/md_port.py, NATIVE).
 * tests/test_md_st.py compares every word with mdkit's.
 */
static inline int32_t trx_sd_word_mpy(int32_t x, int32_t y) { return md_sat(md_mul_shift_exact(x, y, 23)); }
static inline int32_t trx_sd_shape17(int32_t k, int32_t u)
{
#ifdef MD_EMAC
    return md_emac_read(k, u);
#else
    return (int32_t)(((int64_t)k * u) >> 17);
#endif
}
static void trx_sd_sd_block(mdkit_trx_sd *s)
{
    uint32_t *w = s->workspace;
    unsigned i;
    trx_sd_sd_tone(&s->phase_a, s->pitch_a, s->bump, s->tone_a, w + 64);
    const uint32_t old_bump = s->bump;
    if (s->attack_cursor > 0x140) s->bump = md_w24(trx_sd_word_mpy(md_s(s->bump_decay), md_s(old_bump)));
    else s->bump = md_w24(md_sat(2 * md_s(s->bump_step) + md_s(old_bump)));
    trx_sd_sd_tone(&s->phase_b, s->pitch_b, old_bump, s->tone_b, w + 32);
    {
        const int32_t shape = md_s(s->shape);
        int32_t body = md_s(s->body_amplitude);
        const int32_t decay = md_s(s->decay);
        s->shape = md_w24(trx_sd_word_mpy(md_s(s->shape_decay), shape));
        for (i = 0; i < 32; ++i) {
            const int32_t u = md_s(w[64 + i]) + md_s(w[32 + i]);
            const int32_t mixed = md_sat(u + trx_sd_shape17(shape, u));
            w[64 + i] = md_w24(trx_sd_word_mpy(body, mixed));      /* the tone, by the body envelope */
            body = trx_sd_word_mpy(body, decay);
        }
        s->body_amplitude = md_w24(body);
    }
    {
        uint32_t first = s->noise_a, second = s->noise_b;
        for (i = 0; i < 32; ++i) {
            const uint32_t sum = (first + second) & trx_sd_MD_MASK24;
            w[32 + i] = sum;
            if (i < 31) { first = second; second = sum; }
            else { s->noise_a = second; s->noise_b = sum; }
        }
    }
    trx_sd_sd_noise_filter(s);
    {
        int32_t amplitude = md_s(s->noise_amplitude);
        const int32_t decay = md_s(s->noise_decay), boost = md_s(s->snap_boost);
        const uint32_t cursor = s->attack_cursor;
        const int32_t snap = md_s(s->snap);
        s->attack_cursor = md_w24(md_sat(md_s(cursor) + 32));
        for (i = 0; i < 32; ++i) {
            const int32_t noise = trx_sd_word_mpy(amplitude, md_s(w[32 + i]));
            int32_t mixed;
            amplitude = trx_sd_word_mpy(amplitude, decay);
            w[32 + i] = md_w24(noise);
            mixed = md_sat(md_s(w[64 + i]) + md_mul_shift_exact(noise, boost, 23));
            w[64 + i] = md_w24(mixed);
            if (cursor <= 0xfe0) mixed = md_sat(mixed + md_mul_shift_exact(md_s(sd_attack(cursor + i)), snap, 23));
            s->block[i] = md_w24(mixed);
        }
        s->noise_amplitude = md_w24(amplitude);
    }
    s->last_output = s->block[31];
}
