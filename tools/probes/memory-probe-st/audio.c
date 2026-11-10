/* SPDX-License-Identifier: GPL-2.0-or-later
 * Memory probe, AUDIO side (test only). On its first render it fills two
 * areas no code is known to use with a pattern, then checks them every
 * block and counts each word something else changed (and restores it):
 *  - the shared window 0x5b0..0x7df (AUDIO view; MAIN sees 0x100005b0),
 *  - AUDIO SRAM 0x8000f398..0x8000ffff, after FM TONE's scratch.
 * The window is checked whole each block; the SRAM in slices of 128 words.
 * Results go to window 0x7e0..0x7ff, which the probe's SYN page reads.
 * docs/MACHINE-REFERENCE.md has the window map.
 */
typedef unsigned int u32;
#ifndef WIN
#define WIN ((volatile u32 *)0x000005b0u)
#endif
#define WIN_WORDS 140u                       /* 0x5b0..0x7df */
#ifndef STAT
#define STAT ((volatile u32 *)0x000007e0u)   /* 8 words: 0x7e0..0x7ff */
#endif
#ifndef TAIL
#define TAIL ((volatile u32 *)0x8000f398u)
#endif
#define TAIL_WORDS 794u                      /* 0x8000f398..0x8000ffff */
#define SLICE 128u
#define MAGIC 0x4d505231u                    /* "MPR1" */
#define NONE 0xffffffffu
static u32 armed,cursor,blocks,win_bad,tail_bad,tail_dirty,win_first,tail_first;
static u32 pattern(u32 area,u32 i){return ((i*0x9e3779b1u)^0xa55a0000u^(area<<12))|1u;}
static void publish(void)
{
    STAT[0]=0;
    STAT[1]=blocks;STAT[2]=win_bad;STAT[3]=tail_bad;STAT[4]=tail_dirty;
    STAT[5]=win_first;STAT[6]=tail_first;
    STAT[7]=MAGIC+blocks+win_bad+tail_bad+tail_dirty+win_first+tail_first;
    STAT[0]=MAGIC;
}
static void arm(void)
{
    u32 i;
    /* The stock boot leaves the SRAM tail zero: count what was not. */
    for(i=0;i<TAIL_WORDS;i++){if(TAIL[i])tail_dirty++;TAIL[i]=pattern(1,i);}
    for(i=0;i<WIN_WORDS;i++)WIN[i]=pattern(0,i);
    win_first=tail_first=NONE;armed=1;
}
void probe_update(u32 pitch,void *voice,const unsigned short *params)
{(void)pitch;(void)voice;(void)params;}
void probe_render(void *out,void *voice)
{
    u32 i,j,v,*o=out;(void)voice;
    for(i=0;i<32;i++)o[i]=0;
    if(!armed)arm();
    blocks++;
    for(i=0;i<WIN_WORDS;i++){
        v=pattern(0,i);
        if(WIN[i]!=v){win_bad++;if(win_first==NONE)win_first=0x5b0u+4u*i;WIN[i]=v;}
    }
    for(j=0;j<SLICE;j++){
        i=cursor;if(++cursor>=TAIL_WORDS)cursor=0;v=pattern(1,i);
        if(TAIL[i]!=v){tail_bad++;if(tail_first==NONE)tail_first=0x8000f398u+4u*i;TAIL[i]=v;}
    }
    publish();
}
