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
static s32 subtract(s32 y,s32 x)
{
    u32 a=(u32)y,r=a-(u32)x;
    if(((a^(u32)x)&(a^r))&0x80000000u)r=y<0?0x80000000u:0x7fffffffu;
    return (s32)r;
}
/* bus[2i+1] -= g_i x D_i (Q31, as the kernels' MSAC), g ramping over the block.
 * A track whose gain is zero over the whole block adds nothing. */
static void add(u32 k,s32 *bus,const volatile s32 *gain)
{
    u32 t,i,m=current(),a=MAIN_CLOCK;s32 g,end,step,p;int e;const short *d;
    for(t=0;t<8;t++){
        end=gain[t];g=ramp_primed[k]?ramp_start[k][t]:end;ramp_start[k][t]=end;
        if(!(m&(1u<<t)) || (!g && !end))continue;
        /* g x D / 2^31 = (g >> 16) x m x 2^(shift - 14); |(g>>16) x m| < 2^30,
         * so only shifts 15 and 16 can overflow, and then only near +-2. */
        step=(end-g)>>5;d=use->d[t];e=(int)use->shift[t]-14;
        for(i=0;i<32;i++,g+=step){
            p=(s32)(short)(g>>16)*d[i];
            if(e<=0)p>>=-e;
            else if(p>(0x7fffffff>>e))p=0x7fffffff;
            else if(p<-(0x7fffffff>>e))p=-0x7fffffff;
            else p<<=e;
            bus[2*i+1]=subtract(bus[2*i+1],p);
        }
    }
    ramp_primed[k]=1;
    spend(&spent_mix,a);
}
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
