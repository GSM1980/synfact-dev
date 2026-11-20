/* SPDX-License-Identifier: GPL-2.0-or-later
 * md_st_runtime_check.c - the MD machine's compiler helpers
 * (mods/md-st/audio/md-runtime.c) against the host's own IEEE arithmetic and
 * 64-bit multiply. Built and run by tests/test_md_st.py.
 * Usage: md_st_runtime_check [cases]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../mods/md-st/audio/md-runtime.c"

static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float flt(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static uint64_t state = 0x9e3779b97f4a7c15ull;
static uint32_t rnd(void) { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return (uint32_t)(state >> 16); }
static int finite(uint32_t u) { return ((u >> 23) & 0xff) != 0xff; }
static uint32_t pick(void)
{
    /* mostly ordinary values, some subnormals, zeros and exact integers */
    switch (rnd() % 8) {
    case 0: return rnd() & 0x807fffffu;                       /* subnormal or zero */
    case 1: return bits((float)(rnd() % 129)) ^ (rnd() & 0x80000000u);
    case 2: return bits((float)(rnd() % 16257) / 128.0f);     /* the clock's values */
    case 3: return (rnd() & 0x80ffffffu) | 0x3f000000u;       /* near 1 */
    default: return rnd();
    }
}

int main(int argc, char **argv)
{
    unsigned long n = argc > 1 ? strtoul(argv[1], 0, 10) : 2000000ul, i, bad = 0;
    for (i = 0; i < n && bad < 20; ++i) {
        uint32_t a = pick(), b = pick(), u = rnd(), want, got;
        if (finite(a) && finite(b)) {
            want = bits(flt(a) * flt(b)); got = md_f32_mul(a, b);
            if (finite(want) && want != got && !(((want | got) & 0x7fffffffu) == 0 && want == got)) {
                printf("FAIL mul %08x %08x: %08x want %08x\n", a, b, got, want); ++bad; }
            want = bits(flt(a) + flt(b)); got = md_f32_add(a, b);
            if (finite(want) && want != got) { printf("FAIL add %08x %08x: %08x want %08x\n", a, b, got, want); ++bad; }
            want = bits(flt(a) - flt(b)); got = md_f32_add(a, b ^ 0x80000000u);
            if (finite(want) && want != got) { printf("FAIL sub %08x %08x: %08x want %08x\n", a, b, got, want); ++bad; }
        }
        if (bits((float)u) != md_f32_from_u32(u)) { printf("FAIL u2f %u\n", u); ++bad; }
        if (bits((float)(int32_t)u) != md_f32_from_s32((int32_t)u)) { printf("FAIL s2f %d\n", (int32_t)u); ++bad; }
        if (finite(a) && !(a >> 31) && flt(a) < 4294967296.0f && (uint32_t)flt(a) != md_f32_to_u32(a)) {
            printf("FAIL f2u %08x\n", a); ++bad; }
        if (finite(a) && flt(a) < 2147483648.0f && flt(a) > -2147483648.0f && (int32_t)flt(a) != md_f32_to_s32(a)) {
            printf("FAIL f2s %08x\n", a); ++bad; }
        {
            uint64_t x = ((uint64_t)rnd() << 32) | rnd(), y = ((uint64_t)rnd() << 32) | rnd();
            if (rnd() & 1) x = (uint64_t)(int64_t)(int32_t)(x & 0xffffff) << (rnd() % 24);
            if (md_mul64(x, y) != x * y) { printf("FAIL mul64\n"); ++bad; }
        }
    }
    /* the clocks' own iterations: (3x + t) / 4 and 0.75x down to zero */
    for (unsigned t = 0; t < 128 && !bad; ++t)
        for (unsigned x0 = 0; x0 < 128; x0 += 7) {
            float x = (float)x0; uint32_t xb = bits(x), three = bits(3.0f), quarter = bits(0.25f);
            for (unsigned k = 0; k < 400; ++k) {
                x = (3.0f * x + (float)t) * 0.25f;
                xb = md_f32_mul(md_f32_add(md_f32_mul(three, xb), md_f32_from_u32(t)), quarter);
                if (bits(x) != xb) { printf("FAIL clock t=%u x0=%u step %u\n", t, x0, k); ++bad; break; }
            }
        }
    printf(bad ? "FAILED\n" : "PASS md-runtime: %lu cases\n", i);
    return bad != 0;
}
