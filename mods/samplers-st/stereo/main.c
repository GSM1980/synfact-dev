/* SPDX-License-Identifier: GPL-2.0-or-later
 * Stereo tracks, MAIN side (<syntakt/stereo.h>). Written in the core's shape
 * so that it can move into mods/core-st with the AUDIO engine (audio.c).
 *
 * MAIN mixes each track's L with level x pan gains as before. This module
 * adds gR x D, D = R - L from AUDIO, to every bus the track's right gain
 * feeds: the delay and reverb inputs and the two main buses. Left gets
 * gL x L and right gR x R.
 *
 * Pairing D with the right block: AUDIO writes D, then starts the DMA of
 * the block's L. MAIN reads L by DMA inside its audio interrupt and mixes it
 * later in the same interrupt, but AUDIO may finish a block at any moment of
 * that interrupt. So D comes with a fingerprint of its block's L
 * (st_stereo_print). MAIN reads D at ev_render_in; at the first mix event it
 * fingerprints the L it is mixing (ST_TRACK_PCM) and uses the D that matches:
 * the one read at ev_render_in, or, if AUDIO wrote again since, the newer one.
 * When none matches (a read that overlapped AUDIO's write), the track is mono
 * for that block.
 *
 * The gains: the stock mix kernels ramp each gain over the block from the
 * value they found in their state array to target, then store the end value
 * there. At each event the arrays hold the end values. After MAIN/DIRECT
 * correction, commit all four rows together as the next block's starts.
 */
#include <syntakt/modules.h>
#include <syntakt/stereo.h>
#include <syntakt/host.h>
#include <syntakt/sram.h>
typedef st_u32 u32;typedef int s32;
#ifndef WIN
#define WIN(offset) (*(const volatile u32 *)(ST_STEREO_MAIN+(offset)))
#endif
#ifndef DELAY_GAIN
/* State arrays of the stock right-channel kernels (tracks 0..7 first). */
#define DELAY_GAIN ((const volatile s32 *)0x80006308u)
#define REVERB_GAIN ((const volatile s32 *)0x800062c0u)
#define MAIN_GAIN ((const volatile s32 *)0x8000e160u)
#define DIRECT_GAIN ((const volatile s32 *)0x8000e110u)
#define SEND_BUS ((s32 *)0x8000dbd0u)      /* ST_FX_SEND_BUS */
#define MAIN_BUS ((s32 *)0x8000dad0u)      /* ST_FX_TRACK_BUS */
#define DIRECT_BUS ((s32 *)0x8000dfd0u)    /* ST_PREVIEW_BUS */
#endif
#ifndef TRACK_PCM
#define TRACK_PCM ((const volatile s32 *)0x80003d50u)   /* ST_TRACK_PCM(0) */
#endif
#include "main-state.h"
static struct stereo_main_state stereo_ddr;
struct stereo_main_state *st_stereo_state=&stereo_ddr;
static u32 (*resolve_kernel)(void)=st_stereo_resolve;
#if defined(__has_include)
#if __has_include("stereo-test.h")
#include "stereo-test.h"
#endif
#endif
#ifndef ST_STEREO_MAIN_PROFILE
#if defined(SP_STEREO_TEST) && SP_STEREO_TEST
#define ST_STEREO_MAIN_PROFILE 1
#else
#define ST_STEREO_MAIN_PROFILE 0
#endif
#endif
#ifdef __mcoldfire__
/* The block's D / 2 in Q31 (mantissa << shift), once per interrupt for the
 * four buses; a silent track for padding. */
