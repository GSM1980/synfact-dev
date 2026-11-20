/* SPDX-License-Identifier: GPL-2.0-or-later
 * md-native.h - 24-bit DSP arithmetic in plain 32-bit integers, for the
 * voices' hand-written blocks (audio/native/).
 *
 * Most of a Machinedrum voice's work is "multiply two 24-bit words and keep
 * the high word" (mpy then move a,x). That word is floor(x*y / 2^23), which
 * needs no 56-bit accumulator: with x = xh*2^16 + xl and y = yh*2^16 + yl
 * (xl, yl in 0..65535; xh, yh in -128..127),
 *
 *     x*y = m*2^16 + z,   m = xh*y + xl*yh  (|m| < 2^31),   z = xl*yl < 2^32,
 *     floor(x*y / 2^k) = (m + (z >> 16)) >> (k - 16)    for k >= 16,
 *
 * because adding z's fraction below 2^16 cannot carry past a multiple of
 * 2^(k-16). Three multiplies, one of them 16 x 16. Words are int32_t, sign-
 * extended; md_w24 masks one back to the 24-bit pattern the voice stores.
 * The voice tests compare every word with mdkit's own code.
 *
 * MD_ST_FAST (an experiment): the products a voice marks as signal (md_mid_mpy,
 * md_word_mpy, md_mul_shift, md_mid_mpy2) use one 16 x 16 multiply of the
 * words' top 16 bits instead, and are no longer mdkit's word for word; the
 * _exact forms stay exact for the coefficients that feed back.
 */
#ifndef MD_NATIVE_H
#define MD_NATIVE_H
#include "md-types.h"
/* A test bank's copy of the mod folder may add md-fast.h (#define MD_ST_FAST 1),
 * as the stereo test does with stereo-test.h; the repository has none. */
#if defined(__has_include)
#if __has_include("md-fast.h")
#include "md-fast.h"
#endif
#endif

static inline int32_t md_s(uint32_t w) { return (int32_t)(w << 8) >> 8; }
static inline uint32_t md_w24(int32_t v) { return (uint32_t)v & 0xffffffu; }
/* a value as the 24-bit word the DSP stores, sign-extended again */
static inline int32_t md_wrap(int32_t v) { return (int32_t)((uint32_t)v << 8) >> 8; }
static inline int32_t md_sat(int32_t v) { return v > 0x7fffff ? 0x7fffff : v < -0x800000 ? -0x800000 : v; }

/* the parts of x*y: x*y = m*2^16 + z */
struct md_prod {int32_t m; uint32_t z;};
static inline struct md_prod md_prod(int32_t x, int32_t y)
{
    struct md_prod p;
    uint32_t xl = (uint32_t)x & 0xffffu, yl = (uint32_t)y & 0xffffu;
    p.m = (x >> 16) * y + (int32_t)xl * (y >> 16);
    p.z = (uint32_t)(uint16_t)xl * (uint16_t)yl;
    return p;
}
/* floor(x*y / 2^k), 16 <= k < 48 */
static inline int32_t md_mul_shift_c(int32_t x, int32_t y, unsigned k)
{
    struct md_prod p = md_prod(x, y);
    return (p.m + (int32_t)(p.z >> 16)) >> (k - 16);
}
/* floor((x*y + u*v) / 2^23), the high word of mpy x,y,a + mac u,v,a,
 * without overflow at any inputs */
static inline int32_t md_mid_mpy2_c(int32_t x, int32_t y, int32_t u, int32_t v)
{
    struct md_prod p = md_prod(x, y), q = md_prod(u, v);
    uint32_t c = (p.z >> 16) + (q.z >> 16) + (((p.z & 0xffffu) + (q.z & 0xffffu)) >> 16);
    return (p.m >> 7) + (q.m >> 7) + (int32_t)(((uint32_t)(p.m & 127) + (uint32_t)(q.m & 127) + c) >> 7);
}
/* round(x*y / 2^23), convergent (half to even), as the 56300's mpyr leaves
 * A1 with A2: from floor(x*y / 2^23) and the 23 bits below it */
