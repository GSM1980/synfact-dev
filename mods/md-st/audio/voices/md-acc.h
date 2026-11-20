/* SPDX-License-Identifier: GPL-2.0-or-later
 * md-acc.h - the DSP56300 accumulator of the mdkit voices, for a 32-bit CPU.
 *
 * mdkit keeps a 56-bit accumulator value v as a uint64_t masked to 56 bits,
 * and every move, add and product goes through a few helpers (mid, from24,
 * add, mpy, acc_to_word...). On the MCF54415 those are 64-bit shifts, masks
 * and library multiplies. Here the same uint64_t holds v as two words:
 *
 *     high 32 bits: t = v >> 24 (signed; bits 24..55 of v, i.e. A2:A1)
 *     low 32 bits:  l = v & 0xffffff (A0)
 *
 * so A1 is t's low 24 bits, a 56-bit wrap is t's own 32-bit wrap, and a
 * product needs four 12-bit multiplies. The voices look inside an
 * accumulator only through these helpers, and the one idiom they use
 * directly, (a & ~0xffffff) | w to set A0, means the same in both encodings.
 * Zero is zero and equal values have equal encodings. tests/test_md_st.py
 * compares each helper with mdkit's and every voice word with mdkit's.
 */
#ifndef MD_ACC_H
#define MD_ACC_H
#include "md-types.h"
#include "md-native.h"      /* MD_EMAC and its primitives */

typedef uint64_t md_acc;
#define MD_T(a) ((int32_t)(uint32_t)((a) >> 32))
#define MD_L(a) ((uint32_t)(a))
static inline md_acc md_acc_make(int32_t t, uint32_t l) { return ((uint64_t)(uint32_t)t << 32) | l; }

static inline int32_t md_acc_s24(uint32_t w) { return (int32_t)(w << 8) >> 8; }
static inline int64_t md_acc_s56(md_acc a) { return (int64_t)MD_T(a) * 16777216 + (int64_t)MD_L(a); }
static inline md_acc md_acc_from_value(int64_t v)        /* v taken mod 2^56 */
{ return md_acc_make((int32_t)(uint32_t)((uint64_t)v >> 24), (uint32_t)v & 0xffffffu); }
static inline uint32_t md_acc_mid(md_acc a) { return (uint32_t)MD_T(a) & 0xffffffu; }
static inline uint32_t md_acc_low(md_acc a) { return MD_L(a); }
static inline md_acc md_acc_from24(uint32_t w) { return md_acc_make(md_acc_s24(w), 0); }
/* A 48-bit word (what mdkit's pack48 and to48 return, and from48 reads) is
 * kept in the same two words: t = bits 24..47, l = bits 0..23, unsigned. Only
 * a literal written in a voice is a plain 48-bit number: md_port.py turns
 * from48(literal) into md_acc_from48_raw(literal). */
