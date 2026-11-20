/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_delays.c - a PI voice's delay lines, word for word mdkit's
 * drum_delays (mdkit/src/pi/sd/mdkit_pi_sd.c), with fewer products.
 *
 * mdkit follows the DSP: "mac a,g" then "sub mpy b,g" is two products. Their
 * sum is acc + 2 g (a - b), one product (md_acc_mac_diff); the high word of
 * "mpy x,y + mac u,v (+ mac u,v')" is md_acc_word2 with v + v'. The order of
 * the loops, every store and every saturation are mdkit's.
 * Included by the generated voice in place of drum_delays (tools/md_port.py,
 * NATIVE). tests/test_md_st.py compares every word with mdkit's.
 */
#ifndef MD_PI_T                       /* the voice's types: PI-SD's unless the voice names its own */
#define MD_PI_T mdkit_pi_sd
#define MD_PI_MODAL_T mdkit_pi_sd_modal
#endif
/* The one-pole filters' accumulators start a block from a word and end it as one. Under
 * MD_AP_ACC (MD_ST_APPROX) each is one word, floor(acc / 2^18), stepped by the EMAC's read
 * of its product and half a unit every other sample (native/pi_modal_filter.c says why);
 * |acc| stays under 2^25 words. The noise filter stays exact: it took this at -52 dBFS of
 * error. Otherwise md_acc. */
#ifdef MD_AP_ACC
typedef int32_t md_pd_acc;
#define MD_PD_FROM24(w) ((int32_t)((uint32_t)md_s(w) << 6))
#define MD_PD_WORD(a) md_sat((a) >> 6)
#define MD_PD_DIFF(a, g, x, o, i) ((a) + md_ar1((g), (x) - (o)) + (int32_t)((i) & 1u))
#define MD_PD_TOWORD(a) md_w24(md_sat((a) >> 6))
#else
typedef md_acc md_pd_acc;
#define MD_PD_FROM24(w) md_acc_from24(w)
#define MD_PD_WORD(a) md_acc_word_s(a)
#define MD_PD_DIFF(a, g, x, o, i) md_acc_mac_diff((a), (g), (x), (o))
#define MD_PD_TOWORD(a) md_acc_acc_to_word(a)
#endif
#ifdef MD_AP_ACC
/* Under MD_AP_ACC each one-pole is a loop of its own (they are independent sample by
 * sample), over its ring's one or two contiguous runs; the same words. */
static inline int32_t md_pd_ring(int32_t a, int32_t g, const uint32_t *ring, unsigned cursor, uint32_t *out)
{
    unsigned i = 0, run = 512u - cursor;
    const uint32_t *in = ring + cursor;
    if (run > 32u) run = 32u;
    for (; i < run; ++i) {
        const int32_t old = md_sat(a >> 6);
        out[i] = md_w24(old);
        a += md_ar1(g, md_s(in[i]) - old) + (int32_t)(i & 1u);
    }
    in = ring - run;                          /* the wrap: ring[i - run] */
    for (; i < 32u; ++i) {
        const int32_t old = md_sat(a >> 6);
        out[i] = md_w24(old);
        a += md_ar1(g, md_s(in[i]) - old) + (int32_t)(i & 1u);
    }
    return a;
}
#ifdef MD_AP_STRUCT
/* MD_AP_STRUCT (a voice judged by its spectrum): the three delay lines at half rate. The network's one-poles
 * keep it under about 5 kHz, so it runs on 16 points a block: each line keeps 16 words a
 * block, its delay halved (245, 257 and 331 samples: 122.5, 128.5 and 165.5, the half from
 * two taps' mean), each one-pole's gain for two steps (2 g - g^2), its words at the even
 * samples and their means between. The noise, the rattle and the modes stay at full rate. */
static inline int32_t md_pd_gain2(int32_t g)
{
    const int32_t g2 = 2 * g - md_mul_shift_exact(g, g, 23);
    return g2 > 0x7fffff ? 0x7fffff : g2;
}
#ifdef MD_PD_WHOLE
/* the delays rounded to whole words (123, 129 and 166: 0.2 to 0.4 % longer), read with the
 * ring's wrap; the words between as means, in the same loop. One function for the three
 * lines, one loop: its code is fetched once a block (inlined, the three lines' two-run
 * copies were about 1.2 KB of code a block) */