static inline int32_t md_mpyr_c(int32_t x, int32_t y)
{
    struct md_prod p = md_prod(x, y);
    int32_t t = (p.m + (int32_t)(p.z >> 16)) >> 7;
    uint32_t rem = ((((uint32_t)p.m & 127u) << 16) + p.z) & 0x7fffffu;
    if (rem >= 0x400000u) { t += 1; if (rem == 0x400000u) t &= ~1; }
    return t;
}

/* q = floor(x*y / 2^k), 1 <= k <= 16, as a 48-bit pair: *hi = floor(q / 2^24),
 * returns q mod 2^24; from x*y = m*2^16 + z (see above) */
static inline uint32_t md_mul_floor48_c(int32_t x, int32_t y, unsigned k, int32_t *hi)
{
    struct md_prod p = md_prod(x, y);
    *hi = (p.m + (int32_t)(p.z >> 16)) >> (8 + k);
    return (((uint32_t)p.m << (16 - k)) + (p.z >> k)) & 0xffffffu;
}

/* MD_ST_APPROX: a product, or a sum of two or three, kept as the EMAC's read gives it,
 * A = floor(sum of floor(P_i / 2^9) / 2^8) (below), without the 32-bit products that
 * make it exact: short by a unit at most, so not mdkit's word for word
 * (tests/md_st_voice_ser.c measures each voice). Only where the result is a word, never
 * an accumulator that keeps its low bits (A0): dropping those broke the filters and
 * phases of most voices (WORK.md). The md_ar*_c forms are the same values in C. */
