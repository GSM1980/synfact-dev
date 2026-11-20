/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_rs_half_block.c - PI-RS's rs_block at half rate (MD_PI_HALF; as
 * pi_sd_half_block.c does for PI-SD; judged by its spectrum and by ear).
 * Sixteen samples a block, each two of mdkit's: the excitation's phase steps two samples;
 * the feedback's leak (0.1 a sample: a pole of 0.9) takes its pole squared, 0.81, so it
 * leaks 0.19 of the word (0x1851ec); it stays an md_acc as the full-rate block's.
 * drum_delays and drum_modes are pi_sd_half_delays.c's and pi_sd_half_modes.c's.
 * Included by pi_rs_block.c in place of rs_block.
 */
static void rs_block(mdkit_pi_rs *s)
{
    uint32_t *w = s->body.work, *y = s->body.fraction, *p = s->parameters;
    md_acc pitch, step, phase;
    unsigned i;
    s->excitation_slow = mdkit_acc_to_word(mdkit_mpy(s->excitation_slow, 0x7f7cee));
    s->excitation_fast = mdkit_acc_to_word(mdkit_mpy(s->excitation_fast, 0x7c28f6));
    pitch = mdkit_asl(mdkit_mpy(0x400000, p[0]), 1);
    y[0xfe] = mdkit_acc_to_word(pitch);
    step = mdkit_asr(pitch, 21);                          /* two samples' step */
    phase = mdkit_from48(mdkit_pack48(s->phase_hi, s->phase_lo));
    {
        int32_t t = MD_T(phase);
        uint32_t l = MD_L(phase);
        const int32_t st = MD_T(step);
        const uint32_t sl = MD_L(step);
        for (i = 0; i < 16; ++i) {
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
    for (i = 0; i < 16; ++i) {
        const uint32_t index = w[i];
        const int32_t f = (int32_t)(y[i] & 0xffffffu);
        int32_t x0, x1;
#ifdef MD_ST_TABLES
        if (index - 0x144cb2u < 0x144efdu - 0x144cb2u - 1u) {
            x0 = md_s(rs_sample(index - 0x144cb2u)); x1 = md_s(rs_sample(index + 1u - 0x144cb2u));
        } else if (index - 0x144efcu < 0x145000u - 0x144efcu - 1u) {
            x0 = index == 0x144efcu ? md_s(rs_sample(0x144efcu - 0x144cb2u)) : 0; x1 = 0;
        } else
#endif
        { x0 = md_s(drum_excitation_x(s, index)); x1 = md_s(drum_excitation_x(s, index + 1)); }
        y[64 + i] = md_w24(md_sat(2 * x0 + ((md_ar1(x1, f) - md_ar1(x0, f)) >> 6)));
    }
    {
        const int32_t hard = md_s(p[2]);
        for (i = 0; i < 16; ++i) w[64 + i] = md_w24(md_sat(md_mul_shift_exact(hard, md_s(y[64 + i]), 19)));
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
    {
        md_acc feedback = mdkit_from24(s->feedback);
        int32_t previous = md_s(s->feedback_previous);
        uint32_t output = s->feedback;
        for (i = 0; i < 16; ++i) {
            const int32_t sum = md_s(w[0xa0 + i]) + md_s(w[0xc0 + i]) + md_s(w[0x80 + i]);
            const uint32_t before = mdkit_acc_to_word(feedback);
            w[0x60 + i] = output;
            output = before;
            feedback = md_acc_make(MD_T(feedback) - previous + sum, MD_L(feedback));
            previous = md_sat(sum);
            feedback = md_acc_sub(feedback, md_acc_product(md_s(before), 0x1851ec, 1));
        }
        s->feedback = mdkit_acc_to_word(feedback);
        s->feedback_previous = md_w24(previous);
    }
    drum_modes(s, drum_coefficients(s, y[0xfe]));
}
