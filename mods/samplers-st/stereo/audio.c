/* SPDX-License-Identifier: GPL-2.0-or-later
 * Stereo tracks, AUDIO side (core 1.8 chain end, <syntakt/stereo.h>).
 * Written in the core's shape so that it can move into mods/core-st: any
 * machine may claim a stereo track; nothing here is specific to the samplers.
 *
 * A machine's render calls st_stereo_claim(t) and writes R where it points.
 * At the chain end (after AMP, before the track DMA) the engine:
 *  1. exchanges the chain's control state as the L pass left it with the
 *     state from before the L pass, kept from the previous chain end (only
 *     the chain writes these words, docs/MACHINE-REFERENCE.md);
 *  2. swaps in R's own filter memory and runs the native chain on the eight
 *     R buffers: drive and base-width (all tracks, in pairs), then the
 *     multimode and the AMP envelope of the claimed tracks;
 *  3. swaps L's filter memory back and copies the L pass's control state
 *     back, so mono tracks and L are exactly what the stock chain made; that
 *     copy stays as the next block's state from before its L pass;
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
static u32 claimed,installed,primed,seq,blocks,calls,quiet;
/* before: the control state from before this block's L pass (valid when
 * primed); staging: where send() builds the window block. */
#ifndef ST_STEREO_STAGE
#define ST_STEREO_STAGE ST_STEREO_SNAP_B
#endif
static u32 *const before=ST_STEREO_SNAP_A,*const staging=ST_STEREO_STAGE;
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
/* Word copies and exchanges, n a multiple of 4 and not 0 (every span above
 * is; the loops stop only at the end). On
 * the unit four words at a time with MOVEM: these spans are most of the
 * engine's fixed cost per block. */
