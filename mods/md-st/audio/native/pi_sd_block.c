/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_sd_block.c - PI-SD's sd_block, word for word mdkit's
 * (mdkit/src/pi/sd/mdkit_pi_sd.c), with its per-sample loops in plain integers.
 *
 * - The phase: an accumulator as its high word t and A0 l; to48 limits t.
 * - The sixteen pairs: mdkit sets first = second = 0 at the end of each pair,
 *   so after the first pair each word is (0 : y) >> 1, and the "minus
 *   0x7fffff" leaves its A0 (y >> 1) - 0x7fffff; the first pair carries the
 *   phase's (and phase + step's) high word's low bit into bit 23.
 * - The excitation: mpy y32,x0 - mpy y,x1 is md_acc_word2 with -y (25 bits).
 *   Its two words are read straight from PI-SD's ROM table when the index is
 *   in it (every block after a note); elsewhere through drum_excitation_x.
 * - The feedback: from24 sums are integers; only the mpy keeps an A0.
 * Included by the generated voice in place of sd_block (tools/md_port.py,
 * NATIVE). tests/test_md_st.py compares every word with mdkit's.
 */
#if defined(MD_PI_HALF) && defined(MD_AP_STRUCT)
#include "pi_sd_half_block.c"
#else
static void sd_block(mdkit_pi_sd *s)
{
    uint32_t *w = s->body.work, *y = s->body.fraction, *p = s->parameters;
    md_acc pitch, step, phase;
    unsigned i;
    s->excitation_slow = mdkit_acc_to_word(mdkit_mpy(s->excitation_slow, 0x7f7cee));
    s->excitation_fast = mdkit_acc_to_word(mdkit_mpy(s->excitation_fast, 0x7c28f6));
    {
        md_acc tension = mdkit_add(mdkit_add(mdkit_mpy(p[2], s->excitation_fast), mdkit_mpy(p[4], s->excitation_slow)),
                                   mdkit_from24(0x400000));
        pitch = mdkit_asl(mdkit_mpy(mdkit_acc_to_word(tension), p[0]), 1);
    }
    y[0xfe] = mdkit_acc_to_word(pitch);
    step = mdkit_asr(pitch, 22);
    phase = mdkit_from48(mdkit_pack48(s->phase_hi, s->phase_lo));
    {
        int32_t t = MD_T(phase);
        uint32_t l = MD_L(phase);
        const int32_t st = MD_T(step);
        const uint32_t sl = MD_L(step);
        for (i = 0; i < 32; ++i) {                        /* stored = to48(phase); phase += step */
            if (t < -0x800000) { w[i] = 0x800000u; y[i] = 0; }
            else if (t > 0x7fffff) { w[i] = 0x7fffffu; y[i] = 0xffffffu; }
            else { w[i] = (uint32_t)t & 0xffffffu; y[i] = l; }
            l += sl; t += st + (int32_t)(l >> 24); l &= 0xffffffu;
        }
        phase = md_acc_make(t, l);
    }
    if (mdkit_s56(phase) >= mdkit_s56(mdkit_from24(0x1433b6))) phase = mdkit_from24(0x1433b6);
    s->phase_hi = mdkit_mid(phase);
    s->phase_lo = mdkit_low(phase);
    {
        const md_acc second = mdkit_add(phase, step);
        /* pair 0: (t : y) >> 1 keeps t's low bit as A0's bit 23 */
        uint32_t f = (y[0] >> 1) | (((uint32_t)MD_T(phase) & 1u) << 23);
        uint32_t g = (y[1] >> 1) | (((uint32_t)MD_T(second) & 1u) << 23);
        y[0] = f; y[1] = g;
        y[32] = (f - 0x7fffffu) & 0xffffffu; y[33] = (g - 0x7fffffu) & 0xffffffu;
        for (i = 2; i < 32; ++i) {                         /* then (0 : y) >> 1 */
            f = y[i] >> 1;
            y[i] = f;
            y[32 + i] = (f - 0x7fffffu) & 0xffffffu;
        }
    }
    for (i = 0; i < 32; ++i) {
        const uint32_t index = w[i];
        int32_t x0, x1;
#ifdef MD_ST_TABLES
        if (index - 0x142949u < 0x1433b7u - 0x142949u - 1u) {
            x0 = md_s(md_pi_sd_rom[index - 0x142949u]); x1 = md_s(md_pi_sd_rom[index + 1u - 0x142949u]);
        } else if (index - 0x1433b6u < 0x145000u - 0x1433b6u - 1u) {
            /* the sample's last word and past it: sd_rom gives 0 from 0x1433b7 to 0x145000, and
             * drum_excitation_x reaches it (every block after the hit) */
            x0 = index == 0x1433b6u ? md_s(md_pi_sd_rom[0x1433b6u - 0x142949u]) : 0; x1 = 0;
        } else
#endif
        { x0 = md_s(drum_excitation_x(s, index)); x1 = md_s(drum_excitation_x(s, index + 1)); }
        y[64 + i] = md_acc_word2(md_s(y[32 + i]), x0, x1, -md_s(y[i]));
    }
    s->rattle_tension = mdkit_acc_to_word(mdkit_mpy(0x7f5c29, s->rattle_tension));
    s->rattle_threshold = mdkit_acc_to_word(mdkit_mpy(p[6], s->rattle_threshold));
    s->rattle_gate = mdkit_acc_to_word(mdkit_mpy(0x47ae14, s->rattle_gate));
    {
        unsigned idx = mdkit_acc_to_word(mdkit_mpy(30, p[1]));
        uint32_t ring = mdkit_pi_sd_modal_x(&s->body, 0x142b49 + idx);
        w[0xfb] = ring;
        w[0xfc] = mdkit_acc_to_word(mdkit_mpy(ring, 0x5a827a));
        w[0xf9] = mdkit_acc_to_word(mdkit_mac(mdkit_from24(0x333333), 0xd9999a, s->rattle_tension));
    }
    drum_delays(s, p[3], w[0xf9], w[0xf9]);
#ifdef MD_AP_ACC
    /* MD_AP_ACC: the feedback accumulator starts the block from a word: one word in 2^-6 of a
     * word with h its half unit (md_acc_from_read's, subtracted with each product), the same
     * words as md_acc */
    {
        int32_t feedback = (int32_t)((uint32_t)md_s(s->feedback) << 6), h = 0;
        int32_t input = md_s(w[0x40]) + md_s(s->feedback);
        int32_t previous = md_s(s->feedback_previous);
        for (i = 0; i < 32; ++i) {
            const int32_t sum = md_s(w[0xa0 + i]) + md_s(w[0xc0 + i]) + md_s(w[0x80 + i]);
            const int32_t old = md_sat(feedback >> 6);
            w[0x40 + i] = md_w24(md_sat(input));
            feedback += (sum - previous) * 64;
            previous = md_sat(sum);
            input = md_s(w[0x41 + i]) + old;
            feedback -= md_ar1(old, 0x59999a) + (h ^ 1); h ^= 1;
        }
        s->feedback = md_w24(md_sat(feedback >> 6));
        s->feedback_previous = md_w24(previous);
    }
#else
    {
        md_acc feedback = mdkit_from24(s->feedback);
        int32_t input = md_s(w[0x40]) + MD_T(feedback);     /* from24 + feedback: A0 adds no carry */
        int32_t previous = md_s(s->feedback_previous);
        for (i = 0; i < 32; ++i) {
            const int32_t sum = md_s(w[0xa0 + i]) + md_s(w[0xc0 + i]) + md_s(w[0x80 + i]);
            const int32_t old = md_acc_word_s(feedback);
            w[0x40 + i] = md_w24(md_sat(input));
            feedback = md_acc_make(MD_T(feedback) - previous + sum, MD_L(feedback));
            previous = md_sat(sum);
            input = md_s(w[0x41 + i]) + old;
            feedback = md_acc_sub(feedback, md_acc_product(old, md_s(0x59999au), 1));
        }
        s->feedback = mdkit_acc_to_word(feedback);
        s->feedback_previous = md_w24(previous);
    }
#endif
    drum_modes(s, drum_coefficients(s, y[0xfe]));
}
#endif
