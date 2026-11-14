/* SPDX-License-Identifier: GPL-2.0-or-later
 * MAIN audio IRQ: the 24 extended controls of all digital tracks, published
 * in two alternating 512-byte FPGA banks. No allocation, disk IO or waits.
 * Native 0x1b0-byte frames retain their size. Every block reads and compares
 * the effective values (LFO, ramps, locks keep block resolution); a NEW
 * generation is written only when a value, a type or the active mask
 * changed, and only the rows that bank holds stale. Byte +0x1a9 names the
 * newest COMPLETE generation in every frame: a bank is never rewritten until
 * a newer one is complete, so AUDIO may retry it on any later block.
 */
#include "machines.h"
#include <syntakt/native.h>
extern unsigned int core_native(unsigned int,const unsigned int *);
typedef st_u32 u32;typedef unsigned short u16;typedef core_u8 u8;
#define ROW CORE_MACHINE_SHADOW_ROW
#ifndef CORE_MACHINE_TIMER
#define CORE_MACHINE_TIMER() (*(const volatile u32 *)0xfc07000cu) /* accounting's MAIN timer */
#endif
/* Host API 3 telemetry: blocks, timer ticks inside the sender, publications,
 * rows written, then the last publication's value-changed tracks and the OR
 * of their XOR differences (nonzero: changed). Blocks is incremented last. */
volatile u32 core_machine_telemetry[6];
/* Rows each bank holds stale. The shadow's initial contents never matter: a
 * row outside the published mask is always written, and its comparison
 * refreshes every word first. (Word-granular writes, 0.12.7, saved no
 * measurable MAIN time and cost resident code; rows are rewritten whole.) */
static u32 generation,published,stale[2]={~0u,~0u};
/* Cadence: every 4th block compares all tracks (and refreshes the active and
 * LFO-routed masks); other blocks only tracks that trig in this block, so a
 * lock lands with its note, and tracks with a native LFO routed to an
 * extended slot with depth. Edits, slides and aftertouch reach AUDIO within
 * four blocks (2.7 ms); a removal waits for the next full block. */
static u32 cadence,active,routed;
static __inline__ int extended_slot(u32 s){return s==0x11 || s==0x18 || (s>=0x36 && s<=0x46);}
/* Index -> Sound slot is the core's fixed layout (machines.c, checked by the
 * registry test): indices 8..23 are the contiguous slots 36..45, then 0, 11,
 * 18 and 46. Slots 11/18 carry type/gate in the working projection: their
 * value is the Sound mirror plus any of the two native LFOs routed there. */