static __attribute__((noinline)) int32_t md_pd_half(int32_t a, int32_t g2, const uint32_t *ring, unsigned position, unsigned delay, uint32_t *out)
{
    unsigned at = position - delay - 1u, j;
    int32_t last = md_sat(a >> 6);
    out[0] = md_w24(last);
    for (j = 0; j < 16u; ++j, ++at) {
        const int32_t word = j ? md_sat(a >> 6) : last;
        if (j) { out[2 * j - 1] = md_w24((last + word) >> 1); out[2 * j] = md_w24(word); last = word; }
        a += md_ar1(g2, md_s(ring[at & 511u]) - word) + (int32_t)(j & 1u);
    }
    out[31] = md_w24((last + md_sat(a >> 6)) >> 1);
    return a;
}
#else
/* one function for the three lines, as above */
static __attribute__((noinline)) int32_t md_pd_half(int32_t a, int32_t g2, const uint32_t *ring, unsigned position, unsigned delay, uint32_t *out)
{
    int32_t word[17];
    for (unsigned j = 0; j < 16; ++j) {
        const unsigned at = (position + j - delay) & 511u;
        const int32_t x = (md_s(ring[at]) + md_s(ring[(at - 1u) & 511u])) >> 1;
        word[j] = md_sat(a >> 6);
        a += md_ar1(g2, x - word[j]) + (int32_t)(j & 1u);
    }
    word[16] = md_sat(a >> 6);
    for (unsigned j = 0; j < 16; ++j) {
        out[2 * j] = md_w24(word[j]);
        out[2 * j + 1] = md_w24((word[j] + word[j + 1]) >> 1);
    }
    return a;
}
#endif
#endif
#if defined(MD_PI_HALF) && defined(MD_AP_STRUCT)
#include "pi_sd_half_delays.c"
#else
static __attribute__((unused)) void drum_delays(MD_PI_T *s, uint32_t excitation_gain, uint32_t ring_gain, uint32_t noise_gain)
{
    uint32_t *w = s->body.work, *y = s->body.fraction;
    const int32_t eg = md_s(excitation_gain), rg = md_s(ring_gain), ng = md_s(noise_gain);
    unsigned i;
    {                                         /* the excitation filter (lane 0's) */
        const uint32_t *in = s->variant ? y + 0x40 : w + 0x40;
        int32_t excitation = MD_PD_FROM24(s->source_filter);
        for (i = 0; i < 32; ++i) {
            const int32_t old = md_sat(excitation >> 6);
            w[0x60 + i] = md_w24(old);
            excitation += md_ar1(eg, md_s(in[i]) - old) + (int32_t)(i & 1u);
        }
        s->source_filter = md_w24(md_sat(excitation >> 6));
    }
#ifdef MD_AP_STRUCT
    {
        const int32_t rg2 = md_pd_gain2(rg);
        s->delay_filter[0] = md_w24(md_sat(md_pd_half(MD_PD_FROM24(s->delay_filter[0]), rg2, s->delay_x[0], s->delay_position[0], 122, w + 0x80) >> 6));
        s->delay_filter[1] = md_w24(md_sat(md_pd_half(MD_PD_FROM24(s->delay_filter[1]), rg2, s->delay_x[1], s->delay_position[1], 128, w + 0xa0) >> 6));
    }
#else
    s->delay_filter[0] = md_w24(md_sat(md_pd_ring(MD_PD_FROM24(s->delay_filter[0]), rg, s->delay_x[0],
                                                   (s->delay_position[0] + 267) & 511, w + 0x80) >> 6));
    s->delay_filter[1] = md_w24(md_sat(md_pd_ring(MD_PD_FROM24(s->delay_filter[1]), rg, s->delay_x[1],
                                                   (s->delay_position[1] + 255) & 511, w + 0xa0) >> 6));
#endif
    {
        md_acc sum = md_acc_from24(s->noise_a);
        uint32_t next = s->noise_b;
        for (i = 0; i < 16; ++i) {
            uint32_t old = next;
            sum = md_acc_add(sum, md_acc_from24(next));
            y[2 * i] = old;
            next = md_acc_mid(sum);
            y[2 * i + 1] = next;
            next = (old + next) & 0xffffffu;
        }
        s->noise_a = next; s->noise_b = md_acc_mid(sum); y[0xfa] = md_acc_mid(sum);
    }
#ifdef MD_AP_STRUCT
    s->delay_filter[2] = md_w24(md_sat(md_pd_half(MD_PD_FROM24(s->delay_filter[2]), md_pd_gain2(ng), s->delay_y, s->delay_position[2], 165, w + 0xc0) >> 6));
#else
    s->delay_filter[2] = md_w24(md_sat(md_pd_ring(MD_PD_FROM24(s->delay_filter[2]), ng, s->delay_y,
                                                   (s->delay_position[2] + 181) & 511, w + 0xc0) >> 6));
#endif
    {
        uint32_t residual = 0, rattle, small;
#ifdef MD_AP_STRUCT
        /* MD_AP_STRUCT (a voice judged by its spectrum): the leaky sum as the one-poles above,
         * one word, floor(acc / 2^18) (-52 dBFS against mdkit's words alone) */
        int32_t noise = MD_PD_FROM24(s->noise_filter);
        for (i = 0; i < 32; ++i) {
            const int32_t before = md_sat(noise >> 6);
            const uint32_t n = y[i];
            y[i] = md_w24(before);
            noise += (int32_t)((uint32_t)md_s(n) << 6) + md_ar1(-0x266666, before) + (int32_t)(i & 1u);
        }
        residual = y[31];
        s->noise_filter = md_w24(md_sat(noise >> 6));
#else
        md_acc noise = md_acc_from24(s->noise_filter);   /* exact: a leaky sum the approximation does not take */
        for (i = 0; i < 32; ++i) {
            const int32_t before = md_acc_word_s(noise);
            const uint32_t n = y[i];
            y[i] = md_w24(before);
            noise = md_acc_add(md_acc_add(noise, md_acc_from24(n)), md_acc_product_x(-0x266666, before, 1));   /* 0xd9999a */
        }
        residual = y[31];
        s->noise_filter = md_acc_acc_to_word(noise);
#endif
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
            for (i = 0; i < 32; ++i) {                /* one product, added to both */
                const int32_t r = md_mul_shift_exact(sm, md_s(y[i]), 23);
                w[0x40 + i] = md_w24(md_sat(md_s(w[0x40 + i]) + r));
                w[0x60 + i] = md_w24(md_sat(md_s(w[0x60 + i]) + r));
            }
        } else {
            const int32_t large = md_word_mpy_exact(0x59999a, md_s(rattle)), sm = md_s(small);
            uint32_t carry = md_s(w[0x5f]) & 0xffffffu;   /* acc_to_word(from24(w)) is w */
            for (i = 0; i < 32; ++i) {
                const int32_t yi = md_s(y[i]);
                w[0x5f + i] = carry;
                w[0x40 + i] = md_w24(md_sat(2 * md_s(y[0x40 + i]) + md_mul_shift_exact(large, yi, 23)));
                carry = md_w24(md_sat(md_s(w[0x60 + i]) + md_mul_shift_exact(sm, yi, 23)));
            }
            w[0x7f] = carry;
        }
    }