static s32 wide[8][32];
static const s32 silent[32];
/* Keep the assembly's private struct offsets checked by the compiler. */
_Static_assert(__builtin_offsetof(struct block,mask)==4,"assembly mask offset");
_Static_assert(__builtin_offsetof(struct block,shift)==12,"assembly shift offset");
_Static_assert(__builtin_offsetof(struct block,d)==20,"assembly samples offset");
_Static_assert(sizeof(struct block)==532,"assembly block stride");
extern void st_stereo_widen(const struct block *,s32 *);
static void (*widen_kernel)(const struct block *,s32 *)=st_stereo_widen;
static void widen(const struct block *b)
{widen_kernel(b,wide);}
#endif
/* Counters for test pages: interrupts, blocks mixed with D, reads caught
 * mid-write, blocks with no matching D, a block left from before this boot
 * (not mixed), blocks that needed the second read, the tracks with D, a block
 * mixed again with the same L, and MAIN's cost (permille: reading, mixing). */
struct st_stereo_counters st_stereo_main_counters;
#include "main-placement.h"
/* SEQ_END first, then the block, then SEQ: equal means one whole block (the
 * DMA writes SEQ first and SEQ_END last). */
u32 st_stereo_read_block(struct block *b)
{
    u32 s=WIN(ST_STEREO_SEQ_END),m,t,bits,x,y;
    m=WIN(ST_STEREO_MASK)&0xffu;
    b->print=WIN(ST_STEREO_PRINT);
    /* Reject an in-flight publication before copying up to 512 bytes. Still
     * check after the copy: AUDIO may start another publication meanwhile. */
    if(WIN(ST_STEREO_SEQ)!=s){st_stereo_main_counters.torn++;return 0;}
    if(!m){b->mask=0;b->sequence=s;return 1;}
    x=WIN(ST_STEREO_SHIFT);y=WIN(ST_STEREO_SHIFT+4);
    for(bits=m;bits;bits&=bits-1u){
        t=31u-(u32)__builtin_clz(bits&-bits);
        b->shift[t]=(unsigned char)((t<4?x:y)>>(24-8*(t&3)));
#ifdef __mcoldfire__
        copy64((const volatile void *)(ST_STEREO_MAIN+ST_STEREO_WINDOW+64*t),b->d[t]);
#else
        {u32 i,w;
        for(i=0;i<32;i+=2){
            w=WIN(ST_STEREO_WINDOW+64*t+2*i);
            b->d[t][i]=(short)(w>>16);b->d[t][i+1]=(short)w;
        }
        }
#endif
    }
    if(WIN(ST_STEREO_SEQ)!=s){st_stereo_main_counters.torn++;return 0;}
    b->mask=m;b->sequence=s;return 1;
}
/* MAIN profile for test pages: timer ticks spent here, as permille of the
 * interrupt period, per 1024 interrupts. */
#ifndef MAIN_CLOCK
#define MAIN_CLOCK (*(volatile u32 *)0xfc07000cu)
#endif
#if ST_STEREO_MAIN_PROFILE
static u32 period,spent_read,spent_mix,last_tick,profiled;
static void spend(u32 *where,u32 since){*where+=MAIN_CLOCK-since;}
#endif
static void receive(void)
{
#if ST_STEREO_MAIN_PROFILE
    u32 a=MAIN_CLOCK,unit;
    if(last_tick)period+=a-last_tick;last_tick=a;
    if(++profiled==1024){
        unit=period/1000u;
        st_stereo_main_counters.read=unit?spent_read/unit:0;
        st_stereo_main_counters.mix=unit?spent_mix/unit:0;
        period=spent_read=spent_mix=profiled=0;
    }
#endif
    st_stereo_main_counters.interrupts++;
    resolved=0;use=0;have=0;
    tagged=WIN(ST_STEREO_TAG)==ST_STEREO_MAGIC;
    if(tagged)have=st_stereo_read_block(&slot[0]);
#if ST_STEREO_MAIN_PROFILE
    spend(&spent_read,a);
#endif
}
/* Once per interrupt, at the first mix event: the D of the L being mixed. */
#ifndef __mcoldfire__
#include "main-resolve.c"
#endif
static __inline__ u32 current(void)
{
    u32 m;
#if ST_STEREO_MAIN_PROFILE
    u32 a=MAIN_CLOCK;
#endif
    if(resolved)return use?use->mask:0;
    if(!tagged){resolved=1;return 0;}
    m=resolve_kernel();
#ifdef __mcoldfire__
    if(m)widen(use);
#endif
#if ST_STEREO_MAIN_PROFILE
    spend(&spent_read,a);
#endif
    return m;
}
/* For other MAIN modules in the audio interrupt, after ev_render_in (the
 * Recorder at ev_main_mix): track t's D for the L being mixed, in PCM16
 * units (R = L + D, up to +-2 x full scale). 0: the track is mono now. */
