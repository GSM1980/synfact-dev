/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/md-floor.h - the word of a DSP accumulator that is only read as
 * acc_to_word(asl(acc, 1)), for the native filters (trx_metal.c, trx_sd_noise.c).
 *
 * With acc = mpy x1,y1 + mac x2,y2 (+ mac x3,y3) + d * 2^23 (mac x,0x400000 adds
 * x * 2^23 and mac x,0x800000 adds -x * 2^24, so such runs are one integer d), that
 * word is md_floor_word(floor(S / 2^22) + d), S = x1 y1 + x2 y2 (+ x3 y3), |S| <
 * 3 * 2^46: no 56-bit wrap is reached. md_floor22 computes floor(S / 2^22) exactly:
 * with the EMAC from its read and the 32-bit products, as md_mid_mpy2_first does
 * for 2^23 (md-native.h); without it in 64 bits.
 */
#ifndef MD_FLOOR_H
#define MD_FLOOR_H
#ifdef MD_EMAC
static inline int32_t md_floor22_x(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3)
{
    int32_t a;
    uint32_t low = md_low32(x1, y1) + md_low32(x2, y2) + md_low32(x3, y3), l;
    __asm__ volatile ("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmac.l %5,%6,%%acc0\n\tmovclr.l %%acc0,%0"
                      : "=&r"(a)
                      : "r"(MD_EX(x1)), "r"(MD_EY(y1)), "r"(MD_EX(x2)), "r"(MD_EY(y2)), "r"(MD_EX(x3)), "r"(MD_EY(y3)));
    l = low - ((uint32_t)a << 17);                    /* S - a * 2^17, small and >= 0 */
    return (a + (int32_t)(l >> 17)) >> 5;
}
static inline int32_t md_floor22_2_x(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{
    int32_t a;
    uint32_t low = md_low32(x1, y1) + md_low32(x2, y2), l;
    __asm__ volatile ("mac.l %1,%2,%%acc0\n\tmac.l %3,%4,%%acc0\n\tmovclr.l %%acc0,%0"
                      : "=&r"(a) : "r"(MD_EX(x1)), "r"(MD_EY(y1)), "r"(MD_EX(x2)), "r"(MD_EY(y2)));
    l = low - ((uint32_t)a << 17);
    return (a + (int32_t)(l >> 17)) >> 5;
}
#else
static inline int32_t md_floor22_x(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3)
{ return (int32_t)(((int64_t)x1 * y1 + (int64_t)x2 * y2 + (int64_t)x3 * y3) >> 22); }
static inline int32_t md_floor22_2_x(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{ return (int32_t)(((int64_t)x1 * y1 + (int64_t)x2 * y2) >> 22); }
#endif
/* MD_AP_FLOOR (md-native.h, MD_ST_APPROX): floor of the EMAC's read, without the
 * 32-bit products that make it exact (short by a unit at most); the _x forms stay exact */
#ifdef MD_AP_FLOOR
static inline int32_t md_floor22(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3)
{ return md_ar3(x1, y1, x2, y2, x3, y3) >> 5; }
static inline int32_t md_floor22_2(int32_t x1, int32_t y1, int32_t x2, int32_t y2)
{ return md_ar2(x1, y1, x2, y2) >> 5; }
#else
static inline int32_t md_floor22(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3)
{ return md_floor22_x(x1, y1, x2, y2, x3, y3); }
static inline int32_t md_floor22_2(int32_t x1, int32_t y1, int32_t x2, int32_t y2) { return md_floor22_2_x(x1, y1, x2, y2); }
#endif
static inline uint32_t md_floor_word(int32_t t) { return md_acc_limit(t); }   /* acc_to_word's limit */
#endif
