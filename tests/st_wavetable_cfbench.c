/* SPDX-License-Identifier: GPL-2.0-or-later
 * SP WAVETBL on cfbench: the instructions of one track's 32-frame block,
 * both oscillators on a 64-wave table, crossfading between waves, ANIM on
 * position and level (the heaviest case), at a given note. Bit 16 of the
 * argument: no ANIM, a fixed position between two waves (the usual case);
 * bit 17 as well: the same on a plain sample. */
#include "../mods/wavetable-st/engine.h"
static short pcm[64*4088];
static struct wt_osc osc[2];
static struct wt_osc_in in[2];
static int acc[32];
unsigned setup_wt(unsigned note8)
{
    unsigned i,s=1;
    for(i=0;i<64u*4088u;i++){s^=s<<13;s^=s>>17;s^=s<<5;pcm[i]=(short)(s>>16);}
    for(i=0;i<2;i++){
        wt_osc_init(osc+i,i+1);
        in[i].pcm=pcm;in[i].waves=64;in[i].inc=wt_increment((int)(note8&0xffffu)+(int)i*7);
        in[i].pos=0x3f80;in[i].lev=0x6400;in[i].spd=0x2000;in[i].alev=0x5000;in[i].apos=0x6000;
        if(note8>>16){in[i].alev=in[i].apos=0x4000;}
        /* Bit 17: a plain sample (64 waves of 2048, mono) instead of the table. */
        if(note8>>17){static unsigned starts[64];in[i].waves=wt_sample_starts(64u*2048u,0,starts);in[i].starts=starts;in[i].shift=0;}
        in[i].anim=(6u+i*3u)<<8;in[i].tempo=14400;
        wt_osc_trig(osc+i);
    }
    return 0;
}
unsigned render_wt(unsigned note8)
{
    unsigned i;(void)note8;
    for(i=0;i<32;i++)acc[i]=0;
    wt_osc_block(osc,in,acc);wt_osc_block(osc+1,in+1,acc);
    return (unsigned)acc[31];
}