static u32 special(const u16 *working,const u16 *mirror,const struct core_machine_def *m,
                   u32 owner,u32 index,u32 slot)
{
    int v=mirror[slot];
    if((working[4]>>8)==slot)v+=*(const volatile int *)(0x444da748u+owner*0x50);
    if((working[12]>>8)==slot)v+=*(const volatile int *)(0x444da770u+owner*0x50);
    if(v<(int)m->parameter[index].min)v=(int)m->parameter[index].min;
    if(v>(int)m->parameter[index].max)v=(int)m->parameter[index].max;
    return (u32)v&65535u;
}
static __attribute__((noinline)) void publish_block(u32 trigs)
{
    volatile u8 *tail=(void *)0x800021a9u;
    volatile u32 *bank,*r;u32 t,j,n,b,type,tag,mask,changed=0,sum,words,owner,w,bits,all=0,*row,rows=0,todo,full,lfo=0;
    const unsigned char *owners,*lfo_owners;const struct core_machine_def *m;
    const u16 *working,*mirror;
    /* The native boot sender owns the whole 0x800..0xfff page until AUDIO
     * has acknowledged it: nothing in either bank survives that. */
    /* The stock type range has bit 6 clear. Two aligned SRAM reads reject
     * an all-stock digital bank before owner queries and the eight-track
     * cadence scan. Other high types may take the full path harmlessly. */
    if(!core_machine_active || !*(const volatile u32 *)0x43bdc824u ||
       !((*(const volatile u32 *)0x80002f50u|*(const volatile u32 *)0x80002f54u)&0x40404040u)){
        stale[0]=stale[1]=~0u;published=0;active=routed=cadence=0;*tail=0;return;}
    /* Every block: the profiler's D/C boundary and POLY's owner table. */
    owners=(const unsigned char *)core_native(ST_NATIVE_OWNERS,0);
    {const u32 query=1;lfo_owners=(const unsigned char *)core_native(ST_NATIVE_OWNERS,&query);}
    full=!(cadence++&3u);
    todo=full?0xffu:(trigs|routed)&0xffu;
    if(!todo){*tail=published?(u8)generation:0;return;}
    mask=full?0:active;
    for(t=0;t<8;t++){
        if(!(todo&(1u<<t)))continue;
        owner=owners && owners[t]<=t?owners[t]:t;
        type=*(const volatile u8 *)(0x80002f50u+owner);
        if(type<64 || !(m=core_machine_find(type)) || (n=m->params)<=8)continue;
        working=(const u16 *)(0x80006364u+t*0x8e);mirror=(const u16 *)(0x800021ccu+owner*0x8e);
        if((extended_slot(working[4]>>8) && working[8]!=0x4000) ||
           (extended_slot(working[12]>>8) && working[16]!=0x4000))lfo|=1u<<t;
        words=(n-7)/2;type|=(n-8)<<16;mask|=1u<<t;row=core_machine_shadow+ROW*t;
        bits=row[0]^type;row[0]=type;
        /* Pairs of contiguous slots: plain loads, no per-value lookup. */
        for(j=0;j<words && j<8;j++){
            w=(u32)working[0x36+2*j]<<16;
            if(9+2*j<n)w|=working[0x37+2*j];
            bits|=row[1+j]^w;row[1+j]=w;
        }
        if(words>8){                         /* n >= 25: indices 24..27 */
            /* Slot 0 is the native None destination: modulation routed to
             * None lands in working[0]; the stored value is the mirror's. */
            w=(u32)mirror[0]<<16;
            if(n>25)w|=special(working,mirror,m,lfo_owners?lfo_owners[t]:owner,25,0x11);
            bits|=row[9]^w;row[9]=w;
            if(n>26){
                w=special(working,mirror,m,lfo_owners?lfo_owners[t]:owner,26,0x18)<<16;
                if(n>27)w|=working[0x46];
                bits|=row[10]^w;row[10]=w;
            }
        }
        if(bits){changed|=1u<<t;all|=bits;}
    }
    if(full)routed=lfo;else routed|=lfo;
    active=mask;
    /* Stock-only blocks never touch the FPGA mailbox. AUDIO forgets rows
     * outside a committed mask, so a returning row is always republished. */
    if(!mask){published=0;*tail=0;return;}
    changed|=mask&~published;
    if(!changed){*tail=(u8)generation;return;}
    stale[0]|=changed;stale[1]|=changed;
    tag=generation%254+1;b=tag&1;
    bank=(void *)(0x10000c00u+(b<<9));
    /* Invalidate first: a reader that saw the previous header rechecks it
     * after every row, so it never accepts words of this rewrite. */
    bank[0]=0;bank[1]=0;
    for(t=0;t<8;t++){
        if(!(mask&stale[b]&(1u<<t)))continue;
        row=core_machine_shadow+ROW*t;type=row[0];r=bank+4+15*t;sum=t+type;r[0]=type;
        for(j=1;j<=((type>>16)+1)/2;j++){r[j]=row[j];sum+=row[j];}
        r[13]=sum;rows++;
    }
    stale[b]&=~mask;
    bank[2]=mask;bank[3]=0;bank[1]=tag;bank[0]=0x4d503333u;
    generation=tag;published=mask;
    __asm__ volatile("" ::: "memory");*tail=(u8)tag;
    core_machine_telemetry[2]++;core_machine_telemetry[3]+=rows;
    core_machine_telemetry[4]=changed;core_machine_telemetry[5]=all;
}
/* Frame tail: trigs = this block's per-track trigger mask (D3 at 400a4d78). */
void core_machine_snapshot(u32 trigs)
{
    u32 start=CORE_MACHINE_TIMER();
    publish_block(trigs);
    core_machine_telemetry[1]+=CORE_MACHINE_TIMER()-start;
    __asm__ volatile("" ::: "memory");core_machine_telemetry[0]++;
}