int st_stereo_track_d(u32 t,s32 *out)
{
    u32 i,m=current();int e;const short *d;
    if(t>=8 || !(m&(1u<<t)) || !use)return 0;
    d=use->d[t];e=(int)use->shift[t]-15;
    for(i=0;i<32;i++)out[i]=e>=0?(s32)d[i]<<e:(s32)d[i]>>-e;
    return 1;
}
#ifdef __mcoldfire__
/* bus[2i] -= g_k(i) x 2 x h_k(i) for four tracks k, h = D / 2 in Q31: EMAC
 * MSAC, fractional, saturating, truncating (MACSR 0xa0), each product taken
 * twice (the scale factor applies in integer modes only, MCF5441x RM 5.3.5;
 * D itself does not fit 32 bits), the eight summed in ACC0 and saturated once.
 * g ramps by step a frame, from the value it found (the stock kernels' own
 * ramp). */
static void mix4(s32 *bus,const s32 *const *h,const s32 *g,const s32 *step)
{mix4_kernel(bus,h,g,step,4);}
/* The interrupted code's EMAC state: MACSR, ACC0 and ACCext01, read and
 * written raw (integer mode, no saturation), so nothing it holds changes. */
struct emac_state {u32 macsr,acc,ext;};
static void emac_enter(struct emac_state *e)
{
    u32 macsr,acc,ext,clear;
    __asm__ volatile("move.l %%macsr,%0\n\tmove.l #0,%%macsr\n\tmove.l %%acc0,%1\n\t"
                     "move.l %%accext01,%2\n\tmove.l #0xa0,%%macsr\n\tmovclr.l %%acc0,%3"
                     :"=&d"(macsr),"=&d"(acc),"=&d"(ext),"=&d"(clear)::"cc");
    e->macsr=macsr;e->acc=acc;e->ext=ext;
}
static void emac_leave(const struct emac_state *e)
{
    __asm__ volatile("move.l #0,%%macsr\n\tmove.l %0,%%acc0\n\tmove.l %1,%%accext01\n\tmove.l %2,%%macsr"
                     ::"d"(e->acc),"d"(e->ext),"d"(e->macsr):"cc");
}
#endif
static s32 subtract(s32 y,s32 x)
{
    u32 a=(u32)y,r=a-(u32)x;
    if(((a^(u32)x)&(a^r))&0x80000000u)r=y<0?0x80000000u:0x7fffffffu;
    return (s32)r;
}
/* bus[2i+1] -= g_i x D_i (Q31, as the kernels' MSAC), g ramping over the block.
 * A track whose gain is zero over the whole block adds nothing. On the unit
 * with EMAC, four tracks a pass (mix4); the host test builds the C below. */
#ifdef __mcoldfire__
/* Standalone kernel-oracle calls retain their immediate gain postcondition.
 * Production events commit all four rows together at the end of MAIN. */
static __inline__ void remember_gains(const volatile s32 *from,s32 *to)
{
    __asm__ volatile(
        "movem.l (%0),%%d0-%%d1\n\tmovem.l %%d0-%%d1,(%1)\n\t"
        "movem.l 8(%0),%%d0-%%d1\n\tmovem.l %%d0-%%d1,8(%1)\n\t"
        "movem.l 16(%0),%%d0-%%d1\n\tmovem.l %%d0-%%d1,16(%1)\n\t"
        "movem.l 24(%0),%%d0-%%d1\n\tmovem.l %%d0-%%d1,24(%1)"
        ::"a"(from),"a"(to):"d0","d1","memory");
}
/* MAIN runs after all four native gain kernels. Commit their final rows
 * together for the next block, using the active worker's saved registers. */