static inline md_acc md_acc_from48(uint64_t w)
{ return md_acc_make(md_acc_s24((uint32_t)MD_T(w)), MD_L(w) & 0xffffffu); }
static inline md_acc md_acc_from48_raw(uint64_t w)
{ return md_acc_make(md_acc_s24((uint32_t)(w >> 24)), (uint32_t)w & 0xffffffu); }
/* the value limited to 48 bits, as a 48-bit word */
static inline uint64_t md_acc_to48(md_acc a)
{
    int32_t t = MD_T(a);
    if (t < -0x800000) return md_acc_make(0x800000, 0);
    if (t > 0x7fffff) return md_acc_make(0x7fffff, 0xffffffu);
    return md_acc_make((int32_t)((uint32_t)t & 0xffffffu), MD_L(a));
}
static inline md_acc md_acc_setmid(md_acc a, uint32_t w)
{ return md_acc_make((int32_t)(((uint32_t)MD_T(a) & 0xff000000u) | (w & 0xffffffu)), MD_L(a)); }
static inline md_acc md_acc_and_mid(md_acc a, uint32_t mask)
{ return md_acc_make((int32_t)((uint32_t)MD_T(a) & (0xff000000u | (mask & 0xffffffu))), MD_L(a)); }
static inline md_acc md_acc_pack48(uint32_t hi, uint32_t lo)
{ return md_acc_make((int32_t)(hi & 0xffffffu), lo & 0xffffffu); }
static inline md_acc md_acc_add(md_acc a, md_acc b)
{
    uint32_t l = MD_L(a) + MD_L(b);
    return md_acc_make((int32_t)((uint32_t)MD_T(a) + (uint32_t)MD_T(b) + (l >> 24)), l & 0xffffffu);
}
static inline md_acc md_acc_sub(md_acc a, md_acc b)
{
    uint32_t l = MD_L(a) - MD_L(b);                 /* borrow: bit 31 set */
    return md_acc_make((int32_t)((uint32_t)MD_T(a) - (uint32_t)MD_T(b) - (l >> 31)), l & 0xffffffu);
}
static inline md_acc md_acc_asl(md_acc a, unsigned n)
{
    if (!n) return a;
    if (n < 24) return md_acc_make((int32_t)(((uint32_t)MD_T(a) << n) | (MD_L(a) >> (24 - n))),
                                   (MD_L(a) << n) & 0xffffffu);
    return md_acc_from_value((int64_t)((uint64_t)md_acc_s56(a) << n));
}
/* arithmetic shift right, rounding toward minus infinity (mdkit's asr) */
static inline md_acc md_acc_asr(md_acc a, unsigned n)
{
    if (!n) return a;
    if (n < 24) return md_acc_make(MD_T(a) >> n,
                                   ((MD_L(a) >> n) | ((uint32_t)MD_T(a) << (24 - n))) & 0xffffffu);
    if (n < 56) return md_acc_from_value(md_acc_s56(a) >> n);
    return md_acc_s56(a) < 0 ? md_acc_make(-1, 0xffffffu) : 0;
}
/* x * y, |x|, |y| <= 2^23, from 12-bit halves: x = xh * 4096 + xl with xl in
 * 0..4095, so x*y = hi * 2^24 + mid * 2^12 + xl*yl and every partial product
 * fits 32 bits. (A three-multiply split on 16-bit halves measured no fewer
 * instructions.) */
static inline md_acc md_acc_product_c(int32_t x, int32_t y, int doubled)
{
    int32_t xh = x >> 12, yh = y >> 12, xl = x & 4095, yl = y & 4095;
    int32_t hi = xh * yh, mid = xh * yl + xl * yh;
    uint32_t lo = (uint32_t)(xl * yl) + ((uint32_t)(mid & 4095) << 12);   /* < 2^25 */
    int32_t t = hi + (mid >> 12) + (int32_t)(lo >> 24);
    lo &= 0xffffffu;
    if (doubled) { t = (int32_t)(((uint32_t)t << 1) | (lo >> 23)); lo = (lo << 1) & 0xffffffu; }
    return md_acc_make(t, lo);
}
/* MD_AP_ACC (MD_ST_APPROX): a product, or a sum of two or three, from the EMAC's read
 * alone, A = floor(sum / 2^17) less a unit at most (md-native.h): t = A >> 6 and A0's top
 * 6 bits are A's low ones, plus half a unit (2^17 of A0) so that the error has no mean.
 * Not doubled, A0 keeps 7 bits and the half unit is 2^16. The error is at most a unit of
 * A, 2^-6 of a word's LSB, a product. An accumulator keeps its A0: dropping A0 broke the
 * filters (WORK.md), this does not. What must stay exact takes the _x forms: a voice's
 * coefficients, a product whose A0 is read, a high-order filter (tools/md_port.py,
 * ACC_EXACT, and the natives). */
static inline md_acc md_acc_from_read(int32_t a, int doubled)
{
    return doubled ? md_acc_make(a >> 6, (((uint32_t)a & 63u) << 18) | 0x20000u)
                   : md_acc_make(a >> 7, (((uint32_t)a & 127u) << 17) | 0x10000u);
}
#ifdef MD_EMAC
/* The same with the EMAC (md-native.h): t from the read, A0 from the 32-bit
 * product (P mod 2^23 is low mod 2^23). Doubled, t = A >> 6 and A0 is twice
 * the 23 bits below; not doubled, t = A >> 7 and A0 the 24 bits below. */
