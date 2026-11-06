/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real scalar PCM/machine C, native host. No target instruction execution.
 */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../mods/samplers-st/audio/sampler.h"
static short pcm[8192];
static struct sp_state fresh(unsigned frames,unsigned play)
{
    struct sp_state s;memset(&s,0,sizeof(s));s.source.frames=frames;s.source.pcm=pcm;
    s.step=4096;s.control[1]=(unsigned short)(play<<8);return s;
}
int main(void)
{
    struct sp_state s;int out[32];unsigned i,frame,expected;
    for(i=0;i<8192;i++)pcm[i]=(short)(i*7-16000);
    s=fresh(8192,3);s.control[5]=0x7800;sp_one_window(&s,60<<16,14400,1);
    sp_pcm(&s,out);for(i=0;i<32;i++)assert(out[i]==(int)pcm[i]*65536);assert(s.frame==32);
    s=fresh(8192,0);s.control[5]=0x7800;sp_one_window(&s,60<<16,14400,1);
    sp_pcm(&s,out);for(i=0;i<32;i++)assert(out[i]==(int)pcm[8191-i]*65536);
    /* One-shot completion and reverse completion never fetch outside a sample. */
    s=fresh(7,3);s.control[5]=0x7800;sp_one_window(&s,60<<16,14400,1);sp_pcm(&s,out);
    assert(!s.active);for(i=7;i<32;i++)assert(!out[i]);
    s=fresh(7,0);s.control[5]=0x7800;sp_one_window(&s,60<<16,14400,1);sp_pcm(&s,out);
    assert(!s.active);for(i=7;i<32;i++)assert(!out[i]);
    /* Tiny loops disable crossfade; a frame reference checks forward/reverse wrap. */
    for(i=1;i<=2;i++){
        s=fresh(7,i);sp_window(&s,0,7,2,i,1);frame=i==1?6:0;
        for(expected=0;expected<100;expected++){
            sp_pcm(&s,out);
            for(unsigned j=0;j<32;j++){
                assert(out[j]==(int)pcm[frame]*65536);
                frame=i==1?(frame==2?6:frame-1):(frame==6?2:frame+1);
            }
            assert(s.frame==frame && s.active);
        }
    }
    /* Fractional cursor arithmetic is independent of sample length. */
    s=fresh(8192,3);sp_window(&s,0,8192,0,3,1);s.step=6144;sp_pcm(&s,out);assert(s.frame==48 && !s.fraction);
    assert(sp_scale(15500000,30720)==15500000 && sp_scale(15500000,15360)==7750000);
    /* REPITCH: formula and four-times cap, including windows with a large product. */
    s=fresh(96000,3);s.control[5]=0x7800;s.control[6]=0x0200;
    sp_repitch_window(&s,60<<16,14400,1);assert(s.step==((96000u*((14400u<<16)/1350000))>>16));
    s=fresh(2880000,3);s.control[5]=0x7800;sp_repitch_window(&s,60<<16,36000,1);assert(s.step==0x4000);
    /* Note-select slice grid and explicit slice selection, with real zero crossings. */
    for(i=0;i<8192;i++)pcm[i]=(short)((i&31)<16?-12000:12000);
    s=fresh(8192,3);s.control[6]=0x0400;sp_slice_window(&s,12<<16,14400,1);
    assert(s.start==0 && s.end==sp_point(&s.source,4));
    s.control[4]=0x4000;sp_slice_window(&s,12<<16,14400,1);
    assert(s.start==sp_point(&s.source,252) && s.end==8192);
    /* WERP starts and advances on its tempo grid; fractional play reads remain bounded. */
    s=fresh(8192,2);s.control[4]=0x0100;s.control[5]=0x0300;s.control[6]=0x0200;
    sp_werp_window(&s,60<<16,14400,1);assert(s.running && s.start<s.end);
    for(i=0;i<1000;i++){sp_werp_window(&s,60<<16,14400,0);sp_pcm(&s,out);assert(s.start<=s.end && s.end<=8192);}
    /* BR must not turn a near-zero negative sample into a loud DC step. */
    pcm[0]=-1;pcm[1]=1;pcm[2]=-8191;pcm[3]=8191;
    pcm[4]=-16384;pcm[5]=16384;pcm[6]=-32767;pcm[7]=32767;
    s=fresh(8192,3);sp_window(&s,0,8192,0,3,1);s.br=127;
    sp_pcm(&s,out);
    for(i=0;i<8;i+=2)assert(out[i]==-out[i+1]);
    assert(!out[0] && !out[1] && !out[2] && !out[3]);
    assert(out[4]==-16384*65536 && out[5]==16384*65536);
    s=fresh(8192,3);sp_window(&s,0,8192,0,3,1);s.br=0;
    sp_pcm(&s,out);for(i=0;i<8;i++)assert(out[i]==(int)pcm[i]*65536);
    /* The lower half of the knob now changes a normal-level sample, and
     * increasing BR never restores bits already removed. */
    pcm[0]=12345;pcm[1]=-12345;
    s=fresh(8192,3);sp_window(&s,0,8192,0,3,1);s.br=32;
    sp_pcm(&s,out);assert(out[0]!=12345*65536 && out[0]==-out[1]);
    assert(out[0]>0 && out[0]<12345*65536);expected=(unsigned)out[0];
    s=fresh(8192,3);sp_window(&s,0,8192,0,3,1);s.br=64;
    sp_pcm(&s,out);assert(out[0]>0 && (unsigned)out[0]<=expected);
    puts("PASS: source PCM, forward/reverse/loop/fractional bounds, long scaling, REPITCH, SLICE and WERP");return 0;
}