static __inline__ void remember_all_gains(void)
{
    const volatile s32 *from; struct stereo_main_state *to=stereo_state;
#if ST_STEREO_MAIN_PROFILE
    u32 a=MAIN_CLOCK;
#endif
    __asm__ volatile(
        "movea.l #0x80006308,%0\n\tmovem.l (%0),%%d0-%%d7\n\tmovem.l %%d0-%%d7,1092(%1)\n\t"
        "movea.l #0x800062c0,%0\n\tmovem.l (%0),%%d0-%%d7\n\tmovem.l %%d0-%%d7,1124(%1)\n\t"
        "movea.l #0x8000e160,%0\n\tmovem.l (%0),%%d0-%%d7\n\tmovem.l %%d0-%%d7,1156(%1)\n\t"
        "movea.l #0x8000e110,%0\n\tmovem.l (%0),%%d0-%%d7\n\tmovem.l %%d0-%%d7,1188(%1)\n\t"
        "moveq #1,%%d0\n\tmoveq #1,%%d1\n\tmovem.l %%d0-%%d1,1220(%1)\n\tmovem.l %%d0-%%d1,1228(%1)"
        :"=&a"(from):"a"(to):"d0","d1","d2","d3","d4","d5","d6","d7","memory");
#if ST_STEREO_MAIN_PROFILE
    spend(&spent_mix,a);
#endif
}
/* Specialize the active workers' bus/gain/row constants. Their large EMAC
 * frames are only entered for a nonempty mask; empty callbacks stay small.
 * MAIN and DIRECT share the result already resolved for this callback. */
static __attribute__((always_inline)) __inline__ void add_nonempty(u32 k,s32 *bus,const volatile s32 *gain,u32 m)
{
    u32 t,bits,n=0,entered=0;s32 g[4],step[4],start,end;const s32 *h[4];
    struct emac_state saved;
    s32 *previous=ramp_start[k];u32 primed=ramp_primed[k];
#if ST_STEREO_MAIN_PROFILE
    u32 a=MAIN_CLOCK;
#endif
    for(bits=m;bits;bits&=bits-1u){
        t=31u-(u32)__builtin_clz(bits&-bits);
        end=gain[t];start=primed?previous[t]:end;
        if(!start && !end)continue;
        h[n]=wide[t];g[n]=start;step[n]=(s32)((u32)end-(u32)start)>>5;
        if(++n==4){
            if(!entered){emac_enter(&saved);entered=1;}
            mix4(bus+1,h,g,step);n=0;
        }
    }
    if(n){
        u32 active=n;
        for(;n<4;n++){h[n]=silent;g[n]=0;step[n]=0;}
        if(!entered){emac_enter(&saved);entered=1;}
        mix4_kernel(bus+1,h,g,step,active);
    }
    if(entered)emac_leave(&saved);
#if ST_STEREO_MAIN_PROFILE
    spend(&spent_mix,a);
#endif
}
static __attribute__((noinline)) void delay_nonempty(u32 m){add_nonempty(0,SEND_BUS,DELAY_GAIN,m);}
static __attribute__((noinline)) void reverb_nonempty(u32 m){add_nonempty(1,SEND_BUS,REVERB_GAIN,m);}
static __attribute__((noinline)) void main_nonempty(u32 m)
{add_nonempty(2,MAIN_BUS,MAIN_GAIN,m);add_nonempty(3,DIRECT_BUS,DIRECT_GAIN,m);remember_all_gains();}
/* Internal kernel-oracle entry: unlike the event sequence, a standalone add
 * has no later MAIN callback to commit its gain row. Keep its original
 * postcondition for independent arbitrary-bus EMAC/model checks. */
