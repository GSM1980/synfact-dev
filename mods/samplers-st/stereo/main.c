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
 * there. At each event the arrays hold the end values; this module keeps the
 * start (the previous end) itself.
 */
#include <syntakt/modules.h>
#include <syntakt/stereo.h>
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
struct block {u32 sequence,mask,print;unsigned char shift[8];short d[8][32];};
static struct block slot[2];
static const struct block *use;
static u32 tagged,have,resolved,last_used,boot_sequence,alive;
static s32 ramp_start[4][8];
static u32 ramp_primed[4];
#ifdef __mcoldfire__
/* The block's D / 2 in Q31 (mantissa << shift), once per interrupt for the
 * four buses; a silent track for padding. */
static s32 wide[8][32];
static const s32 silent[32];
static void widen(const struct block *b)
{
    u32 t,i,e;const short *d;s32 *w;
    for(t=0;t<8;t++)if(b->mask&(1u<<t))
        for(d=b->d[t],w=wide[t],e=b->shift[t],i=0;i<32;i++)w[i]=(s32)d[i]<<e;
}
#endif
/* Counters for test pages: interrupts, blocks mixed with D, reads caught
 * mid-write, blocks with no matching D, a block left from before this boot
 * (not mixed), blocks that needed the second read, the tracks with D, a block
 * mixed again with the same L, and MAIN's cost (permille: reading, mixing). */
struct st_stereo_counters {u32 interrupts,received,torn,dropped,stale,late,mask,again,read,mix;};
struct st_stereo_counters st_stereo_main_counters;
/* SEQ_END first, then the block, then SEQ: equal means one whole block (the
 * DMA writes SEQ first and SEQ_END last). */
static u32 read_block(struct block *b)
{
    u32 s=WIN(ST_STEREO_SEQ_END),m,t,i,w,x,y;
    m=WIN(ST_STEREO_MASK)&0xffu;x=WIN(ST_STEREO_SHIFT);y=WIN(ST_STEREO_SHIFT+4);
    b->print=WIN(ST_STEREO_PRINT);
    for(t=0;t<8;t++){
        b->shift[t]=(unsigned char)((t<4?x:y)>>(24-8*(t&3)));
        if(!(m&(1u<<t)))continue;
        for(i=0;i<32;i+=2){
            w=WIN(ST_STEREO_WINDOW+64*t+2*i);
            b->d[t][i]=(short)(w>>16);b->d[t][i+1]=(short)w;
        }
    }
    if(WIN(ST_STEREO_SEQ)!=s){st_stereo_main_counters.torn++;return 0;}
    b->mask=m;b->sequence=s;return 1;
}
/* MAIN profile for test pages: timer ticks spent here, as permille of the
 * interrupt period, per 1024 interrupts. */
