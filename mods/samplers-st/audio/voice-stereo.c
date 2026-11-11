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
    s32 t,a,y1,b,y2,o;u32 i;
    for(i=0;i<32;i++){
        SUB_SAT(t,l[i],l1);MAC_CLR(a,t,c1);ADD_SAT(y1,a,l1);
        SUB_SAT(t,y1,l2);MAC_CLR(b,t,c2);ADD_SAT(y2,b,l2);
        SUB_SAT(o,y1,y2);l[i]=o;ADD_SAT(l1,y1,a);ADD_SAT(l2,y2,b);
        SUB_SAT(t,r[i],r1);MAC_CLR(a,t,c1);ADD_SAT(y1,a,r1);
        SUB_SAT(t,y1,r2);MAC_CLR(b,t,c2);ADD_SAT(y2,b,r2);
        SUB_SAT(o,y1,y2);r[i]=o;ADD_SAT(r1,y1,a);ADD_SAT(r2,y2,b);
    }
    fl[2]=(u32)l1;fl[3]=(u32)l2;fr[2]=(u32)r1;fr[3]=(u32)r2;
}
/* 400045a0 then 400049f4 (locals 0 and 1, second buffer zero) on L and R:
 * l and r are ramped in place, then out_l and out_r get the output ramp. */
void st_voice_gains2(s32 *l,s32 *r,s32 *out_l,s32 *out_r,void *voice)
{
    u32 *v=voice,i;s32 target,g,step,end,ramp,slope,a,b;
    /* 400045a0's start */
    if(v[0x34/4])v[0x3d0/4]=0;
    else if(v[0x38/4] && v[0x3e8/4]==0x80000000u)v[0x3d4/4]=v[0x3d0/4];
    target=(s32)v[0x3d0/4];g=(s32)v[0x3d4/4];SUB_SAT(step,target,g);step>>=5;
    /* 400049f4's first ramp (its second only multiplies the zero buffer) */
    end=(s32)v[2];ramp=(s32)v[6];slope=(s32)((u32)end-(u32)ramp)>>5;
    for(i=0;i<32;i++){
        MSAC_CLR(a,g,l[i]);MSAC_CLR(b,g,r[i]);l[i]=a;r[i]=b;ADD_SAT(g,g,step);
        MAC_CLR(out_l[i],ramp,a);MAC_CLR(out_r[i],ramp,b);ramp=(s32)((u32)ramp+(u32)slope);
    }
    v[0x3d4/4]=(u32)target;v[6]=(u32)end;v[7]=v[3];
}
#endif