static inline md_acc md_acc_product_x(int32_t x, int32_t y, int doubled)
{
    int32_t a = md_emac_read(x, y);
    uint32_t low = md_low32(x, y);
    return doubled ? md_acc_make(a >> 6, (low << 1) & 0xffffffu) : md_acc_make(a >> 7, low & 0xffffffu);
}
#else
static inline md_acc md_acc_product_x(int32_t x, int32_t y, int doubled) { return md_acc_product_c(x, y, doubled); }
#endif
#ifdef MD_AP_ACC
static inline md_acc md_acc_product(int32_t x, int32_t y, int doubled) { return md_acc_from_read(md_ar1(x, y), doubled); }
#else
static inline md_acc md_acc_product(int32_t x, int32_t y, int doubled) { return md_acc_product_x(x, y, doubled); }
#endif
static inline md_acc md_acc_mpy(uint32_t a, uint32_t b) { return md_acc_product(md_acc_s24(a), md_acc_s24(b), 1); }
static inline md_acc md_acc_mpy_x(uint32_t a, uint32_t b) { return md_acc_product_x(md_acc_s24(a), md_acc_s24(b), 1); }
/* signed s by unsigned u: the doubled product of a word and a 24-bit unsigned
 * (|u| < 2^24, which md_acc_product takes on both paths) */
static inline md_acc md_acc_mpy_su(uint32_t s, uint32_t u)
{ return md_acc_product(md_acc_s24(s), (int32_t)(u & 0xffffffu), 1); }
static inline md_acc md_acc_mpy_su_x(uint32_t s, uint32_t u)
{ return md_acc_product_x(md_acc_s24(s), (int32_t)(u & 0xffffffu), 1); }
/* convergent rounding of A1 (the 56300's RND): + 2^23, clear A0, and clear
 * bit 24 when A0 came out zero */
static inline md_acc md_acc_round(md_acc a)
{
    uint32_t l = MD_L(a) + 0x800000u;
    uint32_t t = (uint32_t)MD_T(a) + (l >> 24);
    if (!(l & 0xffffffu)) t &= ~1u;
    return md_acc_make((int32_t)t, 0);
}
/* the rounded product has A0 = 0: only its high word is computed (md-native.h) */
static inline md_acc md_acc_mpyr(uint32_t a, uint32_t b) { return md_acc_make(md_mpyr(md_acc_s24(a), md_acc_s24(b)), 0); }
static inline md_acc md_acc_mac(md_acc acc, uint32_t x, uint32_t y) { return md_acc_add(acc, md_acc_mpy(x, y)); }
static inline md_acc md_acc_mac_x(md_acc acc, uint32_t x, uint32_t y) { return md_acc_add(acc, md_acc_mpy_x(x, y)); }
/* acc + mpy x1,y1 + mac x2,y2 + mac x3,y3 (signed words), exactly. With the
 * EMAC the three products go into ACC0 together and the 32-bit products give
 * the bits it drops (md-native.h); the sum of the three f stays under 2^39. */
