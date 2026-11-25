/* SPDX-License-Identifier: GPL-2.0-or-later
 * Host runner for mods/wavetable-st/engine.c (tests/test_wavetable_st.py).
 * Reads a scenario (little-endian u32 words): mode (0 a canonical table,
 * 1 a plain mono sample, 2 a plain stereo sample), waves (mode 0) or frames
 * (modes 1, 2), the PCM as 32-bit words, the seed, then blocks of {trig,
 * pitch8, pos, lev, spd, alev, apos, anim, tempo, table}; writes each block's
 * 32 accumulator words and the oscillator's state. table 0 means no source
 * for that block. No device, no firmware.
 */
#include <stdio.h>
#include <stdlib.h>
#include "../mods/wavetable-st/engine.h"
static unsigned get(FILE *f){unsigned v;if(fread(&v,4,1,f)!=1)exit(3);return v;}
int main(int argc,char **argv)
{
    FILE *in,*out;unsigned mode,size,waves,i,n,blocks,b,starts[64];short *pcm;
    struct wt_osc o;struct wt_osc_in p;int acc[32];
    if(argc!=3 || !(in=fopen(argv[1],"rb")) || !(out=fopen(argv[2],"wb")))return 2;
    mode=get(in);size=get(in);
    n=mode?size*(mode==2?2u:1u):size*4088u;
    waves=mode?wt_sample_starts(size,mode==2,starts):size;
    pcm=malloc(2*(n?n:1));
    for(i=0;i<n;i++)pcm[i]=(short)(int)get(in);
    wt_osc_init(&o,get(in));blocks=get(in);
    fwrite(&waves,4,1,out);fwrite(starts,4,mode?waves:0,out);
    for(b=0;b<blocks;b++){
        unsigned trig=get(in);int pitch8=(int)get(in);
        p.pos=get(in);p.lev=get(in);p.spd=get(in);p.alev=get(in);p.apos=get(in);p.anim=get(in);p.tempo=get(in);
        p.waves=get(in)?waves:0;p.pcm=pcm;p.inc=wt_increment(pitch8);
        p.starts=mode?starts:0;p.shift=mode==2;
        if(trig)wt_osc_trig(&o);
        for(i=0;i<32;i++)acc[i]=0;
        wt_osc_block(&o,&p,acc);
        fwrite(acc,4,32,out);
        fwrite(&o.phase,4,1,out);fwrite(&o.anim,4,1,out);fwrite(&o.pos,4,1,out);fwrite(&o.gain,4,1,out);
    }
    /* The pitch table, every 1/16 semitone over the range and past both ends. */
    for(i=0;i<=36000u;i+=7u){unsigned v=wt_increment((int)i-512);fwrite(&v,4,1,out);v=wt_level(v);fwrite(&v,4,1,out);}
    fclose(out);return 0;
}
