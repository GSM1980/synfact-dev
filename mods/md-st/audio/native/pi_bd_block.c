/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_bd_block.c - PI-BD's bd_block, word for word mdkit's (mdkit/src/pi/bd/
 * mdkit_pi_bd.c), with the generated helpers. What changes:
 * - The three delay lines are 512 words each (0x135600 + 0x200 i), so a pointer's
 *   step of 1 is base | ((p + 1) & 0x1ff): mdkit_pi_bd_modal_wrap(p, 1, 511).
 * - A word written from an accumulator is taken once (mdkit takes it up to three
 *   times); the input and the branch filters keep their accumulators (A0 carries).
 * - The delay writes: a word of three products is mdkit's mpy, mac, mac (or sub) as
 *   one exact sum, x K + fb (u + v) or x K + fb (u - v) (md_acc_word2: v may be 25
 *   bits). The order of the three stores (p2, p1, p0) is mdkit's.
 * - The feedback loop: a is a sum of words (no product): its word is the limited sum;
 *   b is mpy 0x59999a, x0 less mpy 0x400000, old plus mac 0x400000, new, one exact sum
 *   0x59999a x0 + 0x400000 (new - old); the output's word is limit(4 (w + x0)).
 * Included by the generated voice in place of the function (tools/md_port.py,
 * NATIVE). tests/test_md_st.py compares every word.
 */
