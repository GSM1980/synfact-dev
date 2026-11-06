/* SPDX-License-Identifier: GPL-2.0-or-later
 * The chorus's per-block path, all of this file's code, which mod.json links
 * between hot_begin.s (chorus_hot_c) and hot_end.s (chorus_hot_c_end): the
 * sends, the mono mix, the settings, the DSP and the returns. chorus_place
 * copies this span to MAIN SRAM after the hot state (<syntakt/sram.h>): the
 * unit then fetches none of it from DDR. Calls leaving the span are
 * absolute (the kernels through chorus_k, the rare full settings decode);
 * tests/test_st_chorus_emu.py checks the span keeps no PC-relative reference
 * outside itself. MAIN audio IRQ: core 1.2's ev_track_send (once a block),
 * ev_delay_in, ev_reverb_in and ev_main_mix (<syntakt/mix.h>).
 */
/* Size over speed for this file alone: on the unit the span's place in SRAM
 * (no DDR fetch) is worth more than -O3's unrolled and inlined copies, and
 * the span must fit after the state. Same arithmetic. */
#pragma GCC optimize("Os","no-tree-loop-distribute-patterns")
#include <syntakt/mix.h>
#include "chorus.h"
typedef unsigned char u8;
#define target (CH->target)
#define current (CH->current)
#define block_in (chorus_buf.input+CH24_HISTORY)
#define wet (chorus_buf.wet)
#define return_gain (CH->return_gain)
#define desired (CH->desired)
#define idle (CH->idle)
#define quiet (CH->quiet)
#define target_mask (CH->target_mask)
#define current_mask (CH->current_mask)
extern s32 chorus_ring[257];
/* Every function takes chorus_ch once into a register: a 6-byte reload of
 * the global before each state access was most of this span's size. */
#undef CH
#define CH ch
#define HOT_STATE struct chorus_hot *const ch=chorus_ch
extern void chorus_decode(const u16 *,u16 *);


static s32 gain(u32 value) {return (s32)(2u*value*value);}
/* ev_track_send, once a block: each track's send is its dormant Digitone
 * chorus send (mixer record +106, AMP parameter 72 on the CHO control),
 * scaled like V56's by the track's level x pan; mono, the mean of the two
 * sides. Live mixer accumulators: integer products only, no EMAC. */
void chorus_hot_sends(const struct st_track_sends *t)
{
    HOT_STATE;
    u32 track,mask=target_mask,dirty=0;
#pragma GCC unroll 1
    for(track=0;track<12;track++) {
        u32 raw=*(const u16 *)(t->track[track].mix+106);
        s32 g=0,send;
        if(raw>0x7f00) raw=0x7f00;
        if(raw && !(t->muted>>track&1u)) {
            send=gain(raw)>>16;
            g=(t->track[track].left>>16)*send+(t->track[track].right>>16)*send;
        }
        if(g==target[track]) continue;
        target[track]=g;dirty=1;
        if(g) mask|=1u<<track; else mask&=~(1u<<track);
    }
    if(dirty) target_mask=mask;
}
/* The Kit's settings, decoded again (chorus_decode, params.c) only when the
 * record or one of its eight words changed, compared as four longs. */
static u16 decoded[7] DATA={0};
static const u16 *cached_fx DATA=0;
static u32 cached_key[4] DATA={0,0,0,0};
static const u16 *values(const u16 *fx)
{
    const volatile u16 *s=fx;
    u32 a,b,c,d;
    if (!s) {chorus_decode(0,decoded);cached_fx=0;return decoded;}
    a=((u32)s[0]<<16)|s[0x1a];b=((u32)s[0x24]<<16)|s[0x27];
    c=((u32)s[0x29]<<16)|s[0x2a];d=((u32)s[0x37]<<16)|s[0x46];
    if (fx==cached_fx && a==cached_key[0] && b==cached_key[1] && c==cached_key[2] && d==cached_key[3])
        return decoded;
    chorus_decode(fx,decoded);
    cached_fx=fx;cached_key[0]=a;cached_key[1]=b;cached_key[2]=c;cached_key[3]=d;
    return decoded;
}
static void add_return(s32 *bus,u32 which,u32 subtract)
{
    HOT_STATE;
    s32 g=return_gain[which],step=(desired[which]-g)>>5;
    if (idle || (!g && !desired[which])) return;
    if(!step && g==desired[which]) chorus_k.return_stable(bus,wet,subtract?-g:g);
    else chorus_ramp_return(bus,wet,g,step,subtract);   /* dsp.c: rare */
    return_gain[which]=desired[which];
}
void chorus_hot_before_reverb(void) {add_return(ST_FX_SEND_BUS,1,0);}
void chorus_hot_master_return(void) {add_return(ST_FX_TRACK_BUS,2,1);}

static inline u32 triangle(u32 p) {
    u32 t=p<<1;return (p&0x80000000u?~t:t)>>1;
}
static inline s32 lookup(s32 x) {
    u32 u=(u32)x,index=u>>26;
    s32 a,b;
    if (index>30) index=30;
    a=chorus24_lut[index];b=chorus24_lut[index+1];
    return a+ch_mul((s32)((u&0x03ffffffu)<<5),b-a);
}
/* V56 advanced its modulation four times a block (6 kHz). Once a block is
 * enough for a triangle under 10 Hz: the delay kernel moves each tap
 * linearly between the block's two nodes. The pole that ran four steps of c
 * runs one of c^4, its input weighted 1+c+c^2+c^3 (3 + KFRAC), so its gain
 * and its settling time match V56's. */