#ifndef MAIN_CLOCK
#define MAIN_CLOCK (*(volatile u32 *)0xfc07000cu)
#endif
static u32 period,spent_read,spent_mix,last_tick,profiled;
static void spend(u32 *where,u32 since){*where+=MAIN_CLOCK-since;}
static void receive(void)
{
    u32 a=MAIN_CLOCK,unit;
    if(last_tick)period+=a-last_tick;last_tick=a;
    if(++profiled==1024){
        unit=period/1000u;
        st_stereo_main_counters.read=unit?spent_read/unit:0;
        st_stereo_main_counters.mix=unit?spent_mix/unit:0;
        period=spent_read=spent_mix=profiled=0;
    }
    st_stereo_main_counters.interrupts++;
    resolved=0;use=0;have=0;
    tagged=WIN(ST_STEREO_TAG)==ST_STEREO_MAGIC;
    if(tagged)have=read_block(&slot[0]);
    spend(&spent_read,a);
}
/* Once per interrupt, at the first mix event: the D of the L being mixed. */
static u32 current(void)
{
    u32 print,a,k,got=0;
    if(resolved)return use?use->mask:0;
    resolved=1;
    if(!tagged)return 0;
    /* The engine's empty block, still current: no stereo track. */
    if(have && !slot[0].mask && WIN(ST_STEREO_SEQ)==slot[0].sequence){st_stereo_main_counters.mask=0;return 0;}
    a=MAIN_CLOCK;
    print=st_stereo_print(TRACK_PCM);
    if(have && WIN(ST_STEREO_SEQ)==slot[0].sequence){
        if(slot[0].print==print)use=&slot[0];
    }else{
        /* AUDIO wrote since ev_render_in, or that read was torn: read again.
         * Its DMA takes some 15 us, so a torn read is retried a few times. */
        for(k=0;k<8 && !(got=read_block(&slot[1]));k++){}
        if(got && slot[1].print==print){use=&slot[1];st_stereo_main_counters.late++;}
        else if(have && slot[0].print==print)use=&slot[0];
    }
    spend(&spent_read,a);
    if(!use){st_stereo_main_counters.dropped++;return 0;}
    /* A block MAIN has mixed before: AUDIO had no new block, MAIN mixes the
     * same L again, and its D is right again. Only a block from before this
     * boot is never used (the window is not cleared at a restart). */
    if(use->sequence==boot_sequence && !alive){st_stereo_main_counters.stale++;use=0;return 0;}
    alive=1;
    if(use->sequence==last_used)st_stereo_main_counters.again++;
    last_used=use->sequence;
    if(use->mask)st_stereo_main_counters.received++;
    st_stereo_main_counters.mask=use->mask;
#ifdef __mcoldfire__
    widen(use);
#endif
    return use->mask;
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
{
    const s32 *h0=h[0],*h1=h[1],*h2=h[2],*h3=h[3];
    s32 g0=g[0],g1=g[1],g2=g[2],g3=g[3],x;u32 n=32;
    __asm__ volatile(
        "1:\n\tmove.l (%[b]),%[x]\n\tmove.l %[x],%%acc0\n\t"
        "move.l (%[h0])+,%[x]\n\tmsac.l %[g0],%[x],%%acc0\n\tmsac.l %[g0],%[x],%%acc0\n\t"
        "move.l (%[h1])+,%[x]\n\tmsac.l %[g1],%[x],%%acc0\n\tmsac.l %[g1],%[x],%%acc0\n\t"
        "move.l (%[h2])+,%[x]\n\tmsac.l %[g2],%[x],%%acc0\n\tmsac.l %[g2],%[x],%%acc0\n\t"
        "move.l (%[h3])+,%[x]\n\tmsac.l %[g3],%[x],%%acc0\n\tmsac.l %[g3],%[x],%%acc0\n\t"
        "add.l (%[s]),%[g0]\n\tadd.l 4(%[s]),%[g1]\n\tadd.l 8(%[s]),%[g2]\n\tadd.l 12(%[s]),%[g3]\n\t"
        "movclr.l %%acc0,%[x]\n\tmove.l %[x],(%[b])\n\taddq.l #8,%[b]\n\tsubq.l #1,%[n]\n\tbne.s 1b"
        :[b]"+a"(bus),[h0]"+a"(h0),[h1]"+a"(h1),[h2]"+a"(h2),[h3]"+a"(h3),
         [g0]"+d"(g0),[g1]"+d"(g1),[g2]"+d"(g2),[g3]"+d"(g3),[x]"=&d"(x),[n]"+d"(n)
        :[s]"a"(step):"cc","memory");
}
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
static void add(u32 k,s32 *bus,const volatile s32 *gain)
{
    u32 t,n=0,entered=0,m=current(),a=MAIN_CLOCK;s32 g[4],step[4],start,end;const s32 *h[4];
    struct emac_state saved;
    for(t=0;t<8;t++){
        end=gain[t];start=ramp_primed[k]?ramp_start[k][t]:end;ramp_start[k][t]=end;
        if(!(m&(1u<<t)) || (!start && !end))continue;
        h[n]=wide[t];g[n]=start;step[n]=(s32)((u32)end-(u32)start)>>5;
        if(++n==4){
            if(!entered){emac_enter(&saved);entered=1;}
            mix4(bus+1,h,g,step);n=0;
        }
    }
    if(n){
        for(;n<4;n++){h[n]=silent;g[n]=0;step[n]=0;}
        if(!entered){emac_enter(&saved);entered=1;}
        mix4(bus+1,h,g,step);
    }
    if(entered)emac_leave(&saved);
    ramp_primed[k]=1;
    spend(&spent_mix,a);
}
#else
/* The host build (tests/test_st_stereo.c): the same sums in integer C. */
static void add(u32 k,s32 *bus,const volatile s32 *gain)
{
    u32 t,i,m=current(),a=MAIN_CLOCK;s32 g,end,step,p,x,r,*b;int e;const short *d;
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
    spend(&spent_mix,a);
}
#endif
static void delay_in(void){add(0,SEND_BUS,DELAY_GAIN);}
static void reverb_in(void){add(1,SEND_BUS,REVERB_GAIN);}
static void main_mix(void){add(2,MAIN_BUS,MAIN_GAIN);add(3,DIRECT_BUS,DIRECT_GAIN);}
static int initialize(const struct st_smz_api *api)
{
    (void)api;
    boot_sequence=WIN(ST_STEREO_SEQ);   /* what a restart left in the window */
    return 0;
}
static const struct st_smz_subscription subscriptions[] __attribute__((aligned(4)))={
    {ST_EV_RENDER_IN,receive},
    {ST_EV_DELAY_IN,delay_in},
    {ST_EV_REVERB_IN,reverb_in},
    {ST_EV_MAIN_MIX,main_mix}};
const struct st_smz_module st_stereo_module={"STEREO",initialize,subscriptions,4};