static inline md_acc md_acc_mac3_x(md_acc acc, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3)
{
#ifdef MD_EMAC
    int32_t a;
    uint32_t low = md_low32(x1, y1) + md_low32(x2, y2) + md_low32(x3, y3), l;
    __asm__ volatile ("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmac.l %5,%6,%%acc0\n\tmovclr.l %%acc0,%0"
                      : "=&r"(a)
                      : "r"(MD_EX(x1)), "r"(MD_EY(y1)), "r"(MD_EX(x2)), "r"(MD_EY(y2)), "r"(MD_EX(x3)), "r"(MD_EY(y3)));
    l = low - ((uint32_t)a << 17);                   /* P - a * 2^17, 0 <= l < 2^18 */
    a += (int32_t)(l >> 17);
    return md_acc_add(acc, md_acc_make(a >> 6, (low << 1) & 0xffffffu));
#else
    return md_acc_add(md_acc_add(md_acc_add(acc, md_acc_product_c(x1, y1, 1)), md_acc_product_c(x2, y2, 1)),
                      md_acc_product_c(x3, y3, 1));
#endif
}
#ifdef MD_AP_ACC
static inline md_acc md_acc_mac3(md_acc acc, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3) { return md_acc_add(acc, md_acc_from_read(md_ar3(x1, y1, x2, y2, x3, y3), 1)); }
#else
static inline md_acc md_acc_mac3(md_acc acc, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3) { return md_acc_mac3_x(acc, x1, y1, x2, y2, x3, y3); }
#endif
/* acc + mpy x1,y1 + mac x2,y2 (signed; y up to 25 bits), exactly: md_acc_mac3 with two */
static inline md_acc md_acc_mac2_x(md_acc acc, int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
#ifdef MD_EMAC
    int32_t a;
    uint32_t low = md_low32(x1, y1) + md_low32(x2, y2), l;
    __asm__ volatile ("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmovclr.l %%acc0,%0"
                      : "=&r"(a) : "r"(MD_EX(x1)), "r"(MD_EY(y1)), "r"(MD_EX(x2)), "r"(MD_EY(y2)));
    l = low - ((uint32_t)a << 17);
    a += (int32_t)(l >> 17);
    return md_acc_add(acc, md_acc_make(a >> 6, (low << 1) & 0xffffffu));
#else
    return md_acc_add(md_acc_add(acc, md_acc_product_c(x1, y1, 1)), md_acc_product_c(x2, y2, 1));
#endif
}
#ifdef MD_AP_ACC
static inline md_acc md_acc_mac2(md_acc acc, int32_t x1, int32_t y1, int32_t x2, int32_t y2) { return md_acc_add(acc, md_acc_from_read(md_ar2(x1, y1, x2, y2), 1)); }
#else
static inline md_acc md_acc_mac2(md_acc acc, int32_t x1, int32_t y1, int32_t x2, int32_t y2) { return md_acc_mac2_x(acc, x1, y1, x2, y2); }
#endif
/* acc + d * 2^23 (|d| < 2^30): mac x,0x400000 adds x * 2^23 and mac x,0x800000
 * adds -x * 2^24, so such a run is one integer d (x0 - 2 x1 + x2...) */
static inline md_acc md_acc_add_halves(md_acc acc, int32_t d)
{ return md_acc_add(acc, md_acc_make(d >> 1, ((uint32_t)d & 1u) << 23)); }
static inline md_acc md_acc_macr(md_acc acc, uint32_t x, uint32_t y) { return md_acc_round(md_acc_mac(acc, x, y)); }
static inline md_acc md_acc_macr_x(md_acc acc, uint32_t x, uint32_t y) { return md_acc_round(md_acc_mac_x(acc, x, y)); }
static inline uint32_t md_acc_acc_to_word(md_acc a)
{
    int32_t t = MD_T(a);
    if (t < -0x800000) return 0x800000u;
    if (t > 0x7fffff) return 0x7fffffu;
    return (uint32_t)t & 0xffffffu;
}
/* acc_to_word's limit of a high word t */
static inline uint32_t md_acc_limit(int32_t t)
{
    if ((uint32_t)t + 0x800000u < 0x1000000u) return (uint32_t)t & 0xffffffu;
    return t < 0 ? 0x800000u : 0x7fffffu;
}
/* the same word, sign-extended (for code that keeps signed words) */
static inline int32_t md_acc_word_s(md_acc a)
{
    int32_t t = MD_T(a);
    return t < -0x800000 ? -0x800000 : t > 0x7fffff ? 0x7fffff : t;
}
/* acc + mac a,g - mpy b,g = acc + 2 g (a - b): one product of a word and a
 * 25-bit difference (md_acc_product takes |y| <= 2^24 on both paths) */
static inline md_acc md_acc_mac_diff(md_acc acc, int32_t g, int32_t a, int32_t b)
{ return md_acc_add(acc, md_acc_product(g, a - b, 1)); }
static inline md_acc md_acc_mac_diff_x(md_acc acc, int32_t g, int32_t a, int32_t b)
{ return md_acc_add(acc, md_acc_product_x(g, a - b, 1)); }
/* acc_to_word(mpy x,y + mac u,v): v may be 25 bits (a sum of two words) */
static inline uint32_t md_acc_word2(int32_t x, int32_t y, int32_t u, int32_t v)
{
#if defined(MD_EMAC) || defined(MD_AP_MID)
    int32_t f;      /* |x y| + |u v| < 2^46 + 2^47: the sum of the f stays under 2^39 */
    return md_w24(md_sat(md_mid_mpy2_first(x, y, u, v, &f)));
#else
    return md_acc_acc_to_word(md_acc_add(md_acc_product_c(x, y, 1), md_acc_product_c(u, v, 1)));
#endif
}
#endif
