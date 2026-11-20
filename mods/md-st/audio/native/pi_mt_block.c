/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_mt_block.c - PI-MT's mt_block, word for word mdkit's
 * (mdkit/src/pi/mt/mdkit_pi_mt.c), with its per-sample loops in plain integers.
 *
 * - The three delay lines' one-poles and the source filter (line 0) are independent
 *   of each other sample by sample: each runs in a loop of its own, in mdkit's order of
 *   reads and writes. Under MD_AP_ACC each accumulator is one word, floor(acc / 2^18),
 *   stepped by the EMAC's read of its product and a unit every other sample: the same
 *   words as md_acc with md_acc_from_read's half units (native/pi_delays.c says why),
 *   as each starts the block from a word (A0 zero).
 * - The mix into the delay lines: input x 0x28f5c3 is one product shared by the three
 *   sums. Under MD_AP_ACC each sum is the reads of its products, a sum of two words by
 *   one word a single product (cross (a + c), cross (c - a)), with half a unit a read.
 *   Otherwise mdkit's accumulators.
 * - The feedback: each sample's accumulator is made again from three products,
 *   0x59999a by the last word and 0x400000 by the last and the new difference (one
 *   half each: the EMAC reads them exactly, 32 times the word); the old word, added to
 *   the source and shifted by 2, is the output's start. Under MD_AP_ACC the word of the
 *   three reads with their half units, (2 (r + 32 (new - old)) + 1) >> 7.
 * Included by the generated voice in place of mt_block (tools/md_port.py, NATIVE).
 * tests/test_md_st.py compares every word with mdkit's.
 */
