/* SPDX-License-Identifier: GPL-2.0-or-later
 * Stereo tracks, AUDIO side (<syntakt/stereo.h>). Written in the core's
 * shape so that it can move into mods/core-st: any machine may claim a
 * stereo track; nothing here is specific to the samplers.
 *
 * A machine's render calls st_stereo_claim(t) and writes R where it points.
 * Since core 2.5 the stock chain's multimode loads its routine from the
 * chain directory's word 3 (40000a72), and the engine installs its own entry
 * there, st_stereo_multimode. The stock chain runs drive and base-width on
 * the eight L buffers, then the multimode track by track, then the AMP. The
 * engine runs R inside that loop:
 *  1. at track 0's call, R's drive and base-width for the claimed tracks:
 *     the drive from the drive state from before this block's L drive (kept
 *     by the previous chain end; only the chain writes it), the base-width
 *     with its own code and R's own memory (it has no other state);
 *  2. at each claimed track's call, the native multimode on L, then the
 *     multimode's second part on R from the same starting words (only that
 *     track's control words are put back for it; R's targets are the ones the
 *     L call just computed), then the AMP envelope on R from the envelope
 *     state the AMP stage will find for L; the track's words are left as the
 *     L call made them;
 *  3. at the chain end (after AMP, before the track DMA) it keeps the drive
 *     state for the next block and sends D = R - L of the claimed tracks
 *     through the shared window.
 * Mono tracks and L are exactly what the stock chain made. The L buffers are
 * not changed: MAIN mixes them as before and adds gR x D to the right buses
 * (main.c).
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
#ifndef CHAIN_WORDS
#define CHAIN_WORDS(address) ((u32 *)(address))
#endif
#define FRAME ((const volatile u8 *)CHAIN_WORDS(0x80008000u))
/* The drive's control state (only the chain writes it): 40 words. */
#define DRIVE_STATE CHAIN_WORDS(0x800083b0u)
#define DRIVE_WORDS 40u
/* The tracks claimed for this block (the renders run before the chain), and
 * for the previous one. Named: the multimode entry reads it. */
u32 st_stereo_claimed;
#define claimed st_stereo_claimed
static u32 last,installed,primed,seq,blocks,calls,quiet;
/* drive_before: the drive state from before this block's L drive (valid
 * when primed); staging: where send() builds the window block. */
#ifndef ST_STEREO_STAGE
#define ST_STEREO_STAGE ST_STEREO_SNAP_B
#endif
static u32 *const drive_before=ST_STEREO_SNAP_A,*const staging=ST_STEREO_STAGE;
void st_stereo_chain_end(void);
void st_stereo_multimode(void);
/* The fused multimode (fused_prepare): not tried yet, ready, or off. */
enum {FUSED_UNKNOWN,FUSED_READY,FUSED_OFF};
static u32 fused;
static void fused_prepare(void);
/* The core's chain directory: {magic, chain-end hook, version, multimode}.
 * Version 2 (core 2.5) loads the multimode's routine from word 3. */
int *st_stereo_claim(u32 track)
{
    if(track>=8)return 0;
    if(!installed){
        if(ST_CHAIN_DIRECTORY[0]!=ST_CHAIN_MAGIC || ST_CHAIN_DIRECTORY[2]<ST_CHAIN_MULTIMODE_VERSION ||
           ST_CHAIN_DIRECTORY[3]!=ST_CHAIN_MULTIMODE_NATIVE)return 0;
        if(fused==FUSED_UNKNOWN)fused_prepare();
        ST_CHAIN_DIRECTORY[1]=(u32)st_stereo_chain_end;
        ST_CHAIN_DIRECTORY[3]=(u32)st_stereo_multimode;
        installed=1;
    }
    claimed|=1u<<track;
    return ST_STEREO_R(track);
}
/* Word copies and exchanges, n a multiple of 4 and not 0 (the loops stop
 * only at the end); four words at a time with MOVEM on the unit. */
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
 * overwrites first), then sent to the window in ascending order, the
 * sequence first and again last: on the unit by eDMA channel 46 (send_dma),
 * elsewhere word by word. */