#ifdef MD_AP_STRUCT
    {
        /* the lines' 16 words a block, from the even samples' words and the source's pair means */
        const int32_t gain = md_s(w[0xfb]), cross = md_s(w[0xfc]);
        const unsigned first = s->delay_position[0], second = s->delay_position[1], third = s->delay_position[2];
        for (i = 0; i < 16; ++i) {
            const int32_t a = (md_s(w[0x60 + 2 * i]) + md_s(w[0x61 + 2 * i])) >> 1;
            const int32_t ra = md_s(w[0xa0 + 2 * i]), rc = md_s(w[0xc0 + 2 * i]);
            const int32_t shared = md_ar1(a, 0x7eb852);
            s->delay_x[1][(second + i) & 511] = md_w24(md_sat((shared + md_ar1(cross, ra + rc)) >> 6));
            s->delay_x[0][(first + i) & 511] = md_w24(md_sat((shared + md_ar1(cross, rc - ra)) >> 6));
            s->delay_y[(third + i) & 511] = md_w24(md_sat((shared + md_ar1(gain, md_s(w[0x80 + 2 * i]))) >> 6));
        }
        s->delay_position[0] = (first + 16) & 511; s->delay_position[1] = (second + 16) & 511;
        s->delay_position[2] = (third + 16) & 511;
    }
