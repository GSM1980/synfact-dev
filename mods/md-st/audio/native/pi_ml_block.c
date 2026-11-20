/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_ml_block.c - PI-ML's ml_block, word for word mdkit's (mdkit/src/pi/ml/
 * mdkit_pi_ml.c), with the generated helpers. What changes:
 * - hardness: acc_to_word(asl(mpy(w, h), 1)) is the limited floor(w h / 2^22), and
 *   the output acc_to_word(mpy(gain, w)) the limited floor(gain w / 2^23): one product
 *   each (md_mul_shift_exact, the EMAC's exact read of one product).
 * - the feedback loop: a is a word after its first sample (from24(before)), so each
 *   output word is the previous sample's word of b; b keeps its accumulator (A0).
 * - a word taken from an accumulator is taken once.
 * Included by the generated voice in place of the function (tools/md_port.py,
 * NATIVE). tests/test_md_st.py compares every word.
 */
static void ml_block(mdkit_pi_ml *s) {
  mdkit_pi_ml_modal *c = &s->body;
  uint32_t *w = c->work, *y = c->fraction;
  uint32_t old = s->attack;
  s->attack = mdkit_acc_to_word(mdkit_mpy(s->tension_decay, old));
  uint64_t a = mdkit_add(mdkit_mpy(s->tension, old), mdkit_from24(0x400000));
  a = mdkit_asl(mdkit_mpy(mdkit_acc_to_word(a), s->pitch), 1);
  y[0xfe] = mdkit_acc_to_word(a);
  mdkit_pi_ml_modal_excitation(c, &s->phase_hi, &s->phase_lo, mdkit_asr(a, 22), 0x1449c9);
  {
    const int32_t h = md_s(s->hardness);
    for (unsigned i = 0; i < 32; ++i)
      w[64 + i] = md_w24(md_sat(md_mul_shift_exact(md_s(w[64 + i]), h, 22)));
  }
  s->ring_c = mdkit_acc_to_word(mdkit_mpy(0x7f5c29, s->ring_c));
  s->ring_a = mdkit_acc_to_word(mdkit_mpy(0x7ef9db, s->ring_a));
  s->ring_b = mdkit_acc_to_word(mdkit_mpy(0x47ae14, s->ring_b));
#ifdef MD_AP_ACC
  /* MD_AP_ACC (MD_ST_APPROX): both one-pole accumulators as one word each,
   * floor(acc / 2^18), stepped by the EMAC's read of the product and half a unit every
   * other sample (pi_delays.c, pi_modal_filter.c); they start and end a block as words */
  {
    int32_t e = (int32_t)((uint32_t)md_s(s->excitation_filter) << 6);
    const int32_t g = md_s(s->allpass_gain);
    for (unsigned i = 0; i < 32; ++i) {
      const int32_t oldword = md_sat(e >> 6);
      w[0x60 + i] = md_w24(oldword);
      e += md_ar1(g, md_s(w[0x40 + i]) - oldword) + (int32_t)(i & 1u);
    }
    s->excitation_filter = md_w24(md_sat(e >> 6));
  }
  {
    int32_t b = (int32_t)((uint32_t)md_s(s->feedback_hi) << 6);
    w[0x40] = md_w24(md_sat(md_s(w[0x40]) + (b >> 6)));
    for (unsigned i = 0; i < 32; ++i) {
      const int32_t before = md_sat(b >> 6);
      if (!i) b -= (int32_t)((uint32_t)md_s(s->feedback_prev) << 6);   /* previous is 0 after the first */
      if (i < 31) w[0x41 + i] = md_w24(before);
      b -= md_ar1(before, 0x59999a) - (int32_t)(i & 1u);
    }
    s->feedback_hi = md_w24(md_sat(b >> 6));
    s->feedback_prev = 0;
  }
#else
  {
    md_acc e = md_acc_from24(s->excitation_filter);
    const int32_t g = md_s(s->allpass_gain);
    for (unsigned i = 0; i < 32; ++i) {
      const uint32_t oldword = md_acc_acc_to_word(e);
      w[0x60 + i] = oldword;
      e = md_acc_mac_diff(e, g, md_s(w[0x40 + i]), md_s(oldword)); /* mac then sub mpy, one product */
    }
    s->excitation_filter = md_acc_acc_to_word(e);
  }
  {
    uint64_t b = mdkit_from24(s->feedback_hi);
    w[0x40] = mdkit_acc_to_word(mdkit_add(mdkit_from24(w[0x40]), b));
    for (unsigned i = 0; i < 32; ++i) {
      const uint32_t before = mdkit_acc_to_word(b);
      if (!i) b = mdkit_sub(b, mdkit_from24(s->feedback_prev));   /* previous is 0 after the first */
      if (i < 31) w[0x41 + i] = before;
      b = mdkit_sub(b, mdkit_mpy(before, 0x59999a));
    }
    s->feedback_hi = mdkit_acc_to_word(b);
    s->feedback_prev = 0;
  }
#endif
  for (unsigned i = 0; i < 8; ++i) {
    w[0xe0 + i] =
        mdkit_acc_to_word(mdkit_mpy(mdkit_pi_ml_modal_x(c, 0x143f40 + i), s->mode_spacing));
    w[0xe8 + i] =
        mdkit_acc_to_word(mdkit_mpy(mdkit_pi_ml_modal_x(c, 0x143f48 + i), s->mode_gain));
  }
  mdkit_pi_ml_modal_coefficients(c, y[0xfe], s->mode_decay, 0x143f38, 0xe8);
  mdkit_pi_ml_modal_filter(c);
  old = s->amplitude;
  s->amplitude = mdkit_acc_to_word(mdkit_mpy(s->decay, old));
  {
    const int32_t gain = md_s(mdkit_acc_to_word(mdkit_mpy(0x400000, old)));
    for (unsigned i = 0; i < 32; ++i)
      s->block[i] = md_w24(md_sat(md_mul_shift_exact(gain, md_s(w[0x40 + i]), 23)));
  }
}
