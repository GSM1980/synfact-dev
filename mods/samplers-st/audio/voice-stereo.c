/* SPDX-License-Identifier: GPL-2.0-or-later
 * The stock sample voice's render tail on two channels (samplers-st 1.4).
 * Written in the core's shape: any machine that renders through the native
 * voice helpers can use it for a stereo track.
 *
 * A sampler track's render runs, after its PCM: the render filter 400068ea
 * (two one-pole stages, voice + 0x6f8: coefficients, then memory), the gain
 * control 40003d42, the gain ramp 400045a0 (in place, negated) and the output
 * ramp 400049f4 (out = ramped gain x buffer, plus a second ramped gain times
 * the zeroed second buffer). For a stereo track R takes the same four calls,
 * with L's coefficients and its own filter memory, and the ramps from the
 * values L started from (docs/MACHINE-REFERENCE.md). These functions do L
 * and R in one loop each, with the natives' own EMAC operations, in their
 * order, under the caller's MACSR: the same output and the same voice state,
 * word for word (tests/test_st_stereo_natives_emu.py runs both in
 * analog-emu). The products with the zeroed second buffer add nothing and
 * are left out. One stereo track's tail: 3137 instructions native, 2581 here.
 *
 * Order: st_voice_filter2, then the native 40003d42 (it sets the gain target
 * the ramp reads), then st_voice_gains2, as the native calls run. The first
 * MAC of L's filter finds ACC0 as the native one would.
 */
#include "sampler.h"
typedef sp_s32 s32;typedef sp_u32 u32;
#ifdef __mcoldfire__
#define SUB_SAT(out,a,b) __asm__("move.l %1,%0\n\tsub.l %2,%0\n\tsats.l %0":"=&d"(out):"r"(a),"r"(b):"cc")
#define ADD_SAT(out,a,b) __asm__("move.l %1,%0\n\tadd.l %2,%0\n\tsats.l %0":"=&d"(out):"r"(a),"r"(b):"cc")
#define MAC_CLR(out,a,b) __asm__ volatile("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0":"=d"(out):"r"(a),"r"(b):"cc")
#define MSAC_CLR(out,a,b) __asm__ volatile("msac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0":"=d"(out):"r"(a),"r"(b):"cc")
/* 400068ea on L (filter fl) and R (fr: its own memory; coefficients fl's). */
void st_voice_filter2(s32 *l,s32 *r,u32 *fl,u32 *fr)
{
    s32 c1=(s32)fl[0],c2=(s32)fl[1],l1=(s32)fl[2],l2=(s32)fl[3],r1=(s32)fr[2],r2=(s32)fr[3];
    s32 t,a,y1,b;const s32 *end;
    /* Per channel and frame, with state (s1, s2): t = sat(x - s1),
     * a = mac(t, c1), y1 = sat(a + s1), t = sat(y1 - s2), b = mac(t, c2),
     * y2 = sat(b + s2), out = sat(y1 - y2), s1 = sat(y1 + a), s2 = sat(y2 + b):
     * the C above as it was (SUB_SAT, MAC_CLR, ADD_SAT), by hand, with s2
     * and the loaded sample as temporaries once they are spent. L then R.
     * The coefficients are the MAC's first operand (an address register
     * there is a valid encoding; as the second, with some data registers,
     * it is not: GAS takes `mac.l %d2,%a3` and emits 0xa642). */
#define FILTER_CHANNEL(p,s1,s2) \
        "move.l (%[" #p "]),%[t]\n\tsub.l %[" #s1 "],%[t]\n\tsats.l %[t]\n\t" \
        "mac.l %[c1],%[t],%%acc0\n\tmovclr.l %%acc0,%[a]\n\t" \
        "move.l %[a],%[y1]\n\tadd.l %[" #s1 "],%[y1]\n\tsats.l %[y1]\n\t" \
        "move.l %[y1],%[t]\n\tsub.l %[" #s2 "],%[t]\n\tsats.l %[t]\n\t" \
        "mac.l %[c2],%[t],%%acc0\n\tmovclr.l %%acc0,%[b]\n\t" \
        "move.l %[b],%[t]\n\tadd.l %[" #s2 "],%[t]\n\tsats.l %[t]\n\t" \
        "move.l %[y1],%[" #s2 "]\n\tsub.l %[t],%[" #s2 "]\n\tsats.l %[" #s2 "]\n\tmove.l %[" #s2 "],(%[" #p "])+\n\t" \
        "move.l %[y1],%[" #s1 "]\n\tadd.l %[a],%[" #s1 "]\n\tsats.l %[" #s1 "]\n\t" \
        "move.l %[t],%[" #s2 "]\n\tadd.l %[b],%[" #s2 "]\n\tsats.l %[" #s2 "]\n\t"
    /* The end is computed here, in its own register: as an input it may
     * share one with r (r is l + 32 when L and R are adjacent). */
    __asm__ volatile(
        "lea 128(%[l]),%[e]\n"
        "1:\n\t"
        FILTER_CHANNEL(l,l1,l2)
        FILTER_CHANNEL(r,r1,r2)
        "cmpa.l %[e],%[l]\n\tbne.w 1b"
        :[l]"+a"(l),[r]"+a"(r),[l1]"+d"(l1),[l2]"+d"(l2),[r1]"+d"(r1),[r2]"+d"(r2),
         [t]"=&d"(t),[a]"=&d"(a),[y1]"=&d"(y1),[b]"=&d"(b),[e]"=&a"(end)
        :[c1]"a"(c1),[c2]"a"(c2):"cc","memory");
#undef FILTER_CHANNEL
    fl[2]=(u32)l1;fl[3]=(u32)l2;fr[2]=(u32)r1;fr[3]=(u32)r2;
}
/* 400045a0 then 400049f4 (locals 0 and 1, second buffer zero) on L and R:
 * l and r are ramped in place, then out_l and out_r get the output ramp. */