#ifdef __mcoldfire__
static void copy_words(u32 *to,const u32 *from,u32 n)
{
    u32 *end=to+n;
    __asm__ volatile("1:\n\tmovem.l (%1),%%d0-%%d3\n\tmovem.l %%d0-%%d3,(%0)\n\t"
                     "lea 16(%0),%0\n\tlea 16(%1),%1\n\tcmpa.l %2,%0\n\tbne.s 1b"
                     :"+a"(to),"+a"(from):"a"(end):"d0","d1","d2","d3","cc","memory");
}
static void swap_words(u32 *a,u32 *b,u32 n)
{
    u32 *end=a+n;
    __asm__ volatile("1:\n\tmovem.l (%0),%%d0-%%d3\n\tmovem.l (%1),%%d4-%%d7\n\t"
                     "movem.l %%d0-%%d3,(%1)\n\tmovem.l %%d4-%%d7,(%0)\n\t"
                     "lea 16(%0),%0\n\tlea 16(%1),%1\n\tcmpa.l %2,%0\n\tbne.s 1b"
                     :"+a"(a),"+a"(b):"a"(end):"d0","d1","d2","d3","d4","d5","d6","d7","cc","memory");
}
#else
static void copy_words(u32 *to,const u32 *from,u32 n){while(n--)*to++=*from++;}
static void swap_words(u32 *a,u32 *b,u32 n){u32 t;while(n--){t=*a;*a++=*b;*b++=t;}}
#endif
#ifndef CHAIN_WORDS
#define CHAIN_WORDS(address) ((u32 *)(address))
#endif
/* The live control state <-> buffer (swap), or buffer <- live, live <- buffer. */
enum {SWAP,SAVE,LOAD};
static void control_state(u32 *buffer,u32 op)
{
    u32 i,n;
    for(i=0;i<5;i++){
        n=control[i][1];
        if(op==SWAP)swap_words(CHAIN_WORDS(control[i][0]),buffer,n);
        else if(op==SAVE)copy_words(buffer,CHAIN_WORDS(control[i][0]),n);
        else copy_words(CHAIN_WORDS(control[i][0]),buffer,n);
        buffer+=n;
    }
}
static void swap_signal(void)
{
    u32 i,*r=ST_STEREO_SIGNAL;
    for(i=0;i<3;i++){swap_words(CHAIN_WORDS(signal[i][0]),r,signal[i][1]);r+=signal[i][1];}
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
 * is bounded to +-32767 (MAIN saturates its product): the shift is the
 * smallest with |x| >> shift <= 32767 (x < 0 counts as ~x), so only -32768
 * needs bounding. A track whose D is under ST_STEREO_QUIET (-96 dBFS) all
 * along leaves the mask. The fingerprint of the L buffers (what the track DMA
 * sends) lets MAIN tell which block of L it is mixing (main.c).
 * The block is built in AUDIO SRAM (the snapshot buffer the next chain end
 * overwrites first), then copied to the window in ascending order: the
 * sequence first and again last. (A DMA on channel 46 did not reach the
 * window on the unit, and the copy is the smaller part of the cost.) */
static s32 mantissa(s32 x,u32 e)
{x>>=e;return x<-32767?-32767:x;}
#ifndef SEND_BLOCK
static void send_block(const u32 *from,u32 mask)
{
    u32 i,t;volatile u32 *w=&WIN(ST_STEREO_SEQ);
    for(i=(ST_STEREO_WINDOW-ST_STEREO_SEQ)/4;i;i--)*w++=*from++;
    for(t=0;t<8;t++,from+=16){
        if(!(mask&(1u<<t)))continue;       /* only the tracks MAIN will read */
        for(w=&WIN(ST_STEREO_WINDOW+64*t),i=0;i<16;i++)w[i]=from[i];
    }
    WIN(ST_STEREO_SEQ_END)=*from;          /* from is at SEQ_END now */
}
#define SEND_BLOCK(from,mask) send_block(from,mask)
#endif
/* The smallest e with big >> e <= 32767, big < 2^31: the bit length of
 * big >> 15 (a binary search: Unicorn, which runs this engine in the tests,
 * has no FF1). */
static u32 shift_of(u32 big)
{
    u32 v=big>>15,e;
    if(!v)return 0;
    e=1;if(v>>8){v>>=8;e+=8;}if(v>>4){v>>=4;e+=4;}if(v>>2){v>>=2;e+=2;}if(v>>1)e++;
    return e;
}
#define OUT(stage,offset) (stage)[((offset)-ST_STEREO_SEQ)/4]
static void send(u32 mask,u32 *stage)
{
    u32 t,i,e,big,*o;s32 d[32],*p,x;const s32 *l,*r;u8 shift[8]={0};
    ++seq;
    for(t=0;t<8;t++){
        if(!(mask&(1u<<t)))continue;
        l=TRACKS+32*t;r=ST_STEREO_R(t);big=0;
        for(p=d,i=32;i;i--){x=(*r++>>1)-(*l++>>1);*p++=x;big|=(u32)(x^(x>>31));}
        if(big<ST_STEREO_QUIET/2u){mask&=~(1u<<t);continue;}   /* |D| under -96 dBFS: mono */
        e=shift_of(big);
        shift[t]=(u8)e;
        for(o=&OUT(stage,ST_STEREO_WINDOW+64*t),p=d,i=16;i;i--,p+=2)
            *o++=((u32)mantissa(p[0],e)<<16)|((u32)mantissa(p[1],e)&0xffffu);
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
/* Drive on R with OVERDRIVE off. The native drive (0x4000112c, two tracks
 * a pass) mixes, per track, the dry signal with a ramped dry weight and the
 * clipped signal with a ramped wet weight: out = movclr(dry x + wet y), dry
 * and wet ramping by (target - current) >> 5 a frame. A track whose row
 * word 0x28 is <= 0 has targets dry 0x3fffffff and wet 0; when its current
 * wet weight (0x800083b0 + 4t) is 0 too, every wet product is 0 and the
 * output is movclr(dry x): the same MAC, with the same MACSR, as the native
 * one. The R pass only needs R's output (it reloads the control state
 * after), and only of the claimed tracks (the others are zero and not
 * sent), so when every claimed track is off this replaces the native call,
 * about a thousand instructions a track, by one MAC a frame.
 * tests/test_st_stereo_natives_emu.py compares the two in analog-emu. */
#define DRIVE_DRY ((const volatile s32 *)0x800083d0u)
#define DRIVE_WET ((const volatile s32 *)0x800083b0u)
#ifdef __mcoldfire__
static int drive_off(u32 mask)
{
    u32 t;
    for(t=0;t<8;t++)if(mask&(1u<<t))
        if(*(const volatile short *)(ROWS+142*t+0x50)>0 || DRIVE_WET[t])return 0;
    return 1;
}
static void drive_dry(s32 *x,s32 dry,s32 step)
{
    s32 *end=x+32;
    __asm__ volatile("movclr.l %%acc0,%%d0\n"
                     "1:\n\tmove.l (%0),%%d0\n\tmac.l %1,%%d0,%%acc0\n\tadd.l %2,%1\n\t"
                     "movclr.l %%acc0,%%d0\n\tmove.l %%d0,(%0)+\n\tcmpa.l %3,%0\n\tbne.s 1b"
                     :"+a"(x),"+d"(dry):"d"(step),"a"(end):"d0","cc","memory");
}
#endif
static void drive_r(u32 mask)
{
#ifdef __mcoldfire__
    u32 t;s32 dry;
    if(drive_off(mask)){
        for(t=0;t<8;t++)if(mask&(1u<<t)){
            dry=DRIVE_DRY[t];
            drive_dry(ST_STEREO_R(t),dry,(s32)(0x3fffffffu-(u32)dry)>>5);
        }
        return;
    }
#endif
    (void)mask;DRIVE(ST_STEREO_R(0),ROWS);
}
/* The multimode on R without its coefficient part. The native multimode
 * (0x400019d6, one track a call) first computes the block's targets from the
 * row (0x80008900..0x8000891c; it also writes the 0x80008720 increments,
 * which its next part rewrites), then from 0x40001ea4 on ramps, filters and
 * mixes. R's row and starting state are L's, so its targets are the ones the
 * L pass left as the track's coefficients (0x80008740 + 32t, kept in `before`
 * for the R pass). multimode_tail builds the native frame (the prologue, the
 * MACSR, t at 48 and the track's type at 52, the only words the first part
 * leaves for the rest) and jumps to 0x40001ea4, the join where every path of
 * the first part ends. About 400 instructions less a track; the same words
 * (tests/test_st_stereo_natives_emu.py runs both in analog-emu). */
#define MULTI_TARGETS ((u32 *)0x80008900u)
#define MULTI_COEFFICIENTS(t) ((40u+12u)+8u*(t))     /* word offset in a control-state copy */
#ifdef __mcoldfire__
void st_multimode_tail(void *row,void *buffer,u32 x,u32 t,u32 trig,s32 word);
__asm__(".text\n\t.even\n"
        "st_multimode_tail:\n\t"
        "lea -100(%sp),%sp\n\t"
        "movem.l %d2-%d7/%a2-%a6,(%sp)\n\t"
        "move.l 108(%sp),68(%sp)\n\t"
        "move.l #160,%macsr\n\t"
        "move.l 116(%sp),%d0\n\t"
        "move.l %d0,48(%sp)\n\t"
        "lea 0x80008560,%a0\n\t"
        "move.l (%a0,%d0.l*4),%d1\n\t"
        "move.l %d1,52(%sp)\n\t"
        "jmp 0x40001ea4\n");
#define MULTI_R(t,...) (copy_words(MULTI_TARGETS,before+MULTI_COEFFICIENTS(t),8),st_multimode_tail(__VA_ARGS__))
#else
#define MULTI_R(t,...) MULTI(__VA_ARGS__)
#endif
void st_stereo_chain_end(void)
{
    u32 mask=claimed,t,i,trig,held,gate,macsr,a,b;s32 *r;
    const volatile u8 *frame=(const volatile u8 *)0x80008000u;
    a=CLOCK;if(last_tick)profile[P_PERIOD]+=a-last_tick;last_tick=a;
    if(++profiled==1024){profiled=0;publish_profile();}
    claimed=0;calls++;
    /* No stereo track: one block with an empty mask tells MAIN, then the
     * window is left alone until a track is stereo again. */
    if(!mask){primed=0;if(!quiet){send(0,staging);quiet=1;}return;}
    quiet=0;
    blocks++;
    /* live <- the state from before the L pass, before <- the L pass's; or,
     * with no previous block of R, R starts from the L pass's state too */
    control_state(before,primed?SWAP:SAVE);
    swap_signal();
    for(t=0;t<8;t++)if(!(mask&(1u<<t)))for(r=ST_STEREO_R(t),i=0;i<32;i++)r[i]=0;
    /* The IRQ's own arguments (40000a04..40000a24): trig and held masks. */
    trig=~(u32)*(const volatile u16 *)(frame+0x182)&frame[0x180];
    held=(u32)frame[0x181]|*(const volatile u16 *)(frame+0x182);
    gate=frame[0x1a8];
    macsr=get_macsr();set_macsr(32);
    b=CLOCK;profile[P_STATE]+=b-a;a=b;
    drive_r(mask);
    WIDTH(ST_STEREO_R(0),ROWS,trig);
    b=CLOCK;profile[P_DRIVE]+=b-a;a=b;
    for(t=0;t<8;t++)if(mask&(1u<<t))
        MULTI_R(t,ROWS+142*t+0x52,ST_STEREO_R(t),*(const volatile u32 *)(frame+0x188+4*t),t,trig,
              *(const volatile short *)(ROWS+142*t+0x82));
    b=CLOCK;profile[P_MULTI]+=b-a;a=b;
    for(t=0;t<8;t++)if(mask&(1u<<t))
        ENVELOPE(ST_STEREO_R(t),(void *)(0x800049e0u+12*t),ROWS+142*t+0x6a,
                 (trig>>t)&1,(held>>t)&1,t,(gate>>t)&1);
    set_macsr(macsr);
    b=CLOCK;profile[P_AMP]+=b-a;a=b;
    swap_signal();
    control_state(before,LOAD);                  /* and before keeps it for the next block */
    primed=1;
    send(mask,staging);
    b=CLOCK;profile[P_SEND]+=b-a;
}