#else
    {
        const int32_t gain = md_s(w[0xfb]), cross = md_s(w[0xfc]);
        const unsigned first_cursor = s->delay_position[0], second_cursor = s->delay_position[1], third_cursor = s->delay_position[2];
        for (i = 0; i < 32; ++i) {
            const int32_t a = md_s(w[0x60 + i]), ra = md_s(w[0xa0 + i]), rc = md_s(w[0xc0 + i]);
#ifdef MD_AP_STRUCT
            /* the source's product once for the three words (the read of each sum's two) */
            const int32_t shared = md_ar1(a, 0x7eb852);
            const uint32_t even = md_w24(md_sat((shared + md_ar1(cross, ra + rc)) >> 6));
            const uint32_t odd = md_w24(md_sat((shared + md_ar1(cross, rc - ra)) >> 6));
            s->delay_y[(third_cursor + i) & 511] = md_w24(md_sat((shared + md_ar1(gain, md_s(w[0x80 + i]))) >> 6));
#else
            const uint32_t even = md_acc_word2(a, 0x7eb852, cross, ra + rc);
            const uint32_t odd = md_acc_word2(a, 0x7eb852, cross, rc - ra);
            s->delay_y[(third_cursor + i) & 511] = md_acc_word2(a, 0x7eb852, gain, md_s(w[0x80 + i]));
#endif
            y[i] = even; y[32 + i] = odd;
            s->delay_x[0][(first_cursor + i) & 511] = odd;
            s->delay_x[1][(second_cursor + i) & 511] = even;
        }
        s->delay_position[0] = (first_cursor + 32) & 511; s->delay_position[1] = (second_cursor + 32) & 511;
        s->delay_position[2] = (third_cursor + 32) & 511;
    }