static void mt_block(mdkit_pi_mt *s)
{
    mdkit_pi_mt_modal *c = &s->body;
    uint32_t *w = c->work, *y = c->fraction, *p = s->parameters;
    static const unsigned lengths[3] = {466, 310, 232};
    unsigned i, line;
    uint32_t ring, cross, span;
    {
        const uint32_t old = s->excitation_fast;
        uint32_t slope;
        uint64_t pitch;
        s->excitation_fast = mdkit_acc_to_word(mdkit_mpy(old, 0x7ef9db));
        slope = mdkit_acc_to_word(mdkit_sub(mdkit_mpy(0x333333, p[2]), mdkit_from24(0x66666)));
        pitch = mdkit_add(mdkit_mpy(slope, old), mdkit_from24(0x400000));
        pitch = mdkit_asl(mdkit_mpy(mdkit_acc_to_word(pitch), p[0]), 1);
        y[0xfe] = mdkit_acc_to_word(pitch);
        mdkit_pi_mt_modal_excitation(c, &s->phase_hi, &s->phase_lo, mdkit_asr(pitch, 22), 0x143d1d);
    }
    {
        const unsigned idx = mdkit_acc_to_word(mdkit_mpy(511, p[1]));
        ring = mdkit_pi_mt_modal_x(c, 0x142b49 + idx);
        cross = mdkit_acc_to_word(mdkit_mpy(ring, 0x5a827a));
        w[0xfb] = ring; w[0xfc] = cross;
        span = mdkit_acc_to_word(mdkit_add(mdkit_mpy(0x666666, p[6]), mdkit_from24(0x19999a)));
    }
#ifdef MD_AP_ACC
    {
        const int32_t hammer = md_s(p[3]), damp = md_s(p[7]);
        int32_t filtered = (int32_t)((uint32_t)md_s(s->source_filter) << 6);
        for (i = 0; i < 32; ++i) {                            /* line 0's source filter */
            const int32_t input = md_s(w[0x40 + i]), previous = md_sat(filtered >> 6);
            w[0x40 + i] = w[0x60 + i] = md_w24(previous);
            filtered += md_ar1(hammer, input - previous) + (int32_t)(i & 1u);
        }
        s->source_filter = md_w24(md_sat(filtered >> 6));
#ifdef MD_AP_STRUCT
        /* MD_AP_STRUCT (a voice judged by its spectrum): the three delay lines at half rate, as
         * PI-SD's (pi_delays.c): 16 words a block a line, read half as far back (an odd length
         * from two taps' mean), the one-poles' gain for two steps (2 g - g^2), the words between
         * as means; the source filter and the feedback stay at full rate */
        {
            int32_t g2 = 2 * damp - md_mul_shift_exact(damp, damp, 23);
            if (g2 > 0x7fffff) g2 = 0x7fffff;
            for (line = 0; line < 3; ++line) {
                const unsigned length = mdkit_acc_to_word(mdkit_mpy(span, lengths[line])), half = length >> 1, odd = length & 1u;
                const uint32_t *in = line < 2 ? s->delay_x[line] : s->delay_y;
                uint32_t *out = w + 0x80 + 32 * line;
                const unsigned position = s->delay_position[line];
                int32_t tail = (int32_t)((uint32_t)md_s(s->delay_filter[line]) << 6), word[17];
                for (i = 0; i < 16; ++i) {
                    const unsigned at = (position + i - half) & 511u;
                    const int32_t x = odd ? (md_s(in[at]) + md_s(in[(at - 1u) & 511u])) >> 1 : md_s(in[at]);
                    word[i] = md_sat(tail >> 6);
                    tail += md_ar1(g2, x - word[i]) + (int32_t)(i & 1u);
                }
                word[16] = md_sat(tail >> 6);
                for (i = 0; i < 16; ++i) { out[2 * i] = md_w24(word[i]); out[2 * i + 1] = md_w24((word[i] + word[i + 1]) >> 1); }
                s->delay_filter[line] = md_w24(word[16]);
            }
        }
    }
    {
        const int32_t r = md_s(ring), x = md_s(cross);
        const unsigned first = s->delay_position[0], second = s->delay_position[1], third = s->delay_position[2];
        for (i = 0; i < 16; ++i) {
            const int32_t a = md_s(w[0xa0 + 2 * i]), b = md_s(w[0xc0 + 2 * i]);
            const int32_t shared = md_ar1((md_s(w[0x60 + 2 * i]) + md_s(w[0x61 + 2 * i])) >> 1, 0x28f5c3) + 1;
            s->delay_x[0][(first + i) & 511] = md_w24(md_sat((shared + md_ar1(x, b - a)) >> 6));
            s->delay_x[1][(second + i) & 511] = md_w24(md_sat((shared + md_ar1(x, a + b)) >> 6));
            s->delay_y[(third + i) & 511] = md_w24(md_sat((shared + md_ar1(r, md_s(w[0x80 + 2 * i]))) >> 6));
        }
        s->delay_position[0] = (first + 16) & 511; s->delay_position[1] = (second + 16) & 511; s->delay_position[2] = (third + 16) & 511;
    }
#else
        for (line = 0; line < 3; ++line) {
            const unsigned length = mdkit_acc_to_word(mdkit_mpy(span, lengths[line]));
            const uint32_t *in = line < 2 ? s->delay_x[line] : s->delay_y;
            uint32_t *out = w + 0x80 + 32 * line;
            const unsigned cursor = (s->delay_position[line] - length) & 511;
            int32_t tail = (int32_t)((uint32_t)md_s(s->delay_filter[line]) << 6);
            unsigned run = 512u - cursor;                 /* the ring's one or two runs */
            const uint32_t *at = in + cursor;
            if (run > 32u) run = 32u;
            for (i = 0; i < run; ++i) {
                const int32_t previous = md_sat(tail >> 6);
                out[i] = md_w24(previous);
                tail += md_ar1(damp, md_s(at[i]) - previous) + (int32_t)(i & 1u);
            }
            at = in - run;
            for (; i < 32; ++i) {
                const int32_t previous = md_sat(tail >> 6);
                out[i] = md_w24(previous);
                tail += md_ar1(damp, md_s(at[i]) - previous) + (int32_t)(i & 1u);
            }
            s->delay_filter[line] = md_w24(md_sat(tail >> 6));
        }
    }
    {
        const int32_t r = md_s(ring), x = md_s(cross);
        unsigned first = s->delay_position[0], second = s->delay_position[1], third = s->delay_position[2];
        for (i = 0; i < 32; ++i) {
            const int32_t a = md_s(w[0xa0 + i]), b = md_s(w[0xc0 + i]);
            const int32_t shared = md_ar1(md_s(w[0x60 + i]), 0x28f5c3) + 1;     /* and the other read's half unit */
            s->delay_x[0][first] = md_w24(md_sat((shared + md_ar1(x, b - a)) >> 6));
            s->delay_x[1][second] = md_w24(md_sat((shared + md_ar1(x, a + b)) >> 6));
            s->delay_y[third] = md_w24(md_sat((shared + md_ar1(r, md_s(w[0x80 + i]))) >> 6));
            first = (first + 1) & 511; second = (second + 1) & 511; third = (third + 1) & 511;
        }
        s->delay_position[0] = first; s->delay_position[1] = second; s->delay_position[2] = third;
    }
#endif
    {
        int32_t feedback = md_s(s->feedback), previous = md_s(s->feedback_previous);
        for (i = 0; i < 32; ++i) {
            const int32_t difference = md_sat(md_s(w[0xc0 + i]) - md_s(w[0xa0 + i]) + md_s(w[0x80 + i]));
            w[0x40 + i] = md_w24(md_sat(4 * (md_s(w[0x40 + i]) + feedback)));
            feedback = md_sat((2 * md_ar1(0x59999a, feedback) + 64 * (difference - previous) + 1) >> 7);
            previous = difference;
        }
        s->feedback = md_w24(feedback);
        s->feedback_previous = md_w24(previous);
    }
