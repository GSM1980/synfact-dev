/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_metal.c - PI-CC, HH and RC's metal_filter, metal_delay_read and
 * metal_delay_write, word for word mdkit's (the same code in the three,
 * mdkit/src/pi/{cc,hh,rc}). MD_PM_PART picks the function, MD_PM_T is the voice's
 * state type.
 *   0 metal_filter: running persists from block to block, so it stays an md_acc; a
 *     sample's MACs before the word is taken (four) and after (two) are each one exact
 *     sum (md_acc_mac3 + md_acc_mac, md_acc_mac2). The workspace's reads and writes
 *     keep mdkit's order (the filter reads back what it wrote). Exact also under MD_AP_ACC
 *     (the _x forms): a fourth-order recursion, it took the approximate products at
 *     -27 dBFS of error.
 *   1 metal_delay_read: "mac gain,source / sub mpy gain,old" is one product of gain
 *     and source - old (md_acc_mac_diff); the accumulators persist across the eight.
 *   2 metal_delay_write: first and second are new each sample and only read as
 *     acc_to_word: mpy is 2xy, so the word is the limit of floor(S / 2^23), S = g (+-x0
 *     + x1) + drive x2 (or g (+-x0 - x1) + drive x2): md_floor22_2 (exact, md-floor.h),
 *     halved. negative negates the first product of both.
 * Under the approximate forms (MD_AP_STRUCT with MD_AP_ACC for the filter, MD_AP_ACC for
 * the delay reads and the coupling's mix, MD_AP_FLOOR for the delay writes), the EMAC's
 * operands are shifted once (md_ar1s, md_ar2s, md_ar4s): a word goes in as w << 8 or
 * (w << 8) >> 2, with no separate sign extension; the limits are tested once for the two
 * words a sample forms; and with neither delay page wrapping in the eight samples, the
 * delay loops use straight pointers. The same words as before (the hashes in WORK.md).
 * Code the block runs is weighed against its instruction-cache misses: the filter under
 * `struct` is one function for its three calls, the delay write one loop for both signs.
 * Included by the generated voice in place of the function (tools/md_port.py, NATIVE).
 */
#include "md-floor.h"
#if MD_PM_PART == 0
#if defined(MD_AP_STRUCT) && defined(MD_AP_ACC)
/* MD_AP_STRUCT with MD_AP_ACC (a voice judged by its spectrum): the filter's products from the
 * EMAC's read alone, as `acc` takes them elsewhere, and its accumulator as one word in 2^-6
 * of a word, s = floor(acc / 2^18), with h its A0 bit at 2^17: each read r adds r and a half
 * unit (md_acc_from_read's), s += r + h, h ^= 1; the same words while A0 holds nothing under
 * 2^17 and |acc| < 2^25 words (four times full scale). The word is s >> 6 limited; what is
 * stored is to48's, the word and A0 limited together. */
static __attribute__((noinline)) void metal_filter(MD_PM_T *s, unsigned base, uint32_t *history, uint32_t *fraction, uint32_t *previous, const uint32_t coeff[5])
{
    /* the EMAC's operands shifted once: each word as w << 8 (its sign extension is the shift's),
     * each coefficient as MD_EY (md_ar4s, md_ar2s: the same products) */
    const int32_t c0 = MD_EY(md_s(coeff[0])), c1 = MD_EY(md_s(coeff[1])), c2 = MD_EY(md_s(coeff[2])),
                  c3 = MD_EY(md_s(coeff[3])), c4 = MD_EY(md_s(coeff[4]));
    uint32_t *w = s->work;
    for (unsigned i = 0; i < 5; ++i) s->fraction[40 + i] = coeff[i];
    unsigned output = base + 2;
    int32_t a = (int32_t)((uint32_t)md_s(*history) << 6) | (int32_t)((*fraction & 0xffffffu) >> 18);
    const uint32_t h0 = (*fraction >> 17) & 1u, h1 = h0 ^ 1u;
    uint32_t low = 0;
    /* the inputs w[output..output + 2] are read before w[output] is written, so they slide in
     * registers; the word written the turn before (w[base + 1] first) is the last cached.
     * The four products before the stored word are one read with their two half units
     * (one unit, h unchanged). h alternates with each read after the word: two samples a
     * turn, h1 then h0 */
    int32_t cached = (int32_t)(w[base + 1] << 8);
    int32_t x0 = (int32_t)(w[output] << 8), x1 = (int32_t)(w[output + 1] << 8);
    a += md_ar2s((int32_t)(w[base] << 8), c0, cached, c1) + (int32_t)h0;
#define MD_PM_FILTER_STEP(h)                                                                \
    do {                                                                                    \
        const int32_t x2 = (int32_t)(w[output + 2] << 8), before = cached;                  \
        a += md_ar4s(cached, c1, x0, c2, x1, c3, x2, c4) + 1;                               \
        int32_t word = a >> 6;                                                              \
        uint32_t f;                                                                         \
        low = (((uint32_t)a & 63u) << 18) | ((h) << 17);                                    \
        if (__builtin_expect((uint32_t)word + 0x800000u < 0x1000000u, 1)) f = low;          \
        else if (word > 0) { word = 0x7fffff; f = 0xffffffu; }                              \
        else { word = -0x800000; f = 0u; }                                                  \
        w[output] = md_w24(word); s->fraction[output] = f;                                  \
        cached = (int32_t)((uint32_t)word << 8);                                            \
        a += md_ar2s(before, c0, cached, c1) + (int32_t)(h);                                \
        ++output; x0 = x1; x1 = x2;                                                         \
    } while (0)
    for (unsigned i = 0; i < 16; ++i) { MD_PM_FILTER_STEP(h1); MD_PM_FILTER_STEP(h0); }
#undef MD_PM_FILTER_STEP
    *history = w[output - 1]; *fraction = low; *previous = w[output - 2];
}
#else
static inline void metal_filter(MD_PM_T *s, unsigned base, uint32_t *history, uint32_t *fraction, uint32_t *previous, const uint32_t coeff[5])
{
    const int32_t c0 = md_s(coeff[0]), c1 = md_s(coeff[1]), c2 = md_s(coeff[2]), c3 = md_s(coeff[3]), c4 = md_s(coeff[4]);
    uint32_t *w = s->work;
    for (unsigned i = 0; i < 5; ++i) s->fraction[40 + i] = coeff[i];
    unsigned p = base + 2, output = base + 2;
    md_acc running = md_acc_from48(md_acc_pack48(*history, *fraction)), stored = md_acc_make(0, 0);
    int32_t cached = md_s(w[base + 1]);
    running = md_acc_mac2_x(running, c0, md_s(w[base]), c1, cached);
    for (unsigned i = 0; i < 32; ++i) {
        running = md_acc_mac3_x(running, c1, cached, c2, md_s(w[p]), c3, md_s(w[p + 1]));
        running = md_acc_add(running, md_acc_product_x(c4, md_s(w[p + 2]), 1));
        stored = running;
        cached = md_s(md_acc_acc_to_word(running));
        running = md_acc_mac2_x(running, c0, md_s(w[p - 1]), c1, cached);
        const md_acc limited = md_acc_to48(stored);
        w[output] = md_acc_mid(limited); s->fraction[output] = md_acc_low(limited); ++output;
        ++p;
    }
    *history = w[output - 1]; *fraction = md_acc_low(stored); *previous = w[output - 2];
}
#endif
#endif
#if (MD_PM_PART == 1 || MD_PM_PART == 2) && !defined(MD_PM_PAGE)
#define MD_PM_PAGE 1   /* once in the voice: parts 1 and 2 share it */
/* A page of 256 words (the delay reads' and writes' modulo 0xff block) inside the voice's delay
 * words: metal_read and metal_write would reach s->delay for each of its addresses, so they
 * are read and written there directly; elsewhere (null) through them. */
static inline uint32_t *md_pm_page(MD_PM_T *s, uint32_t address)
{
    const uint32_t at = (address & 0xffff00u) - 0x135600u;
    return at <= 1024u - 256u ? s->delay + at : (uint32_t *)0;
}
#endif
#if MD_PM_PART == 1
/* The two one-poles start from a word: under MD_AP_ACC each is one word, floor(acc / 2^18),
 * stepped by the EMAC's read and a unit every other sample, the same words as md_acc with
 * the read's half units (native/pi_delays.c says why). */
#ifdef MD_AP_ACC
typedef int32_t md_pm_acc;
#define MD_PM_FROM24(w) ((int32_t)((uint32_t)md_s(w) << 6))
#define MD_PM_WORD(a) md_w24(md_sat((a) >> 6))
#define MD_PM_DIFF(a, g, x, o, i) ((a) + md_ar1((g), (x) - (o)) + (int32_t)((i) & 1u))
#else
typedef md_acc md_pm_acc;
#define MD_PM_FROM24(w) md_acc_from24(w)
#define MD_PM_WORD(a) md_acc_acc_to_word(a)
#define MD_PM_DIFF(a, g, x, o, i) md_acc_mac_diff((a), (g), (x), (o))
#endif
#define MD_PM_READ(page, address) ((page) ? (page)[(address) & 0xffu] : metal_read(s, 0, (address)))
static inline void metal_delay_read(MD_PM_T *s, uint32_t page_a, uint32_t page_b, uint32_t stride_a, uint32_t stride_b,
                                    uint32_t *history_a, uint32_t *history_b, unsigned out_a, unsigned out_b)
{
    uint32_t a = metal_advance(page_a, md_s(stride_a), 0xff), b = metal_advance(page_b, md_s(stride_b), 0xff);
    const uint32_t *pa = md_pm_page(s, a), *pb = md_pm_page(s, b);
    md_pm_acc first = MD_PM_FROM24(*history_a), second = MD_PM_FROM24(*history_b);
    const int32_t gain = md_s(s->fraction[7]);
    uint32_t *out1 = s->work + out_a, *out2 = s->work + out_b;
#ifdef MD_AP_ACC
    if (pa && pb) {                       /* both pages in the delay words: an index each */
        /* each old word kept as its value (md_s of the word is the limited value), two samples
         * a turn for the unit every other sample */
        const unsigned ia = a & 0xffu, ib = b & 0xffu;
        const int32_t g = MD_EX(gain);
        /* the two old words limited together when both are in range (the usual case); with
         * neither page wrapping in the eight samples, straight pointers */
#define MD_PM_READ_STEP(i, SOURCE_A, SOURCE_B, UNIT)                                               \
        do {                                                                                      \
            int32_t old_a = first >> 6, old_b = second >> 6;                                      \
            if (__builtin_expect((((uint32_t)old_a + 0x800000u) | ((uint32_t)old_b + 0x800000u)) >= 0x1000000u, 0)) { \
                old_a = md_sat(old_a); old_b = md_sat(old_b);                                     \
            }                                                                                     \
            out1[i] = md_w24(old_a); out2[i] = md_w24(old_b);                                     \
            first += md_ar1s(g, MD_EY(md_s(SOURCE_A) - old_a)) + (UNIT);                          \
            second += md_ar1s(g, MD_EY(md_s(SOURCE_B) - old_b)) + (UNIT);                         \
        } while (0)
        /* one loop, the index's wrap in it: its code is fetched once a block (the cache
         * misses of a second, straight-pointer copy cost more than its instructions save) */
            for (unsigned i = 0; i < 8; i += 2) {
                MD_PM_READ_STEP(i, pa[(ia + i) & 0xffu], pb[(ib + i) & 0xffu], 0);
                MD_PM_READ_STEP(i + 1, pa[(ia + i + 1) & 0xffu], pb[(ib + i + 1) & 0xffu], 1);
            }
#undef MD_PM_READ_STEP
    } else
#endif
    if (pa && pb) {                       /* both pages in the delay words: an index each */
        unsigned ia = a & 0xffu, ib = b & 0xffu;
        for (unsigned i = 0; i < 8; ++i) {
            const uint32_t old_a = MD_PM_WORD(first), old_b = MD_PM_WORD(second);
            out1[i] = old_a; out2[i] = old_b;
            first = MD_PM_DIFF(first, gain, md_s(pa[ia]), md_s(old_a), i);
            second = MD_PM_DIFF(second, gain, md_s(pb[ib]), md_s(old_b), i);
            ia = (ia + 1) & 0xffu; ib = (ib + 1) & 0xffu;
        }
    } else {
        uint32_t source_a = MD_PM_READ(pa, a), source_b = MD_PM_READ(pb, b);
        a = metal_advance(a, 1, 0xff); b = metal_advance(b, 1, 0xff);
        for (unsigned i = 0; i < 8; ++i) {
            const uint32_t old_a = MD_PM_WORD(first), old_b = MD_PM_WORD(second);
            out1[i] = old_a; out2[i] = old_b;
            first = MD_PM_DIFF(first, gain, md_s(source_a), md_s(old_a), i);
            second = MD_PM_DIFF(second, gain, md_s(source_b), md_s(old_b), i);
            source_a = MD_PM_READ(pa, a); source_b = MD_PM_READ(pb, b);   /* mdkit reads a ninth, unused */
            a = metal_advance(a, 1, 0xff); b = metal_advance(b, 1, 0xff);
        }
    }
    *history_a = MD_PM_WORD(first); *history_b = MD_PM_WORD(second);
}
#undef MD_PM_READ
#undef MD_PM_FROM24
#undef MD_PM_WORD
#undef MD_PM_DIFF
#elif MD_PM_PART == 2
static inline void metal_delay_write(MD_PM_T *s, unsigned base, unsigned other, unsigned source, uint32_t *page_a, uint32_t *page_b, int negative)
{
    const int32_t gain = md_s(s->fraction[0]), drive = md_s(s->machine == 70 ? 0x59999au : 0x666666u);
    uint32_t a = *page_a, b = *page_b;
    uint32_t *pa = md_pm_page(s, a), *pb = md_pm_page(s, b);
#ifdef MD_AP_FLOOR
    /* md_floor22_2's read (MD_AP_FLOOR) halved is md_ar2 >> 6. Its operands shifted once: the
     * gains as MD_EX, the words' sums as (w << 8) >> 2 each (md_s of the word, MD_EY'd), so
     * x0 + x1 and x0 - x1 are a sum and a difference of them; negative negates x0's by a mask
     * (one loop: its code is fetched once a block, not twice) */
    if (pa && pb) {
        const int32_t g = MD_EX(gain), d = MD_EX(drive), n = negative ? -1 : 0;
        const uint32_t *w0 = s->work + base, *w1 = s->work + other, *w2 = s->work + source;
        const unsigned ia = a & 0xffu, ib = b & 0xffu;
        /* the two words limited together when both are in range (the usual case); with neither
         * page wrapping in the eight samples, straight pointers (else the index's wrap) */
#define MD_PM_WRITE_LOOP(STORE_B, STORE_A)                                                          \
        for (unsigned i = 0; i < 8; ++i) {                                                          \
            const int32_t m0 = (((int32_t)(w0[i] << 8) >> 2) ^ n) - n, m1 = (int32_t)(w1[i] << 8) >> 2; \
            const int32_t x2 = (int32_t)(w2[i] << 8) >> 2;                                          \
            const int32_t tb = md_ar2s(g, m0 - m1, d, x2) >> 6, ta = md_ar2s(g, m0 + m1, d, x2) >> 6; \
            uint32_t vb = (uint32_t)tb, va = (uint32_t)ta;                                          \
            if (__builtin_expect(((vb + 0x800000u) | (va + 0x800000u)) >= 0x1000000u, 0)) {         \
                vb = md_floor_word(tb); va = md_floor_word(ta);                                     \
            }                                                                                       \
            STORE_B = vb & 0xffffffu; STORE_A = va & 0xffffffu;                                     \
        }
        if (ia <= 0xf8u && ib <= 0xf8u) {
            uint32_t *qa = pa + ia, *qb = pb + ib;
            MD_PM_WRITE_LOOP(qb[i], qa[i])
        } else
            MD_PM_WRITE_LOOP(pb[(ib + i) & 0xffu], pa[(ia + i) & 0xffu])
#undef MD_PM_WRITE_LOOP
        a = (a & 0xffff00u) | ((ia + 8u) & 0xffu); b = (b & 0xffff00u) | ((ib + 8u) & 0xffu);
    } else
#endif
    if (pa && pb) {                       /* both pages in the delay words: an index each */
        unsigned ia = a & 0xffu, ib = b & 0xffu;
        for (unsigned i = 0; i < 8; ++i) {      /* mdkit also forms a ninth pair, unused */
            const int32_t x0 = negative ? -md_s(s->work[base + i]) : md_s(s->work[base + i]);
            const int32_t x1 = md_s(s->work[other + i]), x2 = md_s(s->work[source + i]);
            pb[ib] = md_floor_word(md_floor22_2(gain, x0 - x1, drive, x2) >> 1) & 0xffffffu;
            pa[ia] = md_floor_word(md_floor22_2(gain, x0 + x1, drive, x2) >> 1) & 0xffffffu;
            ia = (ia + 1) & 0xffu; ib = (ib + 1) & 0xffu;
        }
        a = (a & 0xffff00u) | ia; b = (b & 0xffff00u) | ib;
    } else
    for (unsigned i = 0; i < 8; ++i) {
        const int32_t x0 = negative ? -md_s(s->work[base + i]) : md_s(s->work[base + i]);
        const int32_t x1 = md_s(s->work[other + i]), x2 = md_s(s->work[source + i]);
        const uint32_t first = md_floor_word(md_floor22_2(gain, x0 + x1, drive, x2) >> 1);
        const uint32_t second = md_floor_word(md_floor22_2(gain, x0 - x1, drive, x2) >> 1);
        if (pb) pb[b & 0xffu] = second & 0xffffffu; else metal_write(s, 1, b, second);
        b = metal_advance(b, 1, 0xff);
        if (pa) pa[a & 0xffu] = first & 0xffffffu; else metal_write(s, 1, a, first);
        a = metal_advance(a, 1, 0xff);
    }
    *page_a = a; *page_b = b;
}
#elif MD_PM_PART == 3
/* 3 metal_coupling: mdkit's four passes. The mix of the two delays and the source:
 * delay_gain (x + y) + excitation_gain z; under MD_AP_ACC the word of the EMAC's read of
 * the two products, with the three products' half units. */
static inline void metal_coupling(MD_PM_T *s)
{
    unsigned source = 0, audio = 0x100;
    const uint32_t delay_gain = s->machine == 70 ? 0x733333u : 0x7eb852u, excitation_gain = s->machine == 72 ? 0x400000u : 0xccccdu;
    s->fraction[6] = 0;
    for (unsigned pass = 0; pass < 4; ++pass) {
        uint32_t prior;
        metal_delay_read(s, s->storage.voice.page_a, s->storage.voice.page_d, s->fraction[2], s->fraction[5],
                         &s->storage.voice.delay_history_a, &s->storage.voice.delay_history_d, 0x89, 0xc9);
        metal_delay_read(s, s->storage.voice.page_c, s->storage.voice.page_b, s->fraction[4], s->fraction[3],
                         &s->storage.voice.delay_history_c, &s->storage.voice.delay_history_b, 0xc0, 0x80);
        audio = metal_advance(audio, -1, 31);
        prior = s->fraction[audio];
        for (unsigned i = 0; i < 8; ++i) {
#ifdef MD_AP_ACC
            /* md_ar2's operands shifted once: the gains as MD_EX, each word as (w << 8) >> 2 */
            const int32_t sum = ((int32_t)(s->work[0xc9 + i] << 8) >> 2) + ((int32_t)(s->work[0xc0 + i] << 8) >> 2);
            const uint32_t mixed = md_w24(md_sat((2 * md_ar2s(MD_EX((int32_t)delay_gain), sum, MD_EX((int32_t)excitation_gain),
                                                              (int32_t)(s->work[source + i] << 8) >> 2) + 3) >> 7));
#else
            uint64_t m = mdkit_mac(mdkit_mpy(delay_gain, s->work[0xc9 + i]), delay_gain, s->work[0xc0 + i]);
            const uint32_t mixed = mdkit_acc_to_word(mdkit_mac(m, excitation_gain, s->work[source + i]));
#endif
            s->fraction[audio] = prior; audio = metal_advance(audio, 1, 31); prior = mixed;
        }
        s->fraction[audio] = prior; audio = metal_advance(audio, 1, 31);
        metal_delay_write(s, 0x80, 0xc0, source, &s->storage.voice.page_a, &s->storage.voice.page_d, 1);
        metal_delay_write(s, 0x89, 0xc9, source, &s->storage.voice.page_b, &s->storage.voice.page_c, 0);
        source += 8; s->fraction[6] = source;
    }
}
#elif MD_PM_PART == 4
/* 4 metal_amplitude: the attack's accumulator is a sum of words (an integer); each product
 * is one EMAC read, exact under every form (floor(x y / 2^23), as md_acc_from_read's word);
 * the decay's last amplitude is the product's mid word, not limited. */
static inline void metal_amplitude(MD_PM_T *s)
{
    uint32_t *w = s->work;
    int32_t running = md_s(s->storage.voice.amplitude), gain = running;
    int32_t product = md_mul_shift_exact(gain, md_s(w[0]), 23);
    uint32_t next = w[1];
    if (s->storage.voice.attack_gate) {
        const int32_t step = md_s(s->storage.voice.attack_gate);
        running += step;
        for (unsigned i = 0; i < 31; ++i) {
            w[i] = md_w24(md_sat(product));
            product = md_mul_shift_exact(gain, md_s(next), 23);
            next = w[i + 2];
            gain = md_sat(running);
            running += step;
        }
        w[31] = md_w24(md_sat(product));
        s->storage.voice.amplitude = md_w24(md_sat(running));
        if (running >= 0x7fffff) s->storage.voice.attack_gate = 0;
    } else {
        uint32_t decay = s->storage.voice.decay;
        int32_t d, r;
        if (md_s(s->storage.voice.ring_amplitude) < 84) decay = s->machine == 72 ? 0x7f8d50u : 0x666666u;
        d = md_s(decay);
        r = md_mul_shift_exact(gain, d, 23);
        for (unsigned i = 0; i < 31; ++i) {
            const int32_t old = md_sat(r);
            w[i] = md_w24(md_sat(product));
            product = md_mul_shift_exact(gain, md_s(next), 23);
            next = w[i + 2];
            r = md_mul_shift_exact(gain, d, 23);
            gain = old;
        }
        w[31] = md_w24(md_sat(product));
        s->storage.voice.amplitude = md_w24(r);
    }
}
#elif MD_PM_PART == 5
/* 5 metal_output: as mdkit's, its last loop's accumulators sums of two words doubled (integers),
 * in mdkit's order of reads and writes (the outputs and the inputs share Y 0x100..) */
static inline void metal_output(MD_PM_T *s, uint32_t gain)
{
    const unsigned duration = s->machine == 70 ? 16 : s->machine == 71 ? 6 : 8;
    uint32_t pulse = s->machine == 71 ? 0x333333u : 0x666666u;
    uint32_t coeff[5];
    const unsigned row = 4 * s->storage.voice.brightness;
    unsigned table, input = 0x100, output = 0x1ff, work = 4;
    int32_t pending, current, sample;
    if (s->machine == 72 ? s->storage.voice.gate != 0x7fffff : !s->storage.voice.gate) pulse = 0;
    for (unsigned i = 0; i < 32; ++i) s->work[4 + i] = i < duration ? pulse : 0;
    s->work[0] = s->storage.voice.output_previous; s->work[1] = s->storage.voice.output_history;
    s->work[2] = s->storage.voice.output_input_previous; s->work[3] = s->storage.voice.output_input;
    table = s->machine == 72 ? 0x145420 : 0x145224 + row;
    coeff[0] = metal_read(s, 0, table); coeff[1] = metal_read(s, 0, table + 1);
    coeff[2] = metal_product(gain, metal_read(s, 0, table + 2)); coeff[3] = metal_product(gain, metal_read(s, 0, table + 3));
    coeff[4] = coeff[2];
    s->storage.voice.output_input = s->work[35]; s->storage.voice.output_input_previous = s->work[34];
    metal_filter(s, 0, &s->storage.voice.output_history, &s->storage.voice.output_fraction, &s->storage.voice.output_previous, coeff);
    pending = md_s(s->fraction[output]);
    current = md_s(s->fraction[input]); input = metal_advance(input, 1, 31);
    sample = md_s(s->work[work++]);
    for (unsigned i = 0; i < 16; ++i) {
        current += sample; s->fraction[output] = md_w24(md_sat(pending)); output = metal_advance(output, 1, 0xff);
        current *= 2; sample = md_s(s->work[work++]); pending = md_s(s->fraction[input]); input = metal_advance(input, 1, 31);
        pending += sample; sample = md_s(s->work[work++]); pending *= 2;
        s->fraction[output] = md_w24(md_sat(current)); output = metal_advance(output, 1, 0xff);
        current = md_s(s->fraction[input]); input = metal_advance(input, 1, 31);
    }
    s->fraction[output] = md_w24(md_sat(pending)); s->storage.voice.gate = 0;
}
#endif
#undef MD_PM_PART
#undef MD_PM_T