#endif
}
#endif
#else
static __attribute__((unused)) void drum_delays(MD_PI_T *s, uint32_t excitation_gain, uint32_t ring_gain, uint32_t noise_gain)
{
    uint32_t *w = s->body.work, *y = s->body.fraction;
    const int32_t eg = md_s(excitation_gain), rg = md_s(ring_gain), ng = md_s(noise_gain);
    md_pd_acc excitation = MD_PD_FROM24(s->source_filter);
    unsigned lane, i;
    for (lane = 0; lane < 2; ++lane) {
        unsigned cursor = (s->delay_position[lane] + (lane ? 255 : 267)) & 511;
        md_pd_acc delayed = MD_PD_FROM24(s->delay_filter[lane]);
        for (i = 0; i < 32; ++i) {
            int32_t old;
            if (!lane) {
                old = MD_PD_WORD(excitation);
                w[0x60 + i] = md_w24(old);
                excitation = MD_PD_DIFF(excitation, eg, md_s(s->variant ? y[0x40 + i] : w[0x40 + i]), old, i);
            }
            old = MD_PD_WORD(delayed);
            w[0x80 + 32 * lane + i] = md_w24(old);
            delayed = MD_PD_DIFF(delayed, rg, md_s(s->delay_x[lane][cursor]), old, i);
            cursor = (cursor + 1) & 511;
        }
        s->delay_filter[lane] = MD_PD_TOWORD(delayed);
    }
    s->source_filter = MD_PD_TOWORD(excitation);
    {
        md_acc sum = md_acc_from24(s->noise_a);
        uint32_t next = s->noise_b;
        for (i = 0; i < 16; ++i) {
            uint32_t old = next;
            sum = md_acc_add(sum, md_acc_from24(next));
            y[2 * i] = old;
            next = md_acc_mid(sum);
            y[2 * i + 1] = next;
            next = (old + next) & 0xffffffu;
        }
        s->noise_a = next; s->noise_b = md_acc_mid(sum); y[0xfa] = md_acc_mid(sum);
    }
    {
        unsigned cursor = (s->delay_position[2] + 181) & 511;
        md_pd_acc delayed = MD_PD_FROM24(s->delay_filter[2]);
#if defined(MD_AP_STRUCT) && defined(MD_AP_ACC)
        /* MD_AP_STRUCT (a voice judged by its spectrum): the leaky sum as the one-poles above,
         * one word, floor(acc / 2^18) (-52 dBFS against mdkit's words alone) */
        int32_t noise = MD_PD_FROM24(s->noise_filter);
#define MD_PD_NOISE_BEFORE(a) md_sat((a) >> 6)
#define MD_PD_NOISE_STEP(a, n, before, i) ((a) + ((int32_t)((uint32_t)md_s(n) << 6)) + md_ar1(-0x266666, (before)) + (int32_t)((i) & 1u))
#define MD_PD_NOISE_OUT(a) md_w24(md_sat((a) >> 6))
#else
        md_acc noise = md_acc_from24(s->noise_filter);   /* exact: a leaky sum the approximation does not take */
#define MD_PD_NOISE_BEFORE(a) md_acc_word_s(a)
#define MD_PD_NOISE_STEP(a, n, before, i) md_acc_add(md_acc_add((a), md_acc_from24(n)), md_acc_product_x(-0x266666, (before), 1))   /* 0xd9999a */
#define MD_PD_NOISE_OUT(a) md_acc_acc_to_word(a)
#endif
        uint32_t residual = 0, rattle, small;
        for (i = 0; i < 32; ++i) {
            const int32_t old = MD_PD_WORD(delayed), before = MD_PD_NOISE_BEFORE(noise);
            const uint32_t n = y[i];
            w[0xc0 + i] = md_w24(old);
            delayed = MD_PD_DIFF(delayed, ng, md_s(s->delay_y[cursor]), old, i);
            y[i] = md_w24(before);
            noise = MD_PD_NOISE_STEP(noise, n, before, i);
            residual = md_w24(before);
            cursor = (cursor + 1) & 511;
        }
        s->delay_filter[2] = MD_PD_TOWORD(delayed);
        s->noise_filter = MD_PD_NOISE_OUT(noise);
#undef MD_PD_NOISE_BEFORE
#undef MD_PD_NOISE_STEP
#undef MD_PD_NOISE_OUT
        {
            int32_t absnoise = md_s(y[0xfa]);
            if (absnoise < 0) absnoise = -absnoise;
            if (absnoise >= md_s(s->rattle_threshold)) { residual = 0x7fffffu; s->rattle_gate = residual; }
        }
        rattle = md_w24(md_word_mpy_exact(md_s(s->rattle_threshold), md_s(residual)));
        rattle = md_w24(md_word_mpy_exact(md_s(s->parameters[5]), md_s(rattle)));
        small = md_w24(md_word_mpy_exact(0x266666, md_s(rattle)));
        if (!s->variant) {
            for (i = 0; i < 32; ++i) {
                w[0x40 + i] = md_w24(md_sat(md_s(w[0x40 + i]) + md_mul_shift_exact(md_s(small), md_s(y[i]), 23)));
                w[0x60 + i] = md_w24(md_sat(md_s(w[0x60 + i]) + md_mul_shift_exact(md_s(small), md_s(y[i]), 23)));
            }
        } else {
            const int32_t large = md_word_mpy_exact(0x59999a, md_s(rattle));
            uint32_t carry = md_s(w[0x5f]) & 0xffffffu;   /* acc_to_word(from24(w)) is w */
            for (i = 0; i < 32; ++i) {
                const int32_t yi = md_s(y[i]);
                w[0x5f + i] = carry;
                /* asl(from24(Y 0x40+i), 1) + mac large,y: 2 x + floor(large y / 2^23), limited */
                w[0x40 + i] = md_w24(md_sat(2 * md_s(y[0x40 + i]) + md_mul_shift_exact(large, yi, 23)));
                carry = md_w24(md_sat(md_s(w[0x60 + i]) + md_mul_shift_exact(md_s(small), yi, 23)));
            }
            w[0x7f] = carry;
        }
    }
    {
        const int32_t gain = md_s(w[0xfb]), cross = md_s(w[0xfc]);
        unsigned first_cursor = s->delay_position[0], second_cursor = s->delay_position[1], third_cursor = s->delay_position[2];
        for (i = 0; i < 32; ++i) {
            const int32_t a = md_s(w[0x60 + i]), ra = md_s(w[0xa0 + i]), rc = md_s(w[0xc0 + i]);
            y[i] = md_acc_word2(a, 0x7eb852, cross, ra + rc);
            s->delay_y[third_cursor] = md_acc_word2(a, 0x7eb852, gain, md_s(w[0x80 + i]));
            third_cursor = (third_cursor + 1) & 511;
            y[32 + i] = md_acc_word2(a, 0x7eb852, cross, rc - ra);
        }
        for (i = 0; i < 32; ++i) {
            s->delay_x[0][first_cursor] = y[32 + i]; first_cursor = (first_cursor + 1) & 511;
            s->delay_x[1][second_cursor] = y[i]; second_cursor = (second_cursor + 1) & 511;
        }
        s->delay_position[0] = first_cursor; s->delay_position[1] = second_cursor; s->delay_position[2] = third_cursor;
    }
}
#endif
#undef MD_PD_FROM24
#undef MD_PD_WORD
#undef MD_PD_DIFF
#undef MD_PD_TOWORD
#undef MD_PI_T
#undef MD_PI_MODAL_T