#define C4 1588359364                  /* (1991519628/2^31)^4 in Q31 */
#define KFRAC 1256185306               /* 1+c+c^2+c^3 - 3 in Q31 */
static void modulation(const u16 *p)
{
    HOT_STATE;
    u32 side,phase=CH->phase;
    s32 width=(s32)((u32)p[3]<<16),depth=(s32)((u32)p[0]<<16);
    s32 next=ch_mul(0x05000000,depth)+ch_mul(0x7b000000,CH->depth_raw);
    s32 amplitude=ch_mul(75544827,next);
    u32 increment=((25054u+(u32)ch_mul(252329,(s32)((u32)p[1]<<16)))<<3)*4u;
    CH->width=ch_mul(0x05000000,width)+ch_mul(0x7b000000,CH->width);
#pragma GCC unroll 1
    for (side=0;side<2;side++) {
        u32 q=phase+increment+(side?(u32)CH->width:0u);
        s32 t=(s32)triangle(q),y=CH->pole_state[side];
        s32 x=ch_add_sat(ch_mul(amplitude,CH->previous_triangle[side]),ch_mul(amplitude,t));
        s32 k=ch_add_sat(ch_add_sat(ch_add_sat(x,x),x),ch_mul(KFRAC,x));
        y=ch_add_sat(k,ch_mul(C4,y));
        CH->position[side][0]=CH->previous_delay[side];
        CH->position[side][1]=CH->previous_delay[side]=(lookup(y)>>1)-(3<<22);
        CH->previous_triangle[side]=t;CH->pole_state[side]=y;
    }
    CH->phase=phase+increment;CH->depth_raw=depth;CH->amplitude=amplitude;
}
/* out: 32 stereo frames; in: 32 mono samples, chorus_buf.input's block or a copy. */
static void render(s32 *out,const s32 *in,const u16 *p)
{
    HOT_STATE;
    u32 i,w;
    s32 *now=chorus_buf.input+CH24_HISTORY;
    s32 *up=chorus_buf.up+CH24_UP_HISTORY*2;
    s32 low[16],discarded;                   /* the decimator's partial sums */
    if(!CH->initialized) chorus_dsp_reset();
    __asm__ volatile("move.l #0xa0,%%macsr\n\tmovclr.l %%acc0,%0\n\tmovclr.l %%acc1,%0":"=d"(discarded)::"memory");
    if(in!=now) for(i=0;i<32;i++) now[i]=in[i];
    w=CH->write_pos;
    chorus_k.decimate(now,low,chorus24_decimator_coeffs,&chorus_ring[w]);
    chorus_k.hpf(&chorus_ring[w],CH->hp,chorus24_hpf24[p[2]>0x7f00?127:p[2]>>8]);
    if(!w) chorus_ring[256]=chorus_ring[0];
    modulation(p);
    chorus_k.delay(up,chorus_ring,&CH->position[0][0],w);
    CH->write_pos=(w+16)&255;
    chorus_k.upsample(up,out,chorus24_up_coeffs);
    chorus24_history(chorus_buf.input,now+32-CH24_HISTORY,chorus_buf.up,up+32-CH24_UP_HISTORY*2);
}
/* Lowest live track (ColdFire FF1 via clz); bits must be nonzero. */
static inline u32 lowest(u32 bits) {return 31u-(u32)__builtin_clz(bits&-bits);}
void chorus_hot_before_delay(void)
{
    HOT_STATE;
    u32 track,i,n=0,ramp=0,live=0,audible=0,mask=target_mask|current_mask,bits;
    const s32 *src[12];
    s32 gains[12],steps[12];
    s32 *delay=ST_FX_SEND_BUS;
    const u16 *v;
    if (!mask && idle) return;
    if(idle) {
        /* A raised send is not audio activity: sleep on until a live track
         * with a send has sound. Gains settle without waking the kernels. */
        for(bits=mask;bits && !live;bits&=bits-1u) {
            const s32 *pcm=ST_TRACK_PCM(lowest(bits));
            for (i=0;i<32;i+=4) if (pcm[i]|pcm[i+1]|pcm[i+2]|pcm[i+3]) {live=1;break;}
        }
        if(!live) {
            for(track=0;track<12;track++) current[track]=target[track];
            current_mask=target_mask;return;
        }
    }
    /* Every track with a send, one pass: gains ramp over the block. */
    for(bits=mask;bits;bits&=bits-1u) {
        track=lowest(bits);
        src[n]=ST_TRACK_PCM(track);
        gains[n]=current[track];
        steps[n]=(target[track]-current[track])>>5;
        if(steps[n]) ramp=1;
        current[track]=target[track];n++;
    }
    current_mask=target_mask;
    if(n) chorus_k.mix(block_in,src,gains,steps,n,ramp);
    else for(i=0;i<32;i++) block_in[i]=0;
    live=0;
    for (i=0;i<32;i++) if (block_in[i]) {live=1;break;}
    if (!live && idle) return;
    v=values(*(const u16 *volatile *)0x800030bcu);
    for (i=0;i<3;i++) desired[i]=gain(v[i+4]);
    idle=0;
    render(wet,block_in,v);
    /* Native fixed-point filters settle into a tiny limit cycle (~388 Q31
     * units in the silence regression). Four units could never sleep.
     * 512 is -132 dBFS; any nonzero input still keeps the kernel awake.
     */
    if (!live) for (i=0;i<64;i++) if (wet[i]>512 || wet[i]<-512) {audible=1;break;}
    if (live || audible) quiet=0;
    else if (++quiet>=64) {
        idle=1;
        for (i=0;i<64;i++) wet[i]=0;
        for (i=0;i<3;i++) return_gain[i]=desired[i];
        return;
    }
    add_return(delay,0,0);
}