static __inline__ void add(u32 k,s32 *bus,const volatile s32 *gain)
{u32 m=current();if(m)add_nonempty(k,bus,gain,m);remember_gains(gain,ramp_start[k]);ramp_primed[k]=1;}
#else
/* The host build (tests/test_st_stereo.c): the same sums in integer C. */
static void add(u32 k,s32 *bus,const volatile s32 *gain)
{
    u32 t,i,m=current();s32 g,end,step,p,x,r,*b;int e;const short *d;
#if ST_STEREO_MAIN_PROFILE
    u32 a=MAIN_CLOCK;
#endif
    for(t=0;t<8;t++){
        end=gain[t];g=ramp_primed[k]?ramp_start[k][t]:end;ramp_start[k][t]=end;
        if(!(m&(1u<<t)) || (!g && !end))continue;
        /* g x D / 2^31 = (g >> 16) x m x 2^(shift - 14); |(g>>16) x m| < 2^30,
         * so only shifts 15 and 16 can overflow, and then only near +-2. */
        step=(end-g)>>5;d=use->d[t];e=(int)use->shift[t]-14;b=bus+1;
        if(e<=0){
            /* shifts 0..14, every block that is not near full scale: the
             * same products and saturating subtraction as subtract() */
            for(e=-e,i=32;i;i--,g+=step,b+=2){
                p=((s32)(short)(g>>16)*(s32)*d++)>>e;
                x=*b;r=(s32)((u32)x-(u32)p);
                if((x^p)&(x^r)&(s32)0x80000000u)r=x<0?(s32)0x80000000u:0x7fffffff;
                *b=r;
            }
            continue;
        }
        for(i=0;i<32;i++,g+=step){
            p=(s32)(short)(g>>16)*d[i];
            if(p>(0x7fffffff>>e))p=0x7fffffff;
            else if(p<-(0x7fffffff>>e))p=-0x7fffffff;
            else p<<=e;
            bus[2*i+1]=subtract(bus[2*i+1],p);
        }
    }
    ramp_primed[k]=1;
#if ST_STEREO_MAIN_PROFILE
    spend(&spent_mix,a);
#endif
}
#endif
#ifdef __mcoldfire__
static void delay_in(void){u32 m=current();if(m)delay_nonempty(m);}
static void reverb_in(void){u32 m=current();if(m)reverb_nonempty(m);}
static void main_mix(void)
{
    u32 m=current();
    if(m)main_nonempty(m);
    else{
#if ST_STEREO_MAIN_PROFILE
        u32 a=MAIN_CLOCK;
#endif
        remember_kernel();
#if ST_STEREO_MAIN_PROFILE
        spend(&spent_mix,a);
#endif
    }
}
#else
static void delay_in(void){add(0,SEND_BUS,DELAY_GAIN);}
static void reverb_in(void){add(1,SEND_BUS,REVERB_GAIN);}
static void main_mix(void){add(2,MAIN_BUS,MAIN_GAIN);add(3,DIRECT_BUS,DIRECT_GAIN);}
#endif
static int initialize(const struct st_smz_api *api)
{
#ifdef __mcoldfire__
    if(api && api->ui && api->ui->version>=5 && api->ui->host && api->ui->host->version>=12)
        stereo_host=api->ui->host;
#else
    (void)api;
#endif
    boot_sequence=WIN(ST_STEREO_SEQ);   /* what a restart left in the window */
    return 0;
}
static const struct st_smz_subscription subscriptions[] __attribute__((aligned(4)))={
    {ST_EV_RENDER_IN,receive},
    {ST_EV_DELAY_IN,delay_in},
    {ST_EV_REVERB_IN,reverb_in},
    {ST_EV_MAIN_MIX,main_mix}
#ifdef __mcoldfire__
    ,{ST_EV_TICK,(void (*)(void))placement_tick}
#endif
};
const struct st_smz_module st_stereo_module={"STEREO",initialize,subscriptions,sizeof(subscriptions)/sizeof(subscriptions[0])};
