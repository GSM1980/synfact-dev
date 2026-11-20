/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/trx_metal.c - the TRX "metal" stages, word for word mdkit's
 * (mdkit/src/trx/{cy,ch,oh,cb}: the same code in each voice), in plain integers.
 *
 * Included by a generated voice in place of one function (tools/md_port.py,
 * NATIVE): MD_METAL_PART picks it, MD_METAL(name) is the voice's name for it.
 *   0 metal_source: six square waves, each +-8387769 times its gain, summed into
 *     the block. acc_to_word reads only an accumulator's high word, and adding
 *     from24(w) changes only it, so a stage is t = (the product's high word) + w,
 *     limited. A square is + below its transition and - from there to its period
 *     (the voice's own metal_square, scanned once), so a source's two products are
 *     made once a block and the block goes by runs of one sign.
 *   1 metal_resonant_lowpass and 2 metal_difference_cascade: two-pole filters. Each
 *     accumulator there is only read as acc_to_word(asl(acc, 1)): with acc =
 *     mpy x1,y1 + mac x2,y2 (+ mac x3,y3) + d * 2^23 (mac x,0x400000 adds x * 2^23,
 *     mac x,0x800000 adds -x * 2^24, so those runs are one integer d), that word is
 *     the limit of floor(S / 2^22) + d, S = x1 y1 + x2 y2 (+ x3 y3), |S| < 3 * 2^46:
 *     no 56-bit wrap is reached; md_floor22 (md-floor.h) computes floor(S / 2^22) exactly.
 *     The reads and writes of the workspace keep mdkit's order.
 * tests/test_md_st.py compares every word with mdkit's.
 */
#include "md-floor.h"

#ifndef MD_METAL_LOWPASS_LOOP
#define MD_METAL_LOWPASS_LOOP
/* The resonant low-pass loop of parts 1 and 5, in place over w[ib..ib + 31]. mdkit's loop
 * reads back what it wrote: d and a, the two words before the sample, are its two last
 * outputs (the history words before the first); the pair w[0], w[1] takes each sample's
 * first word in turn, so it is the last two of those. Kept signed in registers here; w ends
 * as mdkit leaves it. */
static inline void md_metal_lowpass_loop(uint32_t *w, unsigned ib, int32_t sq, int32_t fb, int32_t fd)
{
    int32_t p = md_s(w[0]), q = md_s(w[1]), d = md_s(w[ib - 2]), a = md_s(w[ib - 1]);
    int32_t first = md_floor22(sq, p, fb, q, md_s(w[ib]), fd);
    for (unsigned i = 0; i < 32; ++i) {
        const int32_t out = md_s(md_floor_word(first));
        const uint32_t word = md_floor_word(md_floor22(sq, d, fb, a, out, fd));
        p = q; q = out;
        first = md_floor22(sq, p, fb, q, md_s(w[ib + 1 + i]), fd);
        w[ib + i] = word;
        d = a; a = md_s(word);
    }
    w[0] = md_w24(p); w[1] = md_w24(q);
}
#endif

#if MD_METAL_PART == 0
/* The block goes by runs in which no square changes sign. In a run each sample takes
 * the same six steps v -> limit(v + c), and such steps compose: limit(limit(x + C, lo, hi)
 * + c) = limit(x + C + c, limit(lo + c), limit(hi + c)) (lo <= hi), so the six are one
 * step, made once a run. A sample's six stores and reads in between are mdkit's own
 * values, unread by anything else, so only the last is stored. */
static inline void MD_METAL(metal_source)(uint32_t *w, unsigned start, uint32_t cursors[6], const uint32_t gains[6], int add)
{
    static const unsigned periods[6] = {114, 102, 86, 80, 58, 52};
    static unsigned transition[6], known;
    int32_t high[6], low[6];
    unsigned cursor[6], source, i = 0;
    if (!known) {
        for (source = 0; source < 6; ++source) {
            unsigned c = 0;
            while (c < periods[source] && MD_METAL(metal_square)(source, c) == 8387769u) ++c;
            transition[source] = c;
        }
        known = 1;
    }
    for (source = 0; source < 6; ++source) {
        high[source] = MD_T(md_acc_mpy_x(8387769u, gains[source]));
        low[source] = MD_T(md_acc_mpy_x((uint32_t)-8387769 & 0xffffffu, gains[source]));
        cursor[source] = cursors[source];
    }
    while (i < 32) {
        unsigned n = 32 - i, end;
        int32_t sum = 0, lo = -0x800000, hi = 0x7fffff;
        for (source = 0; source < 6; ++source) {
            const unsigned c = cursor[source], edge = transition[source];
            const unsigned run = (c < edge ? edge : periods[source]) - c;
            const int32_t v = c < edge ? high[source] : low[source];
            if (run < n) n = run;
            if (!source && !add) { sum = 0; lo = hi = md_s(md_floor_word(v)); continue; }
            sum += v; lo = md_s(md_floor_word(lo + v)); hi = md_s(md_floor_word(hi + v));
        }
        end = i + n;
        if (add) {
            for (; i < end; ++i) {
                int32_t x = md_s(w[start + i]) + sum;
                w[start + i] = md_w24(x < lo ? lo : x > hi ? hi : x);
            }
        } else {
            const uint32_t x = md_w24(lo);       /* lo = hi: a constant */
            for (; i < end; ++i) w[start + i] = x;
        }
        for (source = 0; source < 6; ++source) {
            cursor[source] += n;
            if (cursor[source] == periods[source]) cursor[source] = 0;
        }
    }
    for (source = 0; source < 6; ++source) cursors[source] = cursor[source];
}
#elif MD_METAL_PART == 1
static inline void MD_METAL(metal_resonant_lowpass)(uint32_t *w, unsigned input_base, unsigned output_base, uint32_t history[4],
                                                     unsigned index, unsigned resonance)
{
    uint32_t feedback, square, feed;
    MD_METAL(metal_coeff)(index, resonance, &feedback, &square, &feed);
    const int32_t sq = md_s(square), fb = md_s(feedback), fd = md_s(feed);
    w[0] = history[0]; w[1] = history[1];
    w[input_base - 2] = history[2]; w[input_base - 1] = history[3];
    md_metal_lowpass_loop(w, input_base, sq, fb, fd);
    (void)output_base;
    history[0] = w[0]; history[1] = w[1];
    history[3] = w[input_base + 31]; history[2] = w[input_base + 30];
}
#elif MD_METAL_PART == 2
static inline void MD_METAL(metal_difference_cascade)(uint32_t *w, unsigned input_base, unsigned output_base, unsigned history_base,
                                                       uint32_t history[6], unsigned index, unsigned resonance)
{
    uint32_t feedback, square, unused;
    MD_METAL(metal_coeff)(index, resonance, &feedback, &square, &unused);
    const int32_t sq = md_s(square), fb = md_s(feedback);
    uint32_t *h = w + history_base;
    w[history_base] = history[1]; w[history_base + 1] = history[0];
    w[output_base] = history[3]; w[output_base + 1] = history[2];
    w[input_base - 2] = history[5]; w[input_base - 1] = history[4];
    /* Kept signed in registers (mdkit reads them back from w): the input window a0 a1 a2 =
     * w[input_base - 2 + i ..]; the outputs' two words before the sample, x y (the first
     * two from w); the history pair, m the word taken twice and n the other: each sample
     * writes its first word over n, and the two swap. Only the last pair is stored. */
    int32_t m = md_s(h[1]), n = md_s(h[0]), x = md_s(w[output_base]), y = md_s(w[output_base + 1]);
    int32_t a0 = md_s(w[input_base - 2]), a1 = md_s(w[input_base - 1]);
    int32_t first = md_floor22_2(sq, n, fb, m);   /* floor(S / 2^22), d to come */
    for (unsigned i = 0; i < 32; ++i) {
        const int32_t a2 = md_s(w[input_base + i]);
        const int32_t fw = md_s(md_floor_word(first + a0 - 2 * a1 + a2));   /* 1/2, -1, 1/2 */
        const uint32_t word = md_floor_word(md_floor22_2(sq, x, fb, y) + fw - 2 * m + n);
        first = md_floor22_2(sq, m, fw, fb);
        n = m; m = fw;
        w[output_base + 2 + i] = word;
        x = y; y = md_s(word);
        a0 = a1; a1 = a2;
    }
    h[1] = md_w24(m); h[0] = md_w24(n);
    history[0] = h[1]; history[1] = h[0];
    history[2] = w[output_base + 33]; history[3] = w[output_base + 32];
    history[4] = w[input_base + 31]; history[5] = w[input_base + 30];
}
#elif MD_METAL_PART == 3
/* metal_difference_filter (CH, OH): previous is only read as its word, and each
 * new one is asl(mpy 1/2 / mac -1 / mac 1/2 / mac square / mac feedback, 1) */
static inline void MD_METAL(metal_difference_filter)(uint32_t *w, unsigned input_base, unsigned output_base, uint32_t history[4],
                                                      unsigned index, unsigned resonance)
{
    uint32_t feedback, square, unused;
    MD_METAL(metal_coeff)(index, resonance, &feedback, &square, &unused);
    const int32_t sq = md_s(square), fb = md_s(feedback);
    w[input_base - 2] = history[0]; w[input_base - 1] = history[1];
    w[output_base - 2] = history[2]; w[output_base - 1] = history[3];
    /* Kept signed in registers (mdkit reads them back from w): the input window a0 a1 a2 =
     * w[input_base - 2 + i ..] and the word before the sample, prior (its last output, the
     * history word before the first). */
    int32_t previous = md_s(w[output_base - 1]);          /* from24(w): its word is w */
    int32_t prior = md_s(w[output_base - 2]), a0 = md_s(w[input_base - 2]), a1 = md_s(w[input_base - 1]);
    for (unsigned i = 0; i < 32; ++i) {
        const int32_t a2 = md_s(w[input_base + i]);
        const uint32_t word = md_floor_word(previous);
        const int32_t ws = md_s(word);
        previous = md_floor22_2(sq, prior, fb, ws) + a0 - 2 * a1 + a2;
        w[output_base - 1 + i] = word;
        prior = ws; a0 = a1; a1 = a2;
    }
    history[2] = w[output_base + 30]; history[3] = md_floor_word(previous); w[output_base + 31] = history[3];
    history[1] = w[input_base + 31]; history[0] = w[input_base + 30];
}
#elif MD_METAL_PART == 4
/* MD_CP6_EXACT (TRX-CP, under MD_AP_FLOOR): this filter keeps the exact floor; it took the
 * EMAC's read at -30 dBFS of error there (TRX-MA takes it) */
#ifdef MD_CP6_EXACT
#define MD_CP6_FLOOR md_floor22_2_x
#else
#define MD_CP6_FLOOR md_floor22_2
#endif
/* cp_filter6 (MA, CP): its coefficient index walks {square, 1/2, -1, 1/2} and
 * comes back to 1 every sample (eight steps), so each sample is the cascade's:
 * first = limit(floor(S / 2^22) + s0 - 2 s1 + s2), second the same with
 * output - 2 old_history + w[h]. The aux read is mdkit's (its value unused). */
static inline void MD_METAL(cp_filter6)(uint32_t *w, uint32_t *aux, unsigned input_base, unsigned output_base, unsigned history_base,
                                         uint32_t history[6], uint32_t position, uint32_t width)
{
    uint32_t feedback, square;
    MD_METAL(cp_coefficients)(position, width, &feedback, &square);
    const int32_t sq = md_s(square), fb = md_s(feedback);
    w[history_base] = history[1]; w[history_base + 1] = history[0];
    w[output_base] = history[3]; w[output_base + 1] = history[2];
    w[input_base] = history[5]; w[input_base + 1] = history[4];
    /* What the loop reads back of its own writes is kept signed in registers: the input
     * window a0 a1 a2 slides by one (a2 = w[input_base + 2 + i]); old_output and recent are
     * the outputs of the two samples before (the first two from w); the two history words
     * swap each sample (w[h] = output, then the other is read), so only the last pair is
     * stored. The outputs are stored as mdkit stores them. */
    const unsigned out0 = output_base + 2;
    unsigned h = history_base ^ 1u, i;
    int32_t ha = md_s(w[h]), hb = md_s(w[h ^ 1u]);
    int32_t first = MD_CP6_FLOOR(sq, hb, ha, fb);
    int32_t a0 = md_s(w[input_base]), a1 = md_s(w[input_base + 1]);
    int32_t old_output = md_s(w[output_base]), recent = md_s(w[output_base + 1]);
    (void)aux;
    for (i = 0; i < 32; ++i) {
        const int32_t a2 = md_s(w[input_base + 2 + i]);
        const int32_t output = md_s(md_floor_word(first + a0 - 2 * a1 + a2));
        const int32_t second = MD_CP6_FLOOR(sq, old_output, fb, recent) + output - 2 * ha + hb;
        const uint32_t word = md_floor_word(second);
        first = MD_CP6_FLOOR(sq, ha, output, fb);
        hb = ha; ha = output;
        w[out0 + i] = word;
        old_output = recent; recent = md_s(word);
        a0 = a1; a1 = a2;
    }
    /* h flipped 32 times: back where it started, holding ha, the other hb */
    w[h] = md_w24(ha); w[h ^ 1u] = md_w24(hb);
    history[0] = w[h]; history[1] = w[h ^ 1u];
    history[2] = w[out0 + 31]; history[3] = w[out0 + 30];
    history[4] = w[input_base + 33]; history[5] = w[input_base + 32];
}
#undef MD_CP6_FLOOR
#elif MD_METAL_PART == 5
/* cp_filter4 (CP): its coefficients as mdkit makes them, then the resonant
 * low-pass loop of part 1 (its history words in another order) */
static inline void MD_METAL(cp_filter4)(uint32_t *w, unsigned input_base, uint32_t history[4], uint32_t position, uint32_t width)
{
    uint32_t feedback, square;
    MD_METAL(cp_coefficients)(position, width, &feedback, &square);
    const unsigned ni = (position + MD_METAL(cp_filter_a)(width)) & 0xffffffu, ri = (position + MD_METAL(cp_filter_b)(width)) & 0xffffffu;
    const md_acc doubled = md_acc_asl(md_acc_mpy_x(MD_METAL(cp_filter)(ri), MD_METAL(cp_norm)(ni)), 1);
    const uint32_t feed = md_acc_acc_to_word(md_acc_sub(md_acc_from24(0x400000), md_acc_add(doubled, md_acc_from24(square))));
    const int32_t sq = md_s(square), fb = md_s(feedback), fd = md_s(feed);
    w[0] = history[0]; w[1] = history[1]; w[input_base - 2] = history[3]; w[input_base - 1] = history[2];
    md_metal_lowpass_loop(w, input_base, sq, fb, fd);
    history[0] = w[0]; history[1] = w[1]; history[2] = w[input_base + 31]; history[3] = w[input_base + 30];
}
#endif
#undef MD_METAL_PART
#undef MD_METAL
