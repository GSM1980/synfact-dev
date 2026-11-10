/* SPDX-License-Identifier: GPL-2.0-or-later
 * Stereo tracks, AUDIO side (core 1.8 chain end, <syntakt/stereo.h>).
 * Written in the core's shape so that it can move into mods/core-st: any
 * machine may claim a stereo track; nothing here is specific to the samplers.
 *
 * A machine's render calls st_stereo_claim(t) and writes R where it points.
 * At the chain end (after AMP, before the track DMA) the engine:
 *  1. saves the chain's control state as the L pass left it, and puts back
 *     the state from before the L pass (saved at the previous chain end:
 *     only the chain writes these words, docs/MACHINE-REFERENCE.md);
 *  2. swaps in R's own filter memory and runs the native chain on the eight
 *     R buffers: drive and base-width (all tracks, in pairs), then the
 *     multimode and the AMP envelope of the claimed tracks;
 *  3. swaps L's filter memory back and restores the L pass's control state,
 *     so mono tracks and L are exactly what the stock chain made;
 *  4. sends D = R - L of the claimed tracks through the shared window.
 * The L buffers are not changed: MAIN mixes them as before and adds gR x D
 * to the right buses (stereo-main.c).
 */
#include <syntakt/stereo.h>
typedef unsigned int u32;typedef int s32;typedef unsigned short u16;typedef unsigned char u8;
#define DRIVE ((void (*)(void *,void *))0x4000112cu)
#define WIDTH ((void (*)(void *,void *,u32))0x40002320u)
#define MULTI ((void (*)(void *,void *,u32,u32,u32,s32))0x400019d6u)
#define ENVELOPE ((void (*)(void *,void *,void *,u32,u32,u32,u32))0x40001476u)
#ifndef TRACKS
#define TRACKS ((s32 *)0x80003840u)
#define ROWS ((u8 *)0x80003c40u)
#define WIN(offset) (*(volatile u32 *)(offset))
#endif
/* The chain's control state (word spans): drive, the multimode's and the
 * base-width's flags and types, the multimode's coefficients and mix weights,
 * its 800088e0 words, and the AMP envelopes. 172 words. */
static const u32 control[5][2]={{0x800083b0u,40},{0x80008550u,12},{0x80008740u,88},
    {0x800088e0u,8},{0x800049e0u,24}};
/* Its signal memory, one copy per channel: the multimode's 80008580 and
 * 80008920 words, the base-width's 800088a0. 40 words. */
static const u32 signal[3][2]={{0x80008580u,8},{0x800088a0u,16},{0x80008920u,16}};
static u32 claimed,installed,primed,seq,blocks,calls;
static u32 *before=ST_STEREO_SNAP_A,*after=ST_STEREO_SNAP_B;
void st_stereo_chain_end(void);
int *st_stereo_claim(u32 track)
{
    if(track>=8)return 0;
    if(!installed){
        if(ST_CHAIN_DIRECTORY[0]!=ST_CHAIN_MAGIC)return 0;
        ST_CHAIN_DIRECTORY[1]=(u32)st_stereo_chain_end;installed=1;
    }
    claimed|=1u<<track;
    return ST_STEREO_R(track);
}
static void save(u32 *to)
{
    u32 i,j;const u32 *p;
    for(i=0;i<5;i++)for(p=(const u32 *)control[i][0],j=0;j<control[i][1];j++)*to++=*p++;
}
static void load(const u32 *from)
{
    u32 i,j,*p;
    for(i=0;i<5;i++)for(p=(u32 *)control[i][0],j=0;j<control[i][1];j++)*p++=*from++;
}
static void swap_signal(void)
{
    u32 i,j,t,*p,*r=ST_STEREO_SIGNAL;
    for(i=0;i<3;i++)for(p=(u32 *)signal[i][0],j=0;j<signal[i][1];j++){t=*p;*p++=*r;*r++=t;}
}
#ifdef __mcoldfire__
static u32 get_macsr(void){u32 v;__asm__ volatile("move.l %%macsr,%0":"=d"(v));return v;}
static void set_macsr(u32 v){__asm__ volatile("move.l %0,%%macsr"::"d"(v));}
#else
static u32 get_macsr(void){return 0;}
static void set_macsr(u32 v){(void)v;}
#endif
/* D = R - L as 16-bit mantissas, one shift per track: D = m << (shift + 1).
 * (R>>1)-(L>>1) cannot overflow, so a shift of 16 always fits; the mantissa
 * is bounded to +-32767 (MAIN saturates its product). A track whose D is all
 * zero leaves the mask. The fingerprint of the L buffers (what the track DMA
 * sends) lets MAIN tell which block of L it is mixing (main.c).
 * The block is built in AUDIO SRAM (the snapshot buffer the next chain end
 * overwrites first), then copied to the window in ascending order: the
 * sequence first and again last. (A DMA on channel 46 did not reach the
 * window on the unit, and the copy is the smaller part of the cost.) */