void st_voice_gains2(s32 *l,s32 *r,s32 *out_l,s32 *out_r,void *voice)
{
    u32 *v=voice;s32 target,g,step,end,ramp,slope,a,b;const s32 *stop;
    /* 400045a0's start */
    if(v[0x34/4])v[0x3d0/4]=0;
    else if(v[0x38/4] && v[0x3e8/4]==0x80000000u)v[0x3d4/4]=v[0x3d0/4];
    target=(s32)v[0x3d0/4];g=(s32)v[0x3d4/4];SUB_SAT(step,target,g);step>>=5;
    /* 400049f4's first ramp (its second only multiplies the zero buffer) */
    end=(s32)v[2];ramp=(s32)v[6];slope=(s32)((u32)end-(u32)ramp)>>5;
    /* a = msac(g, l), b = msac(g, r), l = a, r = b, g = sat(g + step),
     * out_l = mac(ramp, a), out_r = mac(ramp, b), ramp += slope: the C loop
     * as it was, in its order, with pointers that advance. */
    __asm__ volatile(
        "lea 128(%[l]),%[e]\n"
        "1:\n\t"
        "move.l (%[l]),%[a]\n\tmsac.l %[g],%[a],%%acc0\n\tmovclr.l %%acc0,%[a]\n\t"
        "move.l (%[r]),%[b]\n\tmsac.l %[g],%[b],%%acc0\n\tmovclr.l %%acc0,%[b]\n\t"
        "move.l %[a],(%[l])+\n\tmove.l %[b],(%[r])+\n\t"
        "add.l %[s],%[g]\n\tsats.l %[g]\n\t"
        "mac.l %[ramp],%[a],%%acc0\n\tmovclr.l %%acc0,%[a]\n\tmove.l %[a],(%[ol])+\n\t"
        "mac.l %[ramp],%[b],%%acc0\n\tmovclr.l %%acc0,%[b]\n\tmove.l %[b],(%[or])+\n\t"
        "add.l %[slope],%[ramp]\n\t"
        "cmpa.l %[e],%[l]\n\tbne.s 1b"
        :[l]"+a"(l),[r]"+a"(r),[ol]"+a"(out_l),[or]"+a"(out_r),[g]"+d"(g),[ramp]"+d"(ramp),
         [a]"=&d"(a),[b]"=&d"(b),[e]"=&a"(stop)
        :[s]"d"(step),[slope]"d"(slope):"cc","memory");
    v[0x3d4/4]=(u32)target;v[6]=(u32)end;v[7]=v[3];
}
#endif