#ifdef __mcoldfire__
/* AUDIO eDMA channel 46 (the stock uses only 47; TCD 0xfc0455c0, the
 * ColdFire field order). A software start runs one minor loop: like the
 * stock's channel 47, the TCD links the channel to itself after each minor
 * loop (CITER/BITER: ELINK, LINKCH 46), so one start moves the whole block,
 * 35 bursts of 16 bytes, ascending. Waits for the previous block (long done)
 * before it reprograms the channel. The track DMA (channel 47) starts right
 * after the chain end and keeps its higher priority. */
#ifndef ST_STEREO_DMA
#define ST_STEREO_DMA 0xfc0455c0u
#define ST_STEREO_SSRT (*(volatile u8 *)0xfc04401eu)
#endif
#define TCD16(offset) (*(volatile u16 *)(ST_STEREO_DMA+(offset)))
#define TCD32(offset) (*(volatile u32 *)(ST_STEREO_DMA+(offset)))
#define ST_STEREO_LOOPS (ST_STEREO_BLOCK/16u)
#define ST_STEREO_LINK (0x8000u|(46u<<9)|ST_STEREO_LOOPS)
static void send_dma(const u32 *from)
{
    while(TCD16(0x1e)&0x40u){}                /* CSR.ACTIVE */
    TCD32(0x00)=(u32)from;                    /* SADDR */
    TCD16(0x04)=0x0404u;                      /* ATTR: 16-byte bursts both sides */
    TCD16(0x06)=16;                           /* SOFF */
    TCD32(0x08)=16;                           /* NBYTES */
    TCD32(0x0c)=0;                            /* SLAST */
    TCD32(0x10)=ST_STEREO_SEQ;                /* DADDR: the window, AUDIO sees it at 0 */
    TCD16(0x14)=ST_STEREO_LINK;               /* CITER */
    TCD16(0x16)=16;                           /* DOFF */
    TCD32(0x18)=0;                            /* DLAST_SGA */
    TCD16(0x1c)=ST_STEREO_LINK;               /* BITER */
    TCD16(0x1e)=0;                            /* CSR, DONE cleared */
    ST_STEREO_SSRT=46;
}
#ifndef SEND_BLOCK
#define SEND_BLOCK(from,mask) ((void)(mask),send_dma(from))
#endif
/* One track's D: x = (r >> 1) - (l >> 1) into x[32] while y collects
 * x ^ (x << 1), whose highest bit is the bit length of x (~x for x < 0);
 * the shift is that length less 15 (FF1 counts from bit 31), 0 under
 * ST_STEREO_QUIET; then o gets the 32 mantissas x >> e, -32768 bounded to
 * -32767, as halfwords (the window's order). The same values as the C below. */
static u32 pack_track(const s32 *l,const s32 *r,u32 *out)
{
    s32 x[32],*p=x,a,b,c;u32 y=0,n=16,e;short *o=(short *)out;
    __asm__ volatile(
        "1:\n\tmove.l (%[l])+,%[a]\n\tmove.l (%[r])+,%[b]\n\tasr.l #1,%[a]\n\tasr.l #1,%[b]\n\t"
        "sub.l %[a],%[b]\n\tmove.l %[b],(%[p])+\n\tmove.l %[b],%[c]\n\tadd.l %[c],%[c]\n\t"
        "eor.l %[b],%[c]\n\tor.l %[c],%[y]\n\t"
        "move.l (%[l])+,%[a]\n\tmove.l (%[r])+,%[b]\n\tasr.l #1,%[a]\n\tasr.l #1,%[b]\n\t"
        "sub.l %[a],%[b]\n\tmove.l %[b],(%[p])+\n\tmove.l %[b],%[c]\n\tadd.l %[c],%[c]\n\t"
        "eor.l %[b],%[c]\n\tor.l %[c],%[y]\n\t"
        "subq.l #1,%[n]\n\tbne.s 1b"
        :[l]"+a"(l),[r]"+a"(r),[p]"+a"(p),[y]"+d"(y),[n]"+d"(n),[a]"=&d"(a),[b]"=&d"(b),[c]"=&d"(c)
        ::"cc","memory");
    __asm__("ff1.l %0":"+d"(y));
    if((s32)(e=16u-y)<=0)return 0;            /* bit length <= 15: |D| < 2^15 */
    p=x;n=16;
    __asm__ volatile(
        "1:\n\tmove.l (%[p])+,%[a]\n\tasr.l %[e],%[a]\n\tcmpi.l #-32768,%[a]\n\tbne.s 2f\n\t"
        "addq.l #1,%[a]\n"
        "2:\n\tmove.w %[a],(%[o])+\n\t"
        "move.l (%[p])+,%[a]\n\tasr.l %[e],%[a]\n\tcmpi.l #-32768,%[a]\n\tbne.s 3f\n\t"
        "addq.l #1,%[a]\n"
        "3:\n\tmove.w %[a],(%[o])+\n\t"
        "subq.l #1,%[n]\n\tbne.s 1b"
        :[p]"+a"(p),[o]"+a"(o),[n]"+d"(n),[a]"=&d"(a):[e]"d"(e):"cc","memory");
    return e;
}
#else
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
 * big >> 15. */
