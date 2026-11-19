/* SPDX-License-Identifier: GPL-2.0-or-later
 * Stereo transport (samplers-st stereo/), native host: AUDIO's D packing
 * into the shared window and MAIN's gR x D on a right bus, with the stock
 * kernels' MSAC (bus -= g x x, Q31) standing in for the native mix. No target
 * code runs: the chain calls of st_stereo_chain_end are not exercised.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t window[0x800/4];
static int32_t tracks[8*32],right[8][32],main_l[8*32];
static uint32_t ticks,stage[0x230/4];
#define TRACKS tracks
#define CLOCK ticks
#define MAIN_CLOCK ticks
#define ROWS ((unsigned char *)0)
#define WIN(offset) window[(offset)/4]
#define ST_STEREO_R(t) (right[t])
#include "../mods/samplers-st/stereo/audio.c"
#undef WIN
static int32_t gains[4][8],buses[4][64];
#define WIN(offset) (*(const volatile uint32_t *)&window[(offset)/4])
#define DELAY_GAIN gains[0]
#define REVERB_GAIN gains[1]
#define MAIN_GAIN gains[2]
#define DIRECT_GAIN gains[3]
#define SEND_BUS buses[0]
#define MAIN_BUS buses[2]
#define DIRECT_BUS buses[3]
#define TRACK_PCM main_l
#undef ST_STEREO_MAIN
#define ST_STEREO_MAIN 0
#define u32 main_u32
#define s32 main_s32
#include "../mods/samplers-st/stereo/main.c"
#undef u32
#undef s32
static int32_t msac(int32_t acc,int32_t g,int32_t x)
{
    int64_t r=(int64_t)acc-(((int64_t)g*x)>>31);
    return r>INT32_MAX?INT32_MAX:r<INT32_MIN?INT32_MIN:(int32_t)r;
}
static int32_t random32(void){return (int32_t)(((uint32_t)rand()<<17)^((uint32_t)rand()<<2)^(uint32_t)rand());}
int main(void)
{
    unsigned t,i,block;int32_t worst=0;
    srand(7);initialize(0);
    for(block=0;block<6;block++){
        int32_t start[8],end[8];
        for(t=0;t<8;t++)for(i=0;i<32;i++){
            int32_t l=random32()>>(t+1),r=t&1?l:random32()>>(t+1);
            if(t==7){l=0x7fffffff;r=(int32_t)0x80000001;}   /* the extreme D */
            tracks[32*t+i]=l;right[t][i]=r;
        }
        send(0xfeu,stage);                    /* track 0 mono: it gets no D */
        assert(window[ST_STEREO_SEQ/4]==window[ST_STEREO_SEQ_END/4] && window[ST_STEREO_TAG/4]==ST_STEREO_MAGIC);
        memcpy(main_l,tracks,sizeof(tracks));   /* MAIN's DMA of L */
        receive();
        assert(have && slot[0].mask==0xd4u);   /* tracks 1, 3, 5: R == L, no D */
        /* Native kernel: ramp from the previous end to this block's end. */
        for(t=0;t<8;t++){start[t]=block?gains[2][t]:-(int32_t)(0x40000000u>>(t&3));
            end[t]=-(int32_t)(0x7e000000u>>(t&3))+(int32_t)block*0x100000;}
        memset(buses,0,sizeof(buses));
        for(t=0;t<8;t++){
            int32_t g=block?start[t]:end[t],step=(end[t]-g)>>5;
            for(i=0;i<32;i++,g+=step)buses[2][2*i+1]=msac(buses[2][2*i+1],g,tracks[32*t+i]);
            gains[2][t]=end[t];
        }
        add(2,MAIN_BUS,MAIN_GAIN);
        assert(use==&slot[0]);
        /* Expected: the same kernel on R for stereo tracks, L for track 0. */
        {int32_t want[64]={0};
         for(t=0;t<8;t++){
             int32_t g=block?start[t]:end[t],step=(end[t]-g)>>5;
             for(i=0;i<32;i++,g+=step)want[2*i+1]=msac(want[2*i+1],g,t?right[t][i]:tracks[32*t+i]);
         }
         for(i=0;i<32;i++){
             int64_t e=(int64_t)buses[2][2*i+1]-want[2*i+1];if(e<0)e=-e;
             if(e>worst)worst=(int32_t)e;
             assert(buses[2][2*i]==0);
         }}
    }
    /* Eight tracks at up to full scale: within 2^-13 of full scale overall. */
    printf("worst error %d (%.1f dB below full scale)\n",worst,worst?-20*__builtin_log10((double)worst/2147483648.0):999);
    assert(worst<(1<<18));
    /* The same L mixed again (AUDIO had no new block): its D is used again. */
    receive();assert(current()!=0 && st_stereo_main_counters.again==1);
    /* AUDIO writes a new block after ev_render_in. Before MAIN's DMA of L:
     * the newer D matches and is used. After it: the first one is kept. */
    for(t=0;t<8*32;t++)tracks[t]^=0x5a5a00;
    send(0xffu,stage);memcpy(main_l,tracks,sizeof(tracks));receive();
    for(t=0;t<8*32;t++)tracks[t]+=77777;
    send(0xffu,stage);memcpy(main_l,tracks,sizeof(tracks));
    assert(current()==0xffu && use==&slot[1] && st_stereo_main_counters.late==1);
    for(t=0;t<8*32;t++)tracks[t]+=33333;
    send(0xffu,stage);memcpy(main_l,tracks,sizeof(tracks));receive();
    for(t=0;t<8*32;t++)tracks[t]+=55555;
    {uint32_t keep=slot[0].sequence;send(0xffu,stage);
     assert(current()==0xffu && use==&slot[0] && slot[0].sequence==keep);}
    /* No D matches the L being mixed (the read overlapped AUDIO's write). */
    receive();for(t=0;t<8*32;t++)main_l[t]=t*3;
    {uint32_t dropped=st_stereo_main_counters.dropped;assert(current()==0 && st_stereo_main_counters.dropped==dropped+1);}
    /* A block the DMA is still writing: SEQ is new, SEQ_END old. */
    send(0xffu,stage);memcpy(main_l,tracks,sizeof(tracks));
    window[ST_STEREO_SEQ/4]++;receive();assert(!have && current()==0 && st_stereo_main_counters.torn>=1);
    window[ST_STEREO_SEQ/4]--;
    /* A silent L that repeats keeps its D too: never mono. */
    {int32_t keep[8*32];uint32_t again=st_stereo_main_counters.again;
     memcpy(keep,tracks,sizeof(keep));memset(tracks,0,sizeof(tracks));
     send(0xffu,stage);memset(main_l,0,sizeof(main_l));receive();assert(current()==0xffu);
     receive();assert(current()==0xffu && st_stereo_main_counters.again==again+1);
     memcpy(tracks,keep,sizeof(keep));}
    /* After a restart the window still holds the last session's block: it is
     * not used until the engine has sent one of this boot. */
    alive=0;initialize(0);receive();assert(current()==0 && st_stereo_main_counters.stale==1);
    send(0xffu,stage);memcpy(main_l,tracks,sizeof(tracks));receive();assert(current()==0xffu && alive);
    /* A track whose D is zero leaves the mask. */
    memcpy(right,tracks,sizeof(right));send(0xffu,stage);assert(window[ST_STEREO_MASK/4]==0);
    /* The engine's empty block (no stereo track): MAIN mixes nothing and
     * counts no dropped block, interrupt after interrupt. */
    {uint32_t dropped;st_stereo_main_counters.mask=0xff;send(0,stage);receive();assert(current()==0);
     assert(st_stereo_main_counters.mask==0);          /* the probe's M shows no stereo track */
     dropped=st_stereo_main_counters.dropped;
     for(t=0;t<4;t++){for(i=0;i<8*32;i++)main_l[i]=(int32_t)(i*7+t);receive();assert(current()==0);}
     assert(st_stereo_main_counters.dropped==dropped);}
    /* A tail's silence for the claim: under 2^16 in magnitude, the native
     * multimode's residual (a few hundred) included; at 2^16 and beyond, sound. */
    {int x[32];static const int quiet[]={0,1,-1,270,-270,0xffff,-0xffff};
     static const int loud[]={0x10000,-0x10000,0x7fffffff,(int)0x80000000,0x123456};
     for(t=0;t<sizeof(quiet)/sizeof(quiet[0]);t++){memset(x,0,sizeof(x));x[t*4]=quiet[t];assert(!st_stereo_audible(x));}
     for(t=0;t<sizeof(loud)/sizeof(loud[0]);t++){memset(x,0,sizeof(x));x[31-t]=loud[t];assert(st_stereo_audible(x));}}
    /* No engine: a window without the tag is never read. */
    window[ST_STEREO_TAG/4]=0;send(0xffu,stage);window[ST_STEREO_TAG/4]=0;receive();assert(!tagged && current()==0);
    puts("PASS: stereo D packing, gR x D on the right bus, L fingerprint pairing (late, kept, dropped, again), torn retries, a restart's stale block, zero D, the empty block, the quiet threshold");
    return 0;
}