static s32 mantissa(s32 x,u32 e)
{x>>=e;return x>32767?32767:x<-32767?-32767:x;}
#ifndef SEND_BLOCK
static void send_block(const u32 *from,u32 mask)
{
    u32 i,t;
    for(i=0;i<(ST_STEREO_WINDOW-ST_STEREO_SEQ)/4;i++)WIN(ST_STEREO_SEQ+4*i)=from[i];
    for(t=0;t<8;t++)if(mask&(1u<<t))       /* only the tracks MAIN will read */
        for(i=0;i<16;i++)WIN(ST_STEREO_WINDOW+64*t+4*i)=from[(ST_STEREO_WINDOW-ST_STEREO_SEQ)/4+16*t+i];
    WIN(ST_STEREO_SEQ_END)=from[(ST_STEREO_SEQ_END-ST_STEREO_SEQ)/4];
}
#define SEND_BLOCK(from,mask) send_block(from,mask)
#endif
#define OUT(stage,offset) (stage)[((offset)-ST_STEREO_SEQ)/4]
static void send(u32 mask,u32 *stage)
{
    u32 t,i,e,big;s32 d[32],*l,*r,x;u8 shift[8]={0};
    ++seq;
    for(t=0;t<8;t++){
        if(!(mask&(1u<<t)))continue;
        l=TRACKS+32*t;r=ST_STEREO_R(t);big=0;
        for(i=0;i<32;i++){x=(r[i]>>1)-(l[i]>>1);d[i]=x;big|=(u32)(x<0?~x:x);}
        if(!big){mask&=~(1u<<t);continue;}
        for(e=0;e<16 && (big>>e)>32767u;e++){}
        shift[t]=(u8)e;
        for(i=0;i<32;i+=2)
            OUT(stage,ST_STEREO_WINDOW+64*t+2*i)=((u32)mantissa(d[i],e)<<16)|((u32)mantissa(d[i+1],e)&0xffffu);
    }
    OUT(stage,ST_STEREO_SEQ)=seq;
    OUT(stage,ST_STEREO_MASK)=mask;
    OUT(stage,ST_STEREO_SHIFT)=(u32)shift[0]<<24|(u32)shift[1]<<16|(u32)shift[2]<<8|shift[3];
    OUT(stage,ST_STEREO_SHIFT+4)=(u32)shift[4]<<24|(u32)shift[5]<<16|(u32)shift[6]<<8|shift[7];
    OUT(stage,ST_STEREO_PRINT)=st_stereo_print(TRACKS);
    OUT(stage,ST_STEREO_TAG)=ST_STEREO_MAGIC;
    OUT(stage,ST_STEREO_COUNT)=blocks;OUT(stage,ST_STEREO_COUNT+4)=calls;
    OUT(stage,ST_STEREO_SEQ_END)=seq;
    SEND_BLOCK(stage,mask);
}
/* Profile for test pages: AUDIO timer ticks (DMA timer 0, counting while
 * System info measures), per 1024 chain ends, as permille of the block. */
#ifndef CLOCK
#define CLOCK (*(volatile u32 *)0xfc07000cu)
#endif
enum {P_PERIOD,P_STATE,P_DRIVE,P_MULTI,P_AMP,P_SEND,P_RENDER,P_COUNT};
static u32 profile[P_COUNT],profiled,last_tick;
u32 st_stereo_clock(void){return CLOCK;}
void st_stereo_render_spent(u32 start){profile[P_RENDER]+=CLOCK-start;}
static void publish_profile(void)
{
    u32 k,v[P_COUNT],unit=profile[P_PERIOD]/1000u;
    for(k=1;k<P_COUNT;k++){v[k]=unit?profile[k]/unit:0;if(v[k]>0xffffu)v[k]=0xffffu;}
    WIN(ST_STEREO_PROFILE)=profile[P_PERIOD]>>10;
    WIN(ST_STEREO_PROFILE+4)=v[P_STATE]<<16|v[P_DRIVE];
    WIN(ST_STEREO_PROFILE+8)=v[P_MULTI]<<16|v[P_AMP];
    WIN(ST_STEREO_PROFILE+12)=v[P_SEND]<<16|v[P_RENDER];
    for(k=0;k<P_COUNT;k++)profile[k]=0;
}
void st_stereo_chain_end(void)
{
    u32 mask=claimed,t,i,trig,held,gate,macsr,*swap,a,b;s32 *r;
    const volatile u8 *frame=(const volatile u8 *)0x80008000u;
    a=CLOCK;if(last_tick)profile[P_PERIOD]+=a-last_tick;last_tick=a;
    if(++profiled==1024){profiled=0;publish_profile();}
    claimed=0;calls++;
    if(!mask){primed=0;send(0,after);return;}
    blocks++;
    save(after);
    load(primed?before:after);
    swap_signal();
    for(t=0;t<8;t++)if(!(mask&(1u<<t)))for(r=ST_STEREO_R(t),i=0;i<32;i++)r[i]=0;
    /* The IRQ's own arguments (40000a04..40000a24): trig and held masks. */
    trig=~(u32)*(const volatile u16 *)(frame+0x182)&frame[0x180];
    held=(u32)frame[0x181]|*(const volatile u16 *)(frame+0x182);
    gate=frame[0x1a8];
    macsr=get_macsr();set_macsr(32);
    b=CLOCK;profile[P_STATE]+=b-a;a=b;
    DRIVE(ST_STEREO_R(0),ROWS);
    WIDTH(ST_STEREO_R(0),ROWS,trig);
    b=CLOCK;profile[P_DRIVE]+=b-a;a=b;
    for(t=0;t<8;t++)if(mask&(1u<<t))
        MULTI(ROWS+142*t+0x52,ST_STEREO_R(t),*(const volatile u32 *)(frame+0x188+4*t),t,trig,
              *(const volatile short *)(ROWS+142*t+0x82));
    b=CLOCK;profile[P_MULTI]+=b-a;a=b;
    for(t=0;t<8;t++)if(mask&(1u<<t))
        ENVELOPE(ST_STEREO_R(t),(void *)(0x800049e0u+12*t),ROWS+142*t+0x6a,
                 (trig>>t)&1,(held>>t)&1,t,(gate>>t)&1);
    set_macsr(macsr);
    b=CLOCK;profile[P_AMP]+=b-a;a=b;
    swap_signal();
    load(after);
    swap=before;before=after;after=swap;primed=1;
    send(mask,after);                            /* free until the next save() */
    b=CLOCK;profile[P_SEND]+=b-a;
}