static u32 shift_of(u32 big)
{
    u32 v=big>>15,e;
    if(!v)return 0;
    e=1;if(v>>8){v>>=8;e+=8;}if(v>>4){v>>=4;e+=4;}if(v>>2){v>>=2;e+=2;}if(v>>1)e++;
    return e;
}
static u32 pack_track(const s32 *l,const s32 *r,u32 *o)
{
    u32 i,e,big=0;s32 d[32],x;
    for(i=0;i<32;i++){x=(r[i]>>1)-(l[i]>>1);d[i]=x;big|=(u32)(x^(x>>31));}
    if(big<ST_STEREO_QUIET/2u)return 0;    /* |D| under -96 dBFS: mono */
    e=shift_of(big);
    for(i=0;i<32;i+=2)*o++=((u32)mantissa(d[i],e)<<16)|((u32)mantissa(d[i+1],e)&0xffffu);
    return e;
}
#endif
#define OUT(stage,offset) (stage)[((offset)-ST_STEREO_SEQ)/4]
static void send(u32 mask,u32 *stage)
{
    u32 t,e;u8 shift[8]={0};
    ++seq;
    for(t=0;t<8;t++){
        if(!(mask&(1u<<t)))continue;
        e=pack_track(TRACKS+32*t,ST_STEREO_R(t),&OUT(stage,ST_STEREO_WINDOW+64*t));
        if(!e){mask&=~(1u<<t);continue;}
        shift[t]=(u8)e;
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
/* Drive on R. The native drive (0x4000112c, two tracks a pass) mixes, per
 * track, the dry signal with a ramped dry weight and the clipped signal with
 * a ramped wet weight: out = movclr(dry x + wet y), dry and wet ramping by
 * (target - current) >> 5 a frame. A track whose row word 0x28 is <= 0 has
 * targets dry 0x3fffffff and wet 0; when its current wet weight (word t of
 * the drive state) is 0 too, every wet product is 0 and the output is
 * movclr(dry x): the same MAC, with the same MACSR, as the native one. So
 * when every claimed track is off, R takes one MAC a frame instead of the
 * native call (about a thousand instructions a track). Otherwise the native
 * drive runs on the eight R buffers (the unclaimed ones are zero) from
 * `state`, the drive state R starts from, which is exchanged with the live
 * one around the call when it is not the live one.
 * tests/test_st_stereo_natives_emu.py compares the two in analog-emu. */
#ifdef __mcoldfire__
static int drive_off(u32 mask,const u32 *state)
{
    u32 t;
    for(t=0;t<8;t++)if(mask&(1u<<t))
        if(*(const volatile short *)(ROWS+142*t+0x50)>0 || state[t])return 0;
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
static void drive_r(u32 mask,u32 *state)
{
#ifdef __mcoldfire__
    u32 t;s32 dry;
    if(drive_off(mask,state)){
        for(t=0;t<8;t++)if(mask&(1u<<t)){
            dry=(s32)state[8+t];
            drive_dry(ST_STEREO_R(t),dry,(s32)(0x3fffffffu-(u32)dry)>>5);
        }
        return;
    }
#endif
    if(state!=DRIVE_STATE)swap_words(DRIVE_STATE,state,DRIVE_WORDS);
    DRIVE(ST_STEREO_R(0),ROWS);
    if(state!=DRIVE_STATE)swap_words(DRIVE_STATE,state,DRIVE_WORDS);
}
/* The base-width filter on one track's R. The native routine (0x40002320)
 * runs the eight tracks in pairs, but a track's filter depends only on its
 * own row, poles and two words of memory: a one-pole stage s1 and a second
 * stage s2 on the pole table at 0x40040060,
 *   s1' = pl s1 - (pl - 1) x           (ACC0)
 *   s2' = ph s2 - h s1 + h s1'         (ACC2, h = ph/2 + 1/2)
 * with pl = T[min((BASE + WDTH) >> 6, 508)] (core's audio_bw_index) and
 * ph = T[BASE >> 6], BASE and WDTH the row's words 0x66 and 0x68; out = s2'.
 * The memory is cleared when the pair's flag (0x80008550 + t) is set and the
 * pair's first row has word 0x82 set (the native tests the first track's row
 * for both). The same EMAC operations in the same order per accumulator,
 * fractional (MACSR 0xa0); the MACSR is 0x20 after, as the native leaves it.
 * tests/test_st_stereo_natives_emu.py compares it with the native. */
#ifndef BW_TABLE
#define BW_TABLE ((const s32 *)0x40040060u)
#define BW_FLAGS ((const volatile u8 *)0x80008550u)
#endif
#ifdef __mcoldfire__
static void width_track(s32 *x,u32 t,u32 *m)
{
    const u8 *row=ROWS+142*t;
    s32 base=*(const short *)(row+0x66),lo=(base+*(const short *)(row+0x68))>>6,hi=base>>6;
    s32 pl,ph,a,h,s1,s2,in;u32 n=32;
    if(lo>508)lo=508;
    pl=BW_TABLE[lo];ph=BW_TABLE[hi];
    a=(s32)((u32)pl+0x80000000u);h=(s32)(((u32)(ph>>1))+0x40000000u);
    if(BW_FLAGS[t] && *(const short *)(ROWS+142*(t&~1u)+0x82)){m[0]=0;m[1]=0;}
    s1=(s32)m[0];s2=(s32)m[1];
    set_macsr(0xa0);
    __asm__ volatile(
        "movclr.l %%acc0,%[in]\n\tmovclr.l %%acc2,%[in]\n\t"
        "mac.l %[pl],%[s1],(%[p]),%[in],%%acc0\n"
        "1:\n\t"
        "msac.l %[a],%[in],%%acc0\n\t"
        "msac.l %[h],%[s1],%%acc2\n\t"
        "mac.l %[ph],%[s2],%%acc2\n\t"
        "movclr.l %%acc0,%[s1]\n\t"
        "mac.l %[h],%[s1],%%acc2\n\t"
        "mac.l %[pl],%[s1],4(%[p]),%[in],%%acc0\n\t"
        "movclr.l %%acc2,%[s2]\n\t"
        "move.l %[s2],(%[p])+\n\t"
        "subq.l #1,%[n]\n\tbne.s 1b\n\t"
        "movclr.l %%acc0,%[in]"
        :[p]"+a"(x),[n]"+d"(n),[s1]"+d"(s1),[s2]"+d"(s2),[in]"=&d"(in)
        :[pl]"r"(pl),[ph]"r"(ph),[a]"r"(a),[h]"r"(h):"cc","memory");
    set_macsr(0x20);
    m[0]=(u32)s1;m[1]=(u32)s2;
}
#endif
/* The multimode's second part on R. The native multimode (0x400019d6, one
 * track a call) first computes the block's targets from the row
 * (0x80008900..0x8000891c; it also writes the 0x80008720 increments, which
 * its second part rewrites), then from 0x40001ea4 on ramps, filters and
 * mixes. R's row and starting words are L's, so its targets are the ones
 * the L call has just left at 0x80008900. st_multimode_tail builds the
 * native frame (the prologue, the MACSR, t at 48 and the track's type at
 * 52, the only words the first part leaves for the rest) and jumps to
 * 0x40001ea4, the join where every path of the first part ends: the same
 * words as the whole routine, about 150 instructions less.
 * The track's control words (the ones the multimode writes for track t):
 * its coefficients and mix weights, its 0x800088e0 and type words and its
 * two flag bytes; its signal words: 0x80008580 + 4t and 0x80008920 + 8t. */
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
#else
#define st_multimode_tail MULTI
#endif
#define MULTI_TRACK_WORDS 14u
static void multi_words(u32 t,u32 *w,int save)
{
    u32 *c=CHAIN_WORDS(0x80008740u+32u*t),*m=CHAIN_WORDS(0x80008840u+12u*t);
    u32 *e=CHAIN_WORDS(0x800088e0u+4u*t),*y=CHAIN_WORDS(0x80008560u+4u*t);
    u8 *f=(u8 *)CHAIN_WORDS(0x80008550u)+t;
    if(save){
        w[0]=c[0];w[1]=c[1];w[2]=c[2];w[3]=c[3];w[4]=c[4];w[5]=c[5];w[6]=c[6];w[7]=c[7];
        w[8]=m[0];w[9]=m[1];w[10]=m[2];w[11]=*e;w[12]=*y;w[13]=(u32)f[0]<<8|f[8];
    }else{
        c[0]=w[0];c[1]=w[1];c[2]=w[2];c[3]=w[3];c[4]=w[4];c[5]=w[5];c[6]=w[6];c[7]=w[7];
        m[0]=w[8];m[1]=w[9];m[2]=w[10];*e=w[11];*y=w[12];f[0]=(u8)(w[13]>>8);f[8]=(u8)w[13];
    }
}
static void multi_signal(u32 t)
{
    u32 *r=ST_STEREO_SIGNAL,*a=CHAIN_WORDS(0x80008580u+4u*t),*b=CHAIN_WORDS(0x80008920u+8u*t),v;
    v=*a;*a=r[t];r[t]=v;
    v=b[0];b[0]=r[24+2*t];r[24+2*t]=v;
    v=b[1];b[1]=r[25+2*t];r[25+2*t]=v;
}
/* The AMP envelope on R: the native per-track routine (0x40001476) from the
 * envelope state (0x800049e0 + 12t, three words) the AMP stage will find for
 * L, put back after; the IRQ's own trig, held and gate masks. */
static void envelope_r(u32 t)
{
    u32 *e=CHAIN_WORDS(0x800049e0u+12u*t),keep[3],trig,held,gate;
    trig=~(u32)*(const volatile u16 *)(FRAME+0x182)&FRAME[0x180];
    held=(u32)FRAME[0x181]|*(const volatile u16 *)(FRAME+0x182);
    gate=FRAME[0x1a8];
    keep[0]=e[0];keep[1]=e[1];keep[2]=e[2];
    ENVELOPE(ST_STEREO_R(t),e,ROWS+142*t+0x6a,(trig>>t)&1,(held>>t)&1,t,(gate>>t)&1);
    e[0]=keep[0];e[1]=keep[1];e[2]=keep[2];
}
/* The fused multimode (stereo/multimode.s): the native routine's first part
 * (400019d6..40001ea3, the block's targets), copied on the unit from its own
 * AUDIO image into fused_first, then a jump to st_mm_fused_tail, the second
 * part on L and R at once. The first part has no PC-relative operand and no
 * call, and every exit of it is the join at its end (40001ea4), so a copy
 * followed by the jump runs as the native does. The copy is made only when
 * the bytes have OS 1.42's fingerprint (FNV-1a); otherwise R takes the
 * native routine's second part after L's call (st_multimode_tail). No
 * firmware byte is in this source or in the bank. */
#define FUSED_FROM 0x400019d6u
#define FUSED_BYTES 1230u                 /* to the join, 40001ea4 */
#define FUSED_PRINT 0xe1c6c2f4u
static u16 fused_first[FUSED_BYTES/2+3] __attribute__((aligned(16)));
#ifdef __mcoldfire__
void st_mm_fused_tail(void);
static void fused_prepare(void)
{
    const volatile u8 *from=(const volatile u8 *)FUSED_FROM;
    const volatile u16 *words=(const volatile u16 *)FUSED_FROM;
    u32 i,h=0x811c9dc5u,line;
    for(i=0;i<FUSED_BYTES;i++)h=(h^from[i])*0x01000193u;
    if(h!=FUSED_PRINT){fused=FUSED_OFF;return;}
    for(i=0;i<FUSED_BYTES/2;i++)fused_first[i]=words[i];
    fused_first[FUSED_BYTES/2]=0x4ef9;                          /* jmp.l */
    fused_first[FUSED_BYTES/2+1]=(u16)((u32)st_mm_fused_tail>>16);
    fused_first[FUSED_BYTES/2+2]=(u16)(u32)st_mm_fused_tail;
    /* push the data cache's lines to memory and drop the instruction cache's */
    for(line=(u32)fused_first&~15u;line<(u32)(fused_first+FUSED_BYTES/2+3);line+=16)
        __asm__ volatile("cpushl %%bc,(%0)"::"a"(line):"memory");
    __asm__ volatile("nop":::"memory");
    fused=FUSED_READY;
}
#else
static void fused_prepare(void){fused=FUSED_OFF;}
#endif
/* R's drive and base-width, at track 0's multimode call (the stock chain
 * has run drive and base-width on L; its MACSR is put back after). */
static void prepass(u32 mask)
{
    u32 t,macsr=get_macsr(),a=CLOCK;
    set_macsr(32);
    /* With no previous block of R, R starts from the L drive's state too
     * (a copy: the live state stays L's). */
    if(!primed)copy_words(drive_before,DRIVE_STATE,DRIVE_WORDS);
    drive_r(mask,drive_before);
#ifdef __mcoldfire__
    for(t=0;t<8;t++)if(mask&(1u<<t))width_track(ST_STEREO_R(t),t,ST_STEREO_SIGNAL+8+2*t);
#else
    (void)t;
#endif
    set_macsr(macsr);
    profile[P_DRIVE]+=CLOCK-a;
}
/* The multimode entry (chain directory word 3): every track's call. Mono
 * tracks go straight to the native routine; track 0 first runs R's drive and
 * base-width when any track is stereo. */
void st_stereo_multimode_c(void *row,s32 *buffer,u32 x,u32 t,u32 trig,s32 word)
{
    u32 pre[MULTI_TRACK_WORDS],post[MULTI_TRACK_WORDS],a,b;
    if(!t && claimed)prepass(claimed);
    if(!(claimed&(1u<<t))){MULTI(row,buffer,x,t,trig,word);return;}
    a=CLOCK;
#ifdef __mcoldfire__
    if(fused==FUSED_READY){
        ((void (*)(void *,s32 *,u32,u32,u32,s32))(void *)fused_first)(row,buffer,x,t,trig,word);
        b=CLOCK;profile[P_MULTI]+=b-a;
        envelope_r(t);
        profile[P_AMP]+=CLOCK-b;
        return;
    }
#endif
    multi_words(t,pre,1);
    MULTI(row,buffer,x,t,trig,word);
    multi_words(t,post,1);multi_words(t,pre,0);
    multi_signal(t);
    st_multimode_tail(row,ST_STEREO_R(t),x,t,trig,word);
    multi_signal(t);
    multi_words(t,post,0);
    b=CLOCK;profile[P_MULTI]+=b-a;
    envelope_r(t);
    profile[P_AMP]+=CLOCK-b;
}
#ifdef __mcoldfire__
/* Stack: return, row 4, buffer 8, x 12, t 16, trig 20, word 24 (the native
 * routine's own arguments, untouched on the way to it). */
__asm__(".text\n\t.even\n\t.globl st_stereo_multimode\n"
        "st_stereo_multimode:\n\t"
        "move.l st_stereo_claimed,%d1\n\t"
        "beq.s 1f\n\t"
        "move.l 16(%sp),%d0\n\t"
        "beq.s 2f\n\t"
        "btst %d0,%d1\n\t"
        "bne.s 2f\n"
        "1:\n\tjmp 0x400019d6\n"
        "2:\n\tjmp st_stereo_multimode_c\n");
#else
void st_stereo_multimode(void){}
#endif
void st_stereo_chain_end(void)
{
    u32 mask=claimed,left=last&~mask,t,i,a,b;s32 *r;
    a=CLOCK;if(last_tick)profile[P_PERIOD]+=a-last_tick;last_tick=a;
    if(++profiled==1024){profiled=0;publish_profile();}
    claimed=0;last=mask;calls++;
    /* A track that leaves the claim: its R, which its machine reads as the
     * last block's output, becomes silence. */
    for(t=0;left;t++,left>>=1)if(left&1)for(r=ST_STEREO_R(t),i=0;i<32;i++)r[i]=0;
    /* No stereo track: one block with an empty mask tells MAIN, then the
     * window is left alone until a track is stereo again. */
    if(!mask){primed=0;if(!quiet){send(0,staging);quiet=1;}return;}
    quiet=0;
    blocks++;
    copy_words(drive_before,DRIVE_STATE,DRIVE_WORDS);primed=1;
    b=CLOCK;profile[P_STATE]+=b-a;
    send(mask,staging);
    profile[P_SEND]+=CLOCK-b;
}
