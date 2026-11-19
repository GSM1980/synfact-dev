/* SPDX-License-Identifier: GPL-2.0-or-later
 * High memory probe, AUDIO side (test only). Checks on the unit what
 * docs/AUDIO-MEMORY-MAP.md says from the code alone, before the pool moves there:
 *  - HIGH: 4404ff50..47ff0000 (pages 8001..ff40 of the sample store mapping)
 *    has no other writer. It is filled with a pattern a slice a block (so no
 *    block runs long), then checked a slice a block, over and over; each word
 *    something else changed is counted, the first address kept, the pattern
 *    restored.
 *  - STACK: how deep the AUDIO stack goes (it starts at 48000000). At its first
 *    render the probe paints 47ff0000 up to just below its own stack pointer;
 *    the lowest painted word found changed is the deepest any code reached.
 * Results go to window 7f0..7ff (MAIN 100007f0), past the stereo tracks' 5b0..7ef.
 * It runs while the pool is still at 42101750..4404f750 (core 2.1.1).
 */
typedef unsigned int u32;
#ifndef HIGH
#define HIGH ((volatile u32 *)0x4404ff50u)
#define HIGH_WORDS ((0x47ff0000u-0x4404ff50u)/4u)
#define STACK_LO 0x47ff0000u
#define STAT ((volatile u32 *)0x000007f0u)   /* 4 words: 7f0..7ff */
#endif
#define FILL_SLICE 8192u                     /* words a block while filling */
#define CHECK_SLICE 2048u                    /* words a block while checking */
#define STACK_SLICE 512u
#define STACK_TOP 0x48000000u
#define MAGIC 0x48505231u                    /* "HPR1" */
#define NONE 0xffffffffu
static u32 filled,cursor,blocks,bad,first=NONE,painted,stack_top,stack_cursor,stack_low=STACK_TOP;
static u32 pattern(u32 i){return (i*0x9e3779b1u)^0x5aa50000u;}
static void publish(void)
{
    u32 peak=(STACK_TOP-stack_low)>>4,w2=(bad>0xffffu?0xffffu:bad)|(peak>0xffffu?0xffffu:peak)<<16;
    STAT[0]=0;STAT[1]=blocks;STAT[2]=w2;STAT[3]=first;
    STAT[0]=MAGIC^(blocks+w2+first);
}
static void paint(void)
{
    volatile u32 here;u32 a,top=((u32)&here-256u)&~3u;
    /* Everything below this frame is dead now: whatever writes it later is stack. */
    for(a=STACK_LO;a<top;a+=4)*(volatile u32 *)a=pattern(a>>2);
    stack_top=top;stack_cursor=STACK_LO;painted=1;
}
void high_probe_update(u32 pitch,void *voice,const unsigned short *params)
{(void)pitch;(void)voice;(void)params;}
void high_probe_render(void *out,void *voice)
{
    u32 i,j,v,*o=out;(void)voice;
    for(i=0;i<32;i++)o[i]=0;
    if(!painted)paint();
    blocks++;
    if(filled<HIGH_WORDS){
        for(j=0;j<FILL_SLICE && filled<HIGH_WORDS;j++,filled++)HIGH[filled]=pattern(filled);
    }else{
        for(j=0;j<CHECK_SLICE;j++){
            i=cursor;if(++cursor>=HIGH_WORDS)cursor=0;v=pattern(i);
            if(HIGH[i]!=v){bad++;if(first==NONE)first=0x4404ff50u+4u*i;HIGH[i]=v;}
        }
    }
    for(j=0;j<STACK_SLICE;j++){
        u32 a=stack_cursor;stack_cursor+=4;if(stack_cursor>=stack_top)stack_cursor=STACK_LO;
        if(a<stack_low && *(volatile u32 *)a!=pattern(a>>2))stack_low=a;
    }
    publish();
}