static inline int64_t md_ar_term_c(int32_t x, int32_t y) { return ((int64_t)x * y) >> 9; }
static inline int32_t md_ar1_c(int32_t x, int32_t y) { return md_mul_shift_c(x, y, 17); }
static inline int32_t md_ar2_c(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{ return (int32_t)((md_ar_term_c(x1, y1) + md_ar_term_c(x2, y2)) >> 8); }
static inline int32_t md_ar3_c(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3)
{ return (int32_t)((md_ar_term_c(x1, y1) + md_ar_term_c(x2, y2) + md_ar_term_c(x3, y3)) >> 8); }
static inline int32_t md_ar4_c(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, int32_t x4, int32_t y4)
{ return (int32_t)((md_ar_term_c(x1, y1) + md_ar_term_c(x2, y2) + md_ar_term_c(x3, y3) + md_ar_term_c(x4, y4)) >> 8); }

/* The EMAC (Syntakt only; MD_ST_NO_EMAC turns it off). With MACSR = 0x20
 * (fractional, signed, truncating, not saturating; md_emac_begin sets it)
 * mac.l A,B,ACC0 adds the 40-bit floor(2AB / 2^24) to ACC0, and move.l or
 * movclr.l (which also clears it) read floor(ACC0 / 2^8) (MCF5441x RM 5.3.5).
 * The voices put A = x << 8 and B = y << 6, so for one product P = x*y, or a
 * sum of a few,
 *
 *     read A = floor(P / 2^17) less at most a few units (each MAC drops its
 *              own bits below 2^9: A is exactly floor of the sum of the
 *              floor(P_i / 2^9), over 2^8),
 *     low    = P mod 2^32, the 32-bit products' own sum (muls.l),
 *     L      = low - A * 2^17, taken mod 2^32: P - A * 2^17, which is
 *              0 <= L < 2^17 + 2^9 * (number of products),
 *
 * so P = A * 2^17 + L exactly, with A carrying the high bits and low the low
 * ones: floor(P / 2^23) = (A + (L >> 17)) >> 6, and P mod 2^23 = low mod 2^23.
 * x is a word (24 bits); y may be 25 (|y| <= 2^24): |each f| < 2^38, so no
 * read overflows. Checked word for word against mdkit in analog-emu
 * (tests/test_md_st.py), whose EMAC follows the reference manual (Unicorn's
 * does not), and on the unit by md.c's self-test. */
#if (defined(__mcoldfire__) || defined(__mcfv4__)) && !defined(MD_ST_NO_EMAC)
#define MD_EMAC 1
#define MD_EX(x) ((int32_t)((uint32_t)(x) << 8))
#define MD_EY(y) ((int32_t)((uint32_t)(y) << 6))
/* floor(x*y / 2^17), one product (exact: L < 2^17) */
static inline int32_t md_emac_read(int32_t x, int32_t y)
{
    int32_t r;
    __asm__ volatile ("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0" : "=r"(r) : "r"(MD_EX(x)), "r"(MD_EY(y)));
    return r;
}
static inline uint32_t md_low32(int32_t x, int32_t y) { return (uint32_t)x * (uint32_t)y; }
/* the EMAC's read of one, two or three products (MD_ST_APPROX; md_ar*_c above) */
static inline int32_t md_ar1(int32_t x, int32_t y) { return md_emac_read(x, y); }
static inline int32_t md_ar2(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    int32_t a;
    __asm__ volatile ("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmovclr.l %%acc0,%0"
                      : "=&r"(a) : "r"(MD_EX(x1)), "r"(MD_EY(y1)), "r"(MD_EX(x2)), "r"(MD_EY(y2)));
    return a;
}
static inline int32_t md_ar3(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3)
{
    int32_t a;
    __asm__ volatile ("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmac.l %5,%6,%%acc0\n\tmovclr.l %%acc0,%0"
                      : "=&r"(a)
                      : "r"(MD_EX(x1)), "r"(MD_EY(y1)), "r"(MD_EX(x2)), "r"(MD_EY(y2)), "r"(MD_EX(x3)), "r"(MD_EY(y3)));
    return a;
}
/* md_ar3 of operands already shifted (MD_EX, MD_EY), for loops that reuse them */
static inline int32_t md_ar3s(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3)
{
    int32_t a;
    __asm__ volatile ("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmac.l %5,%6,%%acc0\n\tmovclr.l %%acc0,%0"
                      : "=&r"(a) : "r"(x1), "r"(y1), "r"(x2), "r"(y2), "r"(x3), "r"(y3));
    return a;
}
/* four products (PI-RC, CC and HH's metal filter under MD_AP_STRUCT) */
static inline int32_t md_ar4(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, int32_t x4, int32_t y4)
{
    int32_t a;
    __asm__ volatile ("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmac.l %5,%6,%%acc0\n\tmac.l %7,%8,%%acc0\n\tmovclr.l %%acc0,%0"
                      : "=&r"(a)
                      : "r"(MD_EX(x1)), "r"(MD_EY(y1)), "r"(MD_EX(x2)), "r"(MD_EY(y2)), "r"(MD_EX(x3)), "r"(MD_EY(y3)),
                        "r"(MD_EX(x4)), "r"(MD_EY(y4)));
    return a;
}
/* md_ar1, md_ar2 and md_ar4 of operands already shifted, each pair's first by 8 (MD_EX) and
 * its second by 6 (MD_EY). The product is the same whichever operand takes which shift, so a
 * 24-bit word w can go first as w << 8, with no sign extension (the shift drops the top byte),
 * and its coefficient second as MD_EY; or a word second as (w << 8) >> 2 (PI-RC, CC and HH's
 * metal, native/pi_metal.c) */
static inline int32_t md_ar1s(int32_t x, int32_t y)
{
    int32_t r;
    __asm__ volatile ("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0" : "=r"(r) : "r"(x), "r"(y));
    return r;
}
static inline int32_t md_ar2s(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    int32_t a;
    __asm__ volatile ("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmovclr.l %%acc0,%0"
                      : "=&r"(a) : "r"(x1), "r"(y1), "r"(x2), "r"(y2));
    return a;
}
static inline int32_t md_ar4s(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, int32_t x4, int32_t y4)
{
    int32_t a;
    __asm__ volatile ("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmac.l %5,%6,%%acc0\n\tmac.l %7,%8,%%acc0\n\tmovclr.l %%acc0,%0"
                      : "=&r"(a) : "r"(x1), "r"(y1), "r"(x2), "r"(y2), "r"(x3), "r"(y3), "r"(x4), "r"(y4));
    return a;
}
/* the read after the first product and after both */
static inline int32_t md_ar2_first(int32_t x, int32_t y, int32_t u, int32_t v, int32_t *first)
{
    int32_t a1, a;
    __asm__ volatile ("mac.l %2,%3,%%acc0\n\tmove.l %%acc0,%0\n\tmac.l %4,%5,%%acc0\n\tmovclr.l %%acc0,%1"
                      : "=&r"(a1), "=&r"(a)
                      : "r"(MD_EX(x)), "r"(MD_EY(y)), "r"(MD_EX(u)), "r"(MD_EY(v)));
    *first = a1;
    return a;
}
/* floor(x*y / 2^k), 17 <= k < 48 */
static inline int32_t md_mul_shift_exact(int32_t x, int32_t y, unsigned k) { return md_emac_read(x, y) >> (k - 17); }
/* floor(x*(a+b) / 2^23) = mpy x,a + mac b,x: one product, |a+b| <= 2^24 */
static inline int32_t md_mid_mpy_sum(int32_t x, int32_t a, int32_t b) { return md_emac_read(x, a + b) >> 6; }
/* floor((x*y + u*v) / 2^23), and floor(x*y / 2^23) in *first: two MACs into
 * ACC0, the first read between them */
static inline int32_t md_mid_mpy2_first(int32_t x, int32_t y, int32_t u, int32_t v, int32_t *first)
{
    int32_t a1, a;
    uint32_t low = md_low32(x, y) + md_low32(u, v), l;
    __asm__ volatile ("mac.l %2,%3,%%acc0\n\tmove.l %%acc0,%0\n\tmac.l %4,%5,%%acc0\n\tmovclr.l %%acc0,%1"
                      : "=&r"(a1), "=&r"(a)
                      : "r"(MD_EX(x)), "r"(MD_EY(y)), "r"(MD_EX(u)), "r"(MD_EY(v)));
    l = low - ((uint32_t)a << 17);
    *first = a1 >> 6;
    return (a + (int32_t)(l >> 17)) >> 6;
}
static inline int32_t md_mid_mpy2_exact(int32_t x, int32_t y, int32_t u, int32_t v)
{ int32_t f; return md_mid_mpy2_first(x, y, u, v, &f); }
/* q = floor(x*y / 2^k), 1 <= k <= 8, as a 48-bit pair: *hi = floor(q / 2^24),
 * returns q mod 2^24 = (P >> k) mod 2^24, which is in low's 32 bits */
static inline uint32_t md_mul_floor48(int32_t x, int32_t y, unsigned k, int32_t *hi)
{
    *hi = md_emac_read(x, y) >> (7 + k);
    return (md_low32(x, y) >> k) & 0xffffffu;
}
/* round(x*y / 2^23), convergent: floor and the 23 bits below, from low */
static inline int32_t md_mpyr(int32_t x, int32_t y)
{
    int32_t t = md_emac_read(x, y) >> 6;
    uint32_t rem = md_low32(x, y) & 0x7fffffu;
    if (rem >= 0x400000u) { t += 1; if (rem == 0x400000u) t &= ~1; }
    return t;
}
/* MACSR, ACC0 and ACCext01 as the stock code left them, around the voices.
 * They are read and written in integer mode (MACSR = 0), where a move reads
 * and writes the physical registers as they are (no rounding or limiting); a
 * MACSR write keeps the physical registers. */
struct md_emac_saved {uint32_t macsr, acc0, ext01;};
static inline void md_emac_begin(struct md_emac_saved *v)
{
    uint32_t m, a, e, junk, zero = 0, mode = 0x20;
    __asm__ volatile ("move.l %%macsr,%0\n\tmove.l %4,%%macsr\n\tmove.l %%acc0,%1\n\tmove.l %%accext01,%2\n\t"
                      "move.l %5,%%macsr\n\tmovclr.l %%acc0,%3"
                      : "=&d"(m), "=&d"(a), "=&d"(e), "=&d"(junk) : "d"(zero), "d"(mode));
    v->macsr = m; v->acc0 = a; v->ext01 = e;
}
static inline void md_emac_end(const struct md_emac_saved *v)
{
    uint32_t zero = 0;
    __asm__ volatile ("move.l %0,%%macsr\n\tmove.l %1,%%acc0\n\tmove.l %2,%%accext01\n\tmove.l %3,%%macsr"
                      : : "d"(zero), "d"(v->acc0), "d"(v->ext01), "d"(v->macsr));
}
#else
static inline int32_t md_mul_shift_exact(int32_t x, int32_t y, unsigned k) { return md_mul_shift_c(x, y, k); }
static inline int32_t md_mid_mpy2_exact(int32_t x, int32_t y, int32_t u, int32_t v) { return md_mid_mpy2_c(x, y, u, v); }
static inline int32_t md_mid_mpy_sum(int32_t x, int32_t a, int32_t b) { return md_mid_mpy2_c(x, a, b, x); }
static inline int32_t md_mpyr(int32_t x, int32_t y) { return md_mpyr_c(x, y); }
static inline int32_t md_mid_mpy2_first(int32_t x, int32_t y, int32_t u, int32_t v, int32_t *first)
{ *first = md_mul_shift_c(x, y, 23); return md_mid_mpy2_c(x, y, u, v); }
static inline uint32_t md_mul_floor48(int32_t x, int32_t y, unsigned k, int32_t *hi) { return md_mul_floor48_c(x, y, k, hi); }
static inline int32_t md_ar1(int32_t x, int32_t y) { return md_ar1_c(x, y); }
static inline int32_t md_ar2(int32_t x1, int32_t y1, int32_t x2, int32_t y2) { return md_ar2_c(x1, y1, x2, y2); }
static inline int32_t md_ar3(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3)
{ return md_ar3_c(x1, y1, x2, y2, x3, y3); }
static inline int32_t md_ar4(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, int32_t x4, int32_t y4)
{ return md_ar4_c(x1, y1, x2, y2, x3, y3, x4, y4); }
#define MD_EX(x) ((int32_t)((uint32_t)(x) << 8))
#define MD_EY(y) ((int32_t)((uint32_t)(y) << 6))
static inline int32_t md_ar3s(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3)
{ return md_ar3_c(x1 >> 8, y1 >> 6, x2 >> 8, y2 >> 6, x3 >> 8, y3 >> 6); }
/* the s forms: each pair's first operand shifted by 8, its second by 6 */
static inline int32_t md_ar1s(int32_t x, int32_t y) { return md_ar1_c(x >> 8, y >> 6); }
static inline int32_t md_ar2s(int32_t x1, int32_t y1, int32_t x2, int32_t y2) { return md_ar2_c(x1 >> 8, y1 >> 6, x2 >> 8, y2 >> 6); }
static inline int32_t md_ar4s(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, int32_t x4, int32_t y4)
{ return md_ar4_c(x1 >> 8, y1 >> 6, x2 >> 8, y2 >> 6, x3 >> 8, y3 >> 6, x4 >> 8, y4 >> 6); }
static inline int32_t md_ar2_first(int32_t x, int32_t y, int32_t u, int32_t v, int32_t *first)
{ *first = md_ar1_c(x, y); return md_ar2_c(x, y, u, v); }
#endif

/* The approximate forms (MD_ST_APPROX): a voice that may use them says so with MD_AP_MID,
 * MD_AP_FLOOR (md-floor.h) and MD_AP_SINE (md-tables.h) before md-acc.h (tools/md_port.py,
 * APPROX), and MD_AP_FM, MD_AP_STRUCT; MD_ST_EXACT turns them all off. md.c's self-test checks the reads they use. */
/* the rounded product from the read: half up at 2^22, not convergent */
static inline int32_t md_mpyr_c_approx(int32_t x, int32_t y) { return (md_ar1_c(x, y) + 32) >> 6; }
static inline int32_t md_mpyr_approx(int32_t x, int32_t y) { return (md_ar1(x, y) + 32) >> 6; }
static inline int32_t md_mid_mpy2_first_approx(int32_t x, int32_t y, int32_t u, int32_t v, int32_t *first)
{ int32_t a1, a = md_ar2_first(x, y, u, v, &a1); *first = a1 >> 6; return a >> 6; }
static inline int32_t md_mid_mpy2_approx(int32_t x, int32_t y, int32_t u, int32_t v) { return md_ar2(x, y, u, v) >> 6; }
/* the exact sum of two products' high word, under a name the approximate forms leave alone */
static inline int32_t md_mid_mpy2_x(int32_t x, int32_t y, int32_t u, int32_t v) { return md_mid_mpy2_exact(x, y, u, v); }
#ifdef MD_ST_EXACT
#undef MD_AP_MID
#undef MD_AP_FLOOR
#undef MD_AP_SINE
#undef MD_AP_ACC
#undef MD_AP_FM
#undef MD_AP_STRUCT     /* the PI voices' structural forms (tools/md_port.py PI_STRUCT) */
#endif
#ifdef MD_AP_MID
#define md_mpyr md_mpyr_approx
#define md_mid_mpy2_first md_mid_mpy2_first_approx
#define md_mid_mpy2_exact md_mid_mpy2_approx
#endif
/* mid(mpy(x, y)): the high word of 2*x*y, unwrapped (-2^23..2^23) */
static inline int32_t md_mid_mpy_exact(int32_t x, int32_t y) { return md_mul_shift_exact(x, y, 23); }
/* floor((x y + u v) / 2^23) and in *rem the 23 bits below it, exactly: the
 * high word and A0 / 2 of mpy x,y + mac u,v (the low bits are the 32-bit
 * products' own sum) */
static inline int32_t md_sum2_rem(int32_t x, int32_t y, int32_t u, int32_t v, uint32_t *rem)
{
    *rem = ((uint32_t)x * (uint32_t)y + (uint32_t)u * (uint32_t)v) & 0x7fffffu;
    return md_mid_mpy2_x(x, y, u, v);
}
/* acc_to_word(round(mpy x,y - from24(old) + mac u,v)): the 56300's convergent RND of
 * that sum (+ 2^23 on A0, bit 24 cleared when A0 comes out 0) */
static inline int32_t md_round_sum2_less(int32_t x, int32_t y, int32_t u, int32_t v, int32_t old)
{
    uint32_t rem;
    int32_t t = md_sum2_rem(x, y, u, v, &rem) - old;
    if (rem >= 0x400000u) { t += 1; if (rem == 0x400000u) t &= ~1; }
    return md_sat(t);
}

/* MD_AP_FM (MD_ST_APPROX, the EFM voices' FM loops): their feedback makes any LSB grow,
 * so they are not mdkit's word for word with any approximate form; these keep the sound.
 * - a 48-bit step floor(x y / 2^k) (k <= 7) from the EMAC's read alone: its 17 - k
 *   bits below are zero (a phase's error under 2^-31 of a turn a sample);
 * - a rounded MAC from the read: half up at 2^22, not convergent;
 * - the sines from their quarter as it is (MD_AP_SINE, md-tables.h): 1 LSB. */
static inline uint32_t md_mul_floor48_fm(int32_t x, int32_t y, unsigned k, int32_t *hi)
{
    const int32_t r = md_ar1(x, y);
    *hi = r >> (7 + k);
    return ((uint32_t)r << (17 - k)) & 0xffffffu;
}
static inline int32_t md_round_sum2_less_fm(int32_t x, int32_t y, int32_t u, int32_t v, int32_t old)
{ return md_sat(((md_ar2(x, y, u, v) + 32) >> 6) - old); }
#ifdef MD_AP_FM
#define md_mul_floor48 md_mul_floor48_fm
#define md_round_sum2_less md_round_sum2_less_fm
#endif

#if defined(MD_ST_FAST) && !defined(MD_EMAC)
static inline int32_t md_mul_shift(int32_t x, int32_t y, unsigned k)
{ return ((int32_t)(short)(x >> 8) * (short)(y >> 8)) >> (k - 16); }
static inline int32_t md_mid_mpy2(int32_t x, int32_t y, int32_t u, int32_t v)
{ return (((int32_t)(short)(x >> 8) * (short)(y >> 8) >> 1) + ((int32_t)(short)(u >> 8) * (short)(v >> 8) >> 1)) >> 6; }
#else
static inline int32_t md_mul_shift(int32_t x, int32_t y, unsigned k) { return md_mul_shift_exact(x, y, k); }
static inline int32_t md_mid_mpy2(int32_t x, int32_t y, int32_t u, int32_t v) { return md_mid_mpy2_exact(x, y, u, v); }
#endif
static inline int32_t md_mid_mpy(int32_t x, int32_t y) { return md_mul_shift(x, y, 23); }
/* acc_to_word(mpy(x, y)): the high word, limited (only -1 * -1 reaches 2^23) */
static inline int32_t md_word_mpy(int32_t x, int32_t y)
{
    int32_t t = md_mul_shift(x, y, 23);
    return t > 0x7fffff ? 0x7fffff : t;
}
static inline int32_t md_word_mpy_exact(int32_t x, int32_t y)
{
    int32_t t = md_mul_shift_exact(x, y, 23);
    return t > 0x7fffff ? 0x7fffff : t;
}
#endif
