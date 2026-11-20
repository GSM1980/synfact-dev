/* SPDX-License-Identifier: GPL-2.0-or-later
 * md-runtime.c - the compiler helpers the mdkit voices need on the Syntakt.
 *
 * The bank is freestanding (no libgcc) and the MCF54415 has no FPU and no
 * 32x32->64 multiply. GCC calls these by their libgcc names:
 *   __muldi3                     64-bit multiply (the 56-bit accumulators)
 *   __mulsf3 __addsf3 __subsf3   float32, round to nearest even, subnormals
 *   __floatunsisf __floatsisf    int -> float32
 *   __fixunssfsi __fixsfsi       float32 -> int, truncating
 * The floats are only the voices' control clock: a few operations a block.
 * Each is IEEE 754 binary32 exactly, so the voices compute here what they
 * compute on a host; tests/test_md_st.py checks them against the host's own
 * arithmetic. NaN and infinity are not produced by the voices; they are
 * passed through as such, without signalling.
 */
#include "voices/md-types.h"

typedef uint32_t f32;

#define SIGN 0x80000000u
#define EXP(a) (((a) >> 23) & 0xffu)
#define FRAC(a) ((a) & 0x7fffffu)

/* The float nearest to (m + t) * 2^(e - 150), ties to even, with the given
 * sign; t is a fraction in (0, 1) when sticky is nonzero, else 0. Callers
 * keep at least seven guard bits below the result's last bit, so shifting
 * m left over a nonzero sticky cannot move a tie. */
static f32 pack(uint32_t sign, int e, uint32_t m, uint32_t sticky)
{
    uint32_t r;
    if (!m) return sign;
    while (!(m & 0x80000000u)) { m <<= 1; --e; }
    e += 8;                           /* now (m >> 8) is the 24-bit significand */
    if (e >= 255) return sign | 0x7f800000u;
    if (e <= 0) {                     /* subnormal: scale 2^(1 - 150), field 0 */
        uint32_t shift = (uint32_t)(1 - e);
        if (shift > 31) { sticky |= m; m = 0; }
        else { sticky |= m & ((1u << shift) - 1u); m >>= shift; }
        r = m & 0xffu; m >>= 8;
        if (r > 0x80u || (r == 0x80u && (sticky || (m & 1u)))) ++m;
        return sign | m;              /* 0x800000 is the smallest normal */
    }
    r = m & 0xffu; m >>= 8;
    if (r > 0x80u || (r == 0x80u && (sticky || (m & 1u)))) ++m;
    if (m == 0x1000000u) { m = 0x800000u; if (++e >= 255) return sign | 0x7f800000u; }
    return sign | ((uint32_t)e << 23) | (m & 0x7fffffu);
}

/* The 24-bit significand and the exponent of a finite float's value
 * m * 2^(e - 150). */
static uint32_t unpack(f32 a, int *e)
{
    uint32_t x = EXP(a);
    if (!x) { *e = 1; return FRAC(a); }
    *e = (int)x;
    return FRAC(a) | 0x800000u;
}

f32 md_f32_mul(f32 a, f32 b)
{
    uint32_t sign = (a ^ b) & SIGN, ma, mb, hi, lo, mid1, mid2, sticky;
    int ea, eb;
    if (EXP(a) == 255 || EXP(b) == 255) {
        if ((EXP(a) == 255 && FRAC(a)) || (EXP(b) == 255 && FRAC(b))) return 0x7fc00000u;
        if (!(a & ~SIGN) || !(b & ~SIGN)) return 0x7fc00000u;
        return sign | 0x7f800000u;
    }
    ma = unpack(a, &ea); mb = unpack(b, &eb);
    if (!ma || !mb) return sign;
    /* 48-bit product from 16-bit halves (no 64-bit multiply here). */
    {
        uint32_t al = ma & 0xffffu, ah = ma >> 16, bl = mb & 0xffffu, bh = mb >> 16, t;
        lo = al * bl;
        mid1 = al * bh; mid2 = ah * bl;
        hi = ah * bh;
        t = lo + (mid1 << 16); hi += (mid1 >> 16) + (t < lo); lo = t;
        t = lo + (mid2 << 16); hi += (mid2 >> 16) + (t < lo); lo = t;
    }
    /* value = (hi:lo) * 2^(ea + eb - 300); keep the top 32 bits of the 64. */
    {
        int e = ea + eb - 300 + 150;           /* exponent for pack's m * 2^(e - 150) */
        uint32_t m;
        if (hi) {
            unsigned n = 0;
            while (!(hi & 0x80000000u)) { hi = (hi << 1) | (lo >> 31); lo <<= 1; ++n; }
            m = hi; sticky = lo; e += 32 - (int)n;
        } else {
            m = lo; sticky = 0;
        }
        return pack(sign, e, m, sticky);
    }
}

