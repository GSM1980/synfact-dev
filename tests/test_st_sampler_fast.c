/* SPDX-License-Identifier: GPL-2.0-or-later
 * The samplers' PCM runs (samplers-st 1.4) against the per-sample model they
 * replace, native host. sp_pcm renders the plain-interpolation stretches of a
 * block in runs and leaves every other sample (crossfade, the window's last
 * frame, a wrap or the end, BR, OVER) to the per-sample model; both must give
 * the same output and the same cursor, block after block. No target
 * instruction execution: the cost is measured in Unicorn
 * (tests/test_st_sampler_cost_emu.py).
 */
#define SP_PCM_REFERENCE
#include "../mods/samplers-st/audio/sampler.c"
#include "../mods/samplers-st/audio/one-shot.c"
#include "../mods/samplers-st/audio/werp.c"
#include "../mods/samplers-st/audio/repitch.c"
#include "../mods/samplers-st/audio/slice.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int *st_stereo_claim(unsigned int track){(void)track;return 0;}
static short pcm[2*6000];
static unsigned seed=12345;
static unsigned rnd(unsigned n){seed=seed*1103515245u+12345u;return ((seed>>8)%n);}
static void same(const struct sp_state *a,const struct sp_state *b)
{
    assert(a->frame==b->frame && a->fraction==b->fraction && a->active==b->active && a->wrapped==b->wrapped);
}
int main(void)
{
    unsigned i,k,block,cases=0,runs=0;
    for(i=0;i<2*6000;i++)pcm[i]=(short)(rnd(65536)-32768);
    pcm[100]=-32768;pcm[101]=32767;           /* full-scale steps */
    for(k=0;k<6000;k++){
        struct sp_state s,r;sp_s32 out[32],right[32],out2[32],right2[32];
        unsigned channels=1+rnd(2),frames=channels==2?6000:12000,play=rnd(4),want=rnd(3);
        unsigned start,end,loop;
        memset(&s,0,sizeof(s));s.source.pcm=pcm;s.source.channels=channels;
        /* small windows and loops too: the runs' limits meet the crossfade
         * and the wrap within a block */
        if(rnd(3)==0){start=rnd(frames-64);end=start+1+rnd(64);}
        else{start=rnd(frames/2);end=start+1+rnd(frames-start-1);}
        loop=start+rnd(end-start);
        s.source.frames=frames;s.control[1]=(unsigned short)(play<<8);
        s.br=rnd(4)==0?rnd(128):0;
        s.control[7]=(unsigned short)((rnd(4)==0?rnd(128):0)<<8);
        switch(rnd(4)){
        case 0:s.step=4096;break;
        case 1:s.step=rnd(0x100001);break;
        case 2:s.step=1+rnd(8192);break;
        default:s.step=4096*(1+rnd(256));break;
        }
        sp_window(&s,start,end,loop,play,1);
        if(rnd(2))s.frame=start+rnd(end-start);
        s.fraction=rnd(4096);
        r=s;
        for(block=0;block<12;block++){
            sp_s32 *rp=want==0?0:right,*rp2=want==0?0:right2;
            memset(out,0x55,sizeof(out));memset(out2,0x55,sizeof(out2));
            memset(right,0x55,sizeof(right));memset(right2,0x55,sizeof(right2));
            sp_pcm(&s,out,rp);sp_pcm_reference(&r,out2,rp2);
            for(i=0;i<32;i++){
                if(out[i]!=out2[i] || (rp && right[i]!=right2[i])){
                    printf("FAIL case %u block %u sample %u: play %u ch %u step %u br %u over %u window %u..%u loop %u\n",
                           k,block,i,play,channels,s.step,s.br,s.control[7]>>8,start,end,loop);
                    return 1;
                }
            }
            same(&s,&r);runs++;
        }
        cases++;
    }
    printf("PASS: sampler PCM runs equal the per-sample model (%u cases, %u blocks)\n",cases,runs);
    return 0;
}