#else
    {
        uint64_t filtered = mdkit_from24(s->source_filter);
        for (line = 0; line < 3; ++line) {
            const unsigned length = mdkit_acc_to_word(mdkit_mpy(span, lengths[line]));
            unsigned cursor = (s->delay_position[line] - length) & 511;
            uint64_t tail = mdkit_from24(s->delay_filter[line]);
            for (i = 0; i < 32; ++i) {
                if (!line) {
                    const uint32_t input = w[0x40 + i], previous = mdkit_acc_to_word(filtered);
                    w[0x40 + i] = w[0x60 + i] = previous;
                    filtered = md_acc_mac_diff(filtered, md_s(p[3]), md_s(input), md_s(previous));
                }
                {
                    const uint32_t previous = mdkit_acc_to_word(tail);
                    const uint32_t input = line < 2 ? s->delay_x[line][cursor] : s->delay_y[cursor];
                    w[0x80 + line * 32 + i] = previous;
                    tail = md_acc_mac_diff(tail, md_s(p[7]), md_s(input), md_s(previous));
                }
                cursor = (cursor + 1) & 511;
            }
            s->delay_filter[line] = mdkit_acc_to_word(tail);
        }
        s->source_filter = mdkit_acc_to_word(filtered);
    }
    for (i = 0; i < 32; ++i) {
        const uint32_t input = w[0x60 + i];
        uint64_t common = mdkit_mac(mdkit_mpy(input, 0x28f5c3), cross, w[0xa0 + i]);
        uint64_t middle, opposite;
        common = mdkit_mac(common, cross, w[0xc0 + i]);
        middle = mdkit_mac(mdkit_mpy(input, 0x28f5c3), ring, w[0x80 + i]);
        opposite = mdkit_sub(mdkit_mpy(input, 0x28f5c3), mdkit_mpy(cross, w[0xa0 + i]));
        opposite = mdkit_mac(opposite, cross, w[0xc0 + i]);
        s->delay_x[0][s->delay_position[0]] = mdkit_acc_to_word(opposite);
        s->delay_x[1][s->delay_position[1]] = mdkit_acc_to_word(common);
        s->delay_y[s->delay_position[2]] = mdkit_acc_to_word(middle);
        for (line = 0; line < 3; ++line) s->delay_position[line] = (s->delay_position[line] + 1) & 511;
    }
    {
        uint64_t feedback = mdkit_from24(s->feedback);
        uint32_t previous = s->feedback_previous;
        for (i = 0; i < 32; ++i) {
            uint64_t difference = mdkit_sub(mdkit_from24(w[0xc0 + i]), mdkit_from24(w[0xa0 + i]));
            uint32_t old_feedback;
            difference = mdkit_add(difference, mdkit_from24(w[0x80 + i]));
            old_feedback = mdkit_acc_to_word(feedback);
            feedback = mdkit_mpy(0x59999a, old_feedback);
            feedback = mdkit_sub(feedback, mdkit_mpy(0x400000, previous));
            previous = mdkit_acc_to_word(difference);
            feedback = mdkit_mac(feedback, 0x400000, previous);
            w[0x40 + i] = mdkit_acc_to_word(mdkit_asl(mdkit_add(mdkit_from24(w[0x40 + i]), mdkit_from24(old_feedback)), 2));
        }
        s->feedback = mdkit_acc_to_word(feedback);
        s->feedback_previous = previous;
    }
#endif
    {
        /* the mode tables interpolated by POS and TUNE (the ROM's words, exact products): made
         * again only when either changes (s->tables; X 0xdc.. is a delay line's too, so they
         * are copied every block) */
        const uint32_t position = p[5], tune = p[4];
        if (s->table_key[0] != 1u || s->table_key[1] != position || s->table_key[2] != tune) {
            uint32_t complement = mdkit_acc_to_word(mdkit_sub(mdkit_from24(0x7fffff), mdkit_from24(position)));
            s->table_key[0] = 1u; s->table_key[1] = position; s->table_key[2] = tune;
            for (i = 0; i < 8; ++i) {
                s->tables[i] = mdkit_acc_to_word(md_acc_mac_x(md_acc_mpy_x(complement, mdkit_pi_mt_modal_x(c, 0x143671 + i)), position, mdkit_pi_mt_modal_x(c, 0x143679 + i)));
                s->tables[8 + i] = mdkit_acc_to_word(md_acc_mac_x(md_acc_mpy_x(complement, mdkit_pi_mt_modal_x(c, 0x143681 + i)), position, mdkit_pi_mt_modal_x(c, 0x143689 + i)));
            }
            complement = mdkit_acc_to_word(mdkit_sub(mdkit_from24(0x7fffff), mdkit_from24(tune)));
            for (i = 0; i < 8; ++i)
                s->tables[16 + i] = mdkit_acc_to_word(md_acc_mac_x(md_acc_mpy_x(complement, mdkit_pi_mt_modal_x(c, 0x143661 + i)), tune, mdkit_pi_mt_modal_x(c, 0x143669 + i)));
        }
        for (i = 0; i < 8; ++i) { w[0xdc + i] = s->tables[i]; w[0xe4 + i] = s->tables[8 + i]; y[0xec + i] = s->tables[16 + i]; }
    }
    mdkit_pi_mt_modal_coefficients(c, y[0xfe], p[1], 0xec, 0xe4);
    mdkit_pi_mt_modal_filter(c);
    for (i = 0; i < 32; ++i) s->block[i] = w[0x40 + i];
}