static f32 add_magnitudes(uint32_t sign, uint32_t ma, int ea, uint32_t mb, int eb)
{
    /* ma*2^ea + mb*2^eb, ea >= eb, 24-bit significands, 7 guard bits */
    uint32_t sticky = 0, d = (uint32_t)(ea - eb);
    ma <<= 7; mb <<= 7;
    if (d > 31) { sticky = mb; mb = 0; }
    else if (d) { sticky = mb & ((1u << d) - 1u); mb >>= d; }
    return pack(sign, ea - 7, ma + mb, sticky);
}

static f32 sub_magnitudes(uint32_t sign, uint32_t ma, int ea, uint32_t mb, int eb)
{
    /* ma*2^ea - mb*2^eb >= 0, ea >= eb */
    uint32_t sticky = 0, d = (uint32_t)(ea - eb), m;
    ma <<= 7; mb <<= 7;
    if (d > 31) { sticky = mb; mb = 0; }
    else if (d) { sticky = mb & ((1u << d) - 1u); mb >>= d; }
    /* subtracting a value with lost low bits: borrow one unit and keep sticky */
    m = ma - mb;
    if (sticky) { m -= 1u; sticky = 1u; }
    return pack(sign, ea - 7, m, sticky);
}

f32 md_f32_add(f32 a, f32 b)
{
    uint32_t ma, mb;
    int ea, eb;
    if (EXP(a) == 255 || EXP(b) == 255) {
        if ((EXP(a) == 255 && FRAC(a)) || (EXP(b) == 255 && FRAC(b))) return 0x7fc00000u;
        if (EXP(a) == 255 && EXP(b) == 255 && ((a ^ b) & SIGN)) return 0x7fc00000u;
        return EXP(a) == 255 ? a : b;
    }
    if (!(a & ~SIGN)) return (b & ~SIGN) ? b : (a & b);
    if (!(b & ~SIGN)) return a;
    ma = unpack(a, &ea); mb = unpack(b, &eb);
    if ((a ^ b) & SIGN) {
        if (ea < eb || (ea == eb && ma < mb))
            return sub_magnitudes(b & SIGN, mb, eb, ma, ea);
        if (ea == eb && ma == mb) return 0;
        return sub_magnitudes(a & SIGN, ma, ea, mb, eb);
    }
    if (ea < eb) return add_magnitudes(a & SIGN, mb, eb, ma, ea);
    return add_magnitudes(a & SIGN, ma, ea, mb, eb);
}

f32 md_f32_from_u32(uint32_t v) { return pack(0, 150, v, 0); }
f32 md_f32_from_s32(int32_t v)
{ return v < 0 ? pack(SIGN, 150, 0u - (uint32_t)v, 0) : pack(0, 150, (uint32_t)v, 0); }

uint32_t md_f32_to_u32(f32 a)
{
    int e;
    uint32_t m;
    if (a & SIGN) return 0;
    if (EXP(a) == 255) return 0xffffffffu;
    m = unpack(a, &e);
    e -= 150;
    if (e >= 9) return 0xffffffffu;
    return e >= 0 ? m << e : (e > -32 ? m >> -e : 0);
}
int32_t md_f32_to_s32(f32 a)
{
    uint32_t m = md_f32_to_u32(a & ~SIGN);
    if (m > 0x7fffffffu) m = (a & SIGN) ? 0x80000000u : 0x7fffffffu;
    return (a & SIGN) ? (int32_t)(0u - m) : (int32_t)m;
}

uint64_t md_mul64(uint64_t a, uint64_t b)
{
    uint32_t al = (uint32_t)a, ah = (uint32_t)(a >> 32), bl = (uint32_t)b, bh = (uint32_t)(b >> 32);
    uint32_t a0 = al & 0xffffu, a1 = al >> 16, b0 = bl & 0xffffu, b1 = bl >> 16;
    uint32_t lo = a0 * b0, m1 = a0 * b1, m2 = a1 * b0, hi = a1 * b1, t;
    t = lo + (m1 << 16); hi += (m1 >> 16) + (t < lo); lo = t;
    t = lo + (m2 << 16); hi += (m2 >> 16) + (t < lo); lo = t;
    hi += al * bh + ah * bl;
    return ((uint64_t)hi << 32) | lo;
}

#ifdef MD_ST_TARGET
/* The libgcc names; floats travel as their 32-bit patterns in this ABI. */
f32 __mulsf3(f32 a, f32 b) { return md_f32_mul(a, b); }
f32 __addsf3(f32 a, f32 b) { return md_f32_add(a, b); }
f32 __subsf3(f32 a, f32 b) { return md_f32_add(a, b ^ SIGN); }
f32 __floatunsisf(uint32_t v) { return md_f32_from_u32(v); }
f32 __floatsisf(int32_t v) { return md_f32_from_s32(v); }
uint32_t __fixunssfsi(f32 a) { return md_f32_to_u32(a); }
int32_t __fixsfsi(f32 a) { return md_f32_to_s32(a); }
uint64_t __muldi3(uint64_t a, uint64_t b) { return md_mul64(a, b); }
#endif
