/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_rs_block.c - PI-RS's rs_block, word for word mdkit's
 * (mdkit/src/pi/rs/mdkit_pi_rs.c), with its per-sample loops in plain integers,
 * as native/pi_sd_block.c does for PI-SD.
 *
 * - The phase: an accumulator as its high word t and A0 l; to48 limits t.
 * - The excitation: from24(x0) twice + mpy_su(x1, f) - mpy_su(x0, f), read as a
 *   word: 2 x0 plus the two products' high word. Exact, floor((x1 - x0) f / 2^23)
 *   (md_mid_mpy2_x: x0 by -f, x1 by f; f < 2^24 is 25 bits signed, which it takes).
 *   Under MD_AP_ACC each product is the EMAC's read with its half unit
 *   (md_acc_from_read): the halves cancel and the word adds (read1 - read0) >> 6.
 *   Its words are read straight from PI-RS's sample table when the index is in it
 *   (rs_sample: every block of a note); elsewhere through drum_excitation_x.
 * - The excitation's gain: word(asl(mpy p2, x, 4)), floor(p2 x / 2^19) limited: one
 *   product. Under MD_AP_ACC mdkit's product is the EMAC's read with half a unit
 *   of A0, which the shift by 4 leaves below the word: the same word.
 * - The feedback: from24 sums are integers; only the mpy keeps an A0.
 * Included by the generated voice in place of rs_block (tools/md_port.py,
 * NATIVE). tests/test_md_st.py compares every word with mdkit's.
 */
#if defined(MD_PI_HALF) && defined(MD_AP_STRUCT)
#include "pi_rs_half_block.c"
#else
static void rs_block(mdkit_pi_rs *s)
{
    uint32_t *w = s->body.work, *y = s->body.fraction, *p = s->parameters;
    md_acc pitch, step, phase;
    unsigned i;
    s->excitation_slow = mdkit_acc_to_word(mdkit_mpy(s->excitation_slow, 0x7f7cee));
    s->excitation_fast = mdkit_acc_to_word(mdkit_mpy(s->excitation_fast, 0x7c28f6));
    pitch = mdkit_asl(mdkit_mpy(0x400000, p[0]), 1);
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
    if (mdkit_s56(phase) >= mdkit_s56(mdkit_from24(0x144efc))) phase = mdkit_from24(0x144efc);
    s->phase_hi = mdkit_mid(phase);
    s->phase_lo = mdkit_low(phase);
    y[63] = mdkit_acc_to_word(phase);
    for (i = 0; i < 32; ++i) {
        const uint32_t index = w[i];
        const int32_t f = (int32_t)(y[i] & 0xffffffu);
        int32_t x0, x1;
#ifdef MD_ST_TABLES
        if (index - 0x144cb2u < 0x144efdu - 0x144cb2u - 1u) {
            x0 = md_s(rs_sample(index - 0x144cb2u)); x1 = md_s(rs_sample(index + 1u - 0x144cb2u));
        } else if (index - 0x144efcu < 0x145000u - 0x144efcu - 1u) {
            /* the sample's last word and past it: the ROM has nothing from 0x144efd to 0x145000
             * (rs_rom gives 0 there, and drum_excitation_x reaches it): every block after the hit */
            x0 = index == 0x144efcu ? md_s(rs_sample(0x144efcu - 0x144cb2u)) : 0; x1 = 0;
        } else
#endif
        { x0 = md_s(drum_excitation_x(s, index)); x1 = md_s(drum_excitation_x(s, index + 1)); }
#ifdef MD_AP_ACC
        y[64 + i] = md_w24(md_sat(2 * x0 + ((md_ar1(x1, f) - md_ar1(x0, f)) >> 6)));
#else
        y[64 + i] = md_w24(md_sat(2 * x0 + md_mid_mpy2_x(x0, -f, x1, f)));
#endif
    }
    {
        const int32_t hard = md_s(p[2]);
        for (i = 0; i < 32; ++i) w[64 + i] = md_w24(md_sat(md_mul_shift_exact(hard, md_s(y[64 + i]), 19)));
    }
    s->rattle_tension = mdkit_acc_to_word(mdkit_mpy(0x7f5c29, s->rattle_tension));
    s->rattle_threshold = mdkit_acc_to_word(mdkit_mpy(p[6], s->rattle_threshold));
    s->rattle_gate = mdkit_acc_to_word(mdkit_mpy(0x47ae14, s->rattle_gate));
    {
        unsigned idx = mdkit_acc_to_word(mdkit_mpy(100, p[1]));
        uint32_t ring = mdkit_pi_rs_modal_x(&s->body, 0x142b49 + idx);
        w[0xfb] = ring;
        w[0xfc] = mdkit_acc_to_word(mdkit_mpy(ring, 0x5a827a));
        w[0xf9] = mdkit_acc_to_word(mdkit_mac(mdkit_from24(0x400000), 0xd9999a, s->rattle_tension));
    }
    drum_delays(s, 0x7ffcb9, w[0xf9], w[0xf9]);
    /* the feedback stays an md_acc: it leaks 0x0ccccd a sample, and grows past the one-word
     * form's range (tried: PI-RS 1.6 dB off) */
    {
        md_acc feedback = mdkit_from24(s->feedback);
        int32_t previous = md_s(s->feedback_previous);
        uint32_t output = s->feedback;
        for (i = 0; i < 32; ++i) {
            const int32_t sum = md_s(w[0xa0 + i]) + md_s(w[0xc0 + i]) + md_s(w[0x80 + i]);
            const uint32_t before = mdkit_acc_to_word(feedback);
            w[0x60 + i] = output;
            output = before;
            feedback = md_acc_make(MD_T(feedback) - previous + sum, MD_L(feedback));
            previous = md_sat(sum);
            feedback = md_acc_sub(feedback, md_acc_product(md_s(before), 0x0ccccd, 1));
        }
        s->feedback = mdkit_acc_to_word(feedback);
        s->feedback_previous = md_w24(previous);
    }
    drum_modes(s, drum_coefficients(s, y[0xfe]));
}
#endif
