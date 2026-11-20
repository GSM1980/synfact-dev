/* SPDX-License-Identifier: GPL-2.0-or-later
 * md_st_acc_check.c - every helper of mods/md-st/audio/voices/md-acc.h
 * against mdkit's own (as mdkit/src/efm/sd writes them), on random and edge
 * values. Built and run by tests/test_md_st.py.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../mods/md-st/mdkit/src/efm/sd/mdkit_efm_sd.c"
#include "../mods/md-st/audio/voices/md-acc.h"

static uint64_t seed = 0x853c49e6748fea9bull;
static uint64_t r64(void) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed; }
static uint32_t word(void)
{
    static const uint32_t edge[] = {0, 1, 0x7fffff, 0x800000, 0xffffff, 0x400000, 0xc00000, 0xfff, 0x1000};
    return r64() % 4 ? (uint32_t)r64() & 0xffffffu : edge[r64() % 9];
}
static uint64_t value(void)                 /* an accumulator, mdkit's form */
{
    uint64_t v = r64() & MDKIT_MASK56;
    switch (r64() % 6) {
    case 0: return mdkit_from24(word());
    case 1: return mdkit_mpy(word(), word());
    case 2: return v >> (r64() % 56);
    case 3: return (0 - (v >> (r64() % 56))) & MDKIT_MASK56;
    case 4: return mdkit_pack48(word(), word());
    default: return v;
    }
}
static md_acc enc(uint64_t a) { return md_acc_from_value(mdkit_s56(a)); }
/* a 48-bit word in the helpers' form */
static uint64_t enc48(uint64_t w) { return md_acc_make((int32_t)((w >> 24) & 0xffffff), (uint32_t)w & 0xffffff); }

#define SAME(what, ours, theirs) do { if ((ours) != (theirs)) { \
    printf("FAIL %s: case %lu\n", what, n); return 1; } } while (0)

int main(int argc, char **argv)
{
    unsigned long cases = argc > 1 ? strtoul(argv[1], 0, 10) : 1000000ul, n;
    for (n = 0; n < cases; ++n) {
        uint64_t a = value(), b = value(), w48 = r64();
        uint32_t x = word(), y = word(), mask = word();
        unsigned k = (unsigned)(r64() % 57);
        md_acc ea = enc(a), eb = enc(b);
        SAME("encoding", md_acc_s56(ea), mdkit_s56(a));
        SAME("mid", md_acc_mid(ea), mdkit_mid(a));
        SAME("low", md_acc_low(ea), mdkit_low(a));
        SAME("from24", md_acc_from24(x), enc(mdkit_from24(x)));
        SAME("from48", md_acc_from48(enc48(w48 & MDKIT_MASK48)), enc(mdkit_from48(w48)));
        SAME("from48_raw", md_acc_from48_raw(w48), enc(mdkit_from48(w48)));
        SAME("to48", md_acc_to48(ea), enc48(mdkit_to48(a)));
        SAME("from48(to48)", md_acc_from48(md_acc_to48(ea)), enc(mdkit_from48(mdkit_to48(a))));
        SAME("setmid", md_acc_setmid(ea, x), enc(mdkit_setmid(a, x)));
        SAME("and_mid", md_acc_and_mid(ea, mask), enc(mdkit_and_mid(a, mask)));
        SAME("pack48", md_acc_pack48(x, y), enc48(mdkit_pack48(x, y)));
        SAME("from48(pack48)", md_acc_from48(md_acc_pack48(x, y)), enc(mdkit_from48(mdkit_pack48(x, y))));
        SAME("add", md_acc_add(ea, eb), enc(mdkit_add(a, b)));
        SAME("sub", md_acc_sub(ea, eb), enc(mdkit_sub(a, b)));
        SAME("asl", md_acc_asl(ea, k), enc(mdkit_asl(a, k)));
        SAME("asr", md_acc_asr(ea, k % 56), enc(mdkit_asr(a, k % 56)));
        SAME("mpy", md_acc_mpy(x, y), enc(mdkit_mpy(x, y)));
        SAME("mpy_su", md_acc_mpy_su(x, y), enc(mdkit_mpy_su(x, y)));
        SAME("mpyr", md_acc_mpyr(x, y), enc(mdkit_mpyr(x, y)));
        SAME("mac", md_acc_mac(ea, x, y), enc(mdkit_mac(a, x, y)));
        SAME("macr", md_acc_macr(ea, x, y), enc(mdkit_macr(a, x, y)));
        SAME("acc_to_word", md_acc_acc_to_word(ea), mdkit_acc_to_word(a));
    }
    printf("PASS md-acc: %lu cases, every helper\n", n);
    return 0;
}