#define BD_NEXT(p) (((p) & ~0x1ffu) | (((p) + 1u) & 0x1ffu))
static void bd_block(mdkit_pi_bd *s) {
  mdkit_pi_bd_modal *c = &s->body;
  uint32_t *w = c->work, *y = c->fraction;
  uint32_t old = s->attack;
  s->attack = mdkit_acc_to_word(mdkit_mpy(old, 0x7c28f6));
  uint64_t a = mdkit_add(mdkit_mpy(s->tension, old), mdkit_from24(0x400000));
  a = mdkit_asl(mdkit_mpy(mdkit_acc_to_word(a), s->pitch), 1);
  y[0xfe] = mdkit_acc_to_word(a);
  mdkit_pi_bd_modal_excitation(c, &s->phase_hi, &s->phase_lo, mdkit_asr(a, 22), 0x142859);
  const uint32_t idx = mdkit_acc_to_word(mdkit_mpy(0x42, s->decay));
  const uint32_t coupling = mdkit_pi_bd_modal_x(c, 0x142b49 + idx);
  const uint32_t feedback = mdkit_acc_to_word(mdkit_mpy(coupling, 0x5a827a));
  const int32_t CP = md_s(coupling), FB = md_s(feedback), H = md_s(s->hammer), D = md_s(s->damp);
  w[0xfb] = coupling;
  w[0xfc] = feedback;

  /* the input (branch 0 only) and the three delay branches */
#ifdef MD_AP_ACC
  /* MD_AP_ACC: each branch's one-pole as one word, floor(acc / 2^18), stepped by the EMAC's
   * read and a unit every other sample: the same words as md_acc with the read's half units
   * (native/pi_delays.c says why). The hammer filter stays exact (-76 dBFS with the read)
   * but under MD_AP_STRUCT, where it takes the same form. */
  {
    static const unsigned stride[3] = {0x10b, 0xff, 0xb5};
#ifdef MD_AP_STRUCT
    int32_t input = (int32_t)((uint32_t)md_s(s->excitation_filter) << 6);
    for (unsigned i = 0; i < 32; ++i) {
      const int32_t source = md_s(w[0x40 + i]), word = md_sat(input >> 6);
      w[0x40 + i] = w[0x60 + i] = md_w24(word);
      input += md_ar1(H, source - word) + (int32_t)(i & 1u);
    }
    s->excitation_filter = md_w24(md_sat(input >> 6));
#else
    md_acc input = md_acc_from24(s->excitation_filter);
    for (unsigned i = 0; i < 32; ++i) {
      const int32_t source = md_s(w[0x40 + i]);
      const uint32_t word = md_acc_acc_to_word(input);
      w[0x40 + i] = word; w[0x60 + i] = word;
      input = md_acc_mac_diff_x(input, H, source, md_s(word)); /* mac then sub mpy, one product */
    }
    s->excitation_filter = md_acc_acc_to_word(input);
#endif
#ifdef MD_AP_STRUCT
    /* MD_AP_STRUCT: the three delay lines at half rate, as PI-SD's (pi_delays.c): 16 words a
     * block a line, their delays (512 - stride: 245, 257, 331) halved and rounded to whole words
     * (123, 129, 166), the one-poles' gain for two steps (2 g - g^2), the words between as means */
    {
      static const unsigned half[3] = {123, 129, 166};
      int32_t d2 = 2 * D - md_mul_shift_exact(D, D, 23);
      if (d2 > 0x7fffff) d2 = 0x7fffff;
      for (unsigned branch = 0; branch < 3; ++branch) {
        const uint32_t *ring = s->delay + ((s->delay_pointer[branch] - 0x135600u) & ~0x1ffu);
        const unsigned start = (s->delay_pointer[branch] - half[branch]) & 0x1ffu;
        int32_t b = (int32_t)((uint32_t)md_s(s->delay_filter[branch]) << 6), last = md_sat(b >> 6);
        uint32_t *out = w + 0x80 + 32 * branch;
        out[0] = md_w24(last);
        for (unsigned j = 0; j < 16; ++j) {
          if (j) { const int32_t word = md_sat(b >> 6); out[2 * j - 1] = md_w24((last + word) >> 1); out[2 * j] = md_w24(word); last = word; }
          b += md_ar1(d2, md_s(ring[(start + j) & 0x1ffu]) - last) + (int32_t)(j & 1u);
        }
        out[31] = md_w24((last + md_sat(b >> 6)) >> 1);
        s->delay_filter[branch] = md_w24(md_sat(b >> 6));
      }
    }
#else
    for (unsigned branch = 0; branch < 3; ++branch) {
      /* the line's 512 words and the index in them: its one or two runs (BD_NEXT's wrap) */
      const uint32_t pointer = mdkit_pi_bd_modal_wrap(s->delay_pointer[branch], stride[branch], 511);
      const uint32_t *ring = s->delay + ((pointer - 0x135600u) & ~0x1ffu), *in = ring + (pointer & 0x1ffu);
      int32_t b = (int32_t)((uint32_t)md_s(s->delay_filter[branch]) << 6);
      uint32_t *out = w + 0x80 + 32 * branch;
      unsigned i = 0, run = 512u - (pointer & 0x1ffu);
      if (run > 32u) run = 32u;
      for (; i < run; ++i) {
        const int32_t word = md_sat(b >> 6);
        out[i] = md_w24(word);
        b += md_ar1(D, md_s(in[i]) - word) + (int32_t)(i & 1u);
      }
      in = ring - run;
      for (; i < 32u; ++i) {
        const int32_t word = md_sat(b >> 6);
        out[i] = md_w24(word);
        b += md_ar1(D, md_s(in[i]) - word) + (int32_t)(i & 1u);
      }
      s->delay_filter[branch] = md_w24(md_sat(b >> 6));
    }
#endif
  }
#else
  {
    md_acc input = md_acc_from24(s->excitation_filter);
    static const unsigned stride[3] = {0x10b, 0xff, 0xb5};
    for (unsigned branch = 0; branch < 3; ++branch) {
      uint32_t pointer = mdkit_pi_bd_modal_wrap(s->delay_pointer[branch], stride[branch], 511);
      md_acc b = md_acc_from24(s->delay_filter[branch]);
      uint32_t *out = w + 0x80 + 32 * branch;
      int32_t delayed = md_s(s->delay[pointer - 0x135600]);
      pointer = BD_NEXT(pointer);
      if (!branch)
        for (unsigned i = 0; i < 32; ++i) {
          const int32_t source = md_s(w[0x40 + i]);
          const uint32_t word = md_acc_acc_to_word(input);
          w[0x40 + i] = word; w[0x60 + i] = word;
          input = md_acc_mac_diff_x(input, H, source, md_s(word)); /* mac then sub mpy, one product */
        }
      for (unsigned i = 0; i < 32; ++i) {
        const uint32_t word = md_acc_acc_to_word(b);
        out[i] = word;
        b = md_acc_mac_diff(b, D, delayed, md_s(word));
        delayed = md_s(s->delay[pointer - 0x135600]);
        pointer = BD_NEXT(pointer);
      }
      s->delay_filter[branch] = md_acc_acc_to_word(b);
    }
    s->excitation_filter = md_acc_acc_to_word(input);
  }
#endif

  /* the delay writes: x K + coupling y (va), x K + fb (u + v) (vb), x K + fb (v - u) (p0) */
  {
    /* each line's 512 words and the index in them (BD_NEXT steps the index) */
    uint32_t *r0 = s->delay + ((s->delay_pointer[0] - 0x135600u) & ~0x1ffu), *r1 = s->delay + ((s->delay_pointer[1] - 0x135600u) & ~0x1ffu),
             *r2 = s->delay + ((s->delay_pointer[2] - 0x135600u) & ~0x1ffu);
    unsigned p0 = s->delay_pointer[0] & 0x1ffu, p1 = s->delay_pointer[1] & 0x1ffu, p2 = s->delay_pointer[2] & 0x1ffu;
#ifdef MD_AP_STRUCT
    for (unsigned j = 0; j < 16; ++j) {        /* 16 words a block, from the source's pair means */
      const int32_t x = (md_s(w[0x60 + 2 * j]) + md_s(w[0x61 + 2 * j])) >> 1, middle = md_s(w[0xa0 + 2 * j]), last = md_s(w[0xc0 + 2 * j]);
      const int32_t shared = md_ar1(x, 0x51eb8);
      r2[p2] = md_w24(md_sat((shared + md_ar1(CP, md_s(w[0x80 + 2 * j]))) >> 6));
      p2 = (p2 + 1u) & 0x1ffu;
      r1[p1] = md_w24(md_sat((shared + md_ar1(FB, middle + last)) >> 6));
      p1 = (p1 + 1u) & 0x1ffu;
      r0[p0] = md_w24(md_sat((shared + md_ar1(FB, last - middle)) >> 6));
      p0 = (p0 + 1u) & 0x1ffu;
    }
#else
    for (unsigned i = 0; i < 32; ++i) {
      const int32_t x = md_s(w[0x60 + i]), middle = md_s(w[0xa0 + i]), last = md_s(w[0xc0 + i]);
      r2[p2] = md_acc_word2(x, 0x51eb8, CP, md_s(w[0x80 + i]));
      p2 = (p2 + 1u) & 0x1ffu;
      r1[p1] = md_acc_word2(x, 0x51eb8, FB, middle + last);
      p1 = (p1 + 1u) & 0x1ffu;
      r0[p0] = md_acc_word2(x, 0x51eb8, FB, last - middle);
      p0 = (p0 + 1u) & 0x1ffu;
    }
#endif
    s->delay_pointer[0] = (s->delay_pointer[0] & ~0x1ffu) | p0;
    s->delay_pointer[1] = (s->delay_pointer[1] & ~0x1ffu) | p1;
    s->delay_pointer[2] = (s->delay_pointer[2] & ~0x1ffu) | p2;
  }

  /* the feedback loop */
  {
    int32_t previous = md_s(s->feedback_prev), xb = md_s(s->feedback_hi);   /* b starts as a word */
    for (unsigned i = 0; i < 32; ++i) {
      const int32_t now = md_sat(md_s(w[0xc0 + i]) - md_s(w[0xa0 + i]) + md_s(w[0x80 + i]));
      const int32_t next = md_s(md_acc_word2(0x59999a, xb, 0x400000, now - previous));
      w[0x40 + i] = md_w24(md_sat((md_s(w[0x40 + i]) + xb) * 4));
      previous = now; xb = next;
    }
    s->feedback_hi = md_w24(xb);
    s->feedback_prev = md_w24(previous);
  }
  mdkit_pi_bd_modal_coefficients(c, y[0xfe], s->decay, 0x142279, 0x142289);
  mdkit_pi_bd_modal_filter(c);
  for (unsigned i = 0; i < 32; ++i)
    s->block[i] = w[0x40 + i];
}
#undef BD_NEXT
