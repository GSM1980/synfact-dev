/* SPDX-License-Identifier: GPL-2.0-or-later
 * Target-side check of the samplers' PCM runs (samplers-st 1.4), with the
 * hand-written loops the host test cannot run: sp_pcm against the per-sample
 * model (sp_pcm_reference), both built for the target, block after block,
 * output and cursor, in analog-emu's cfbench
 * (tests/test_st_stereo_natives_emu.py). The same random windows, loops,
 * steps and modes as tests/test_st_sampler_fast.c. Never in a bank.
 */
#define SP_PCM_REFERENCE
#include "../mods/samplers-st/audio/sampler.c"
#include "../mods/samplers-st/audio/one-shot.c"
#include "../mods/samplers-st/audio/werp.c"
#include "../mods/samplers-st/audio/repitch.c"
#include "../mods/samplers-st/audio/slice.c"
#include "../mods/samplers-st/audio/voice-stereo.c"
int *st_stereo_claim(unsigned int track){(void)track;return 0;}
/* struct copies below; the bank has no C library */
void *memcpy(void *to,const void *from,unsigned long n)
{unsigned char *t=to;const unsigned char *f=from;while(n--)*t++=*f++;return to;}
static short pcm[2*6000];
static unsigned seed;
static unsigned rnd(unsigned n){seed=seed*1103515245u+12345u;return ((seed>>8)%n);}
static struct sp_state bench_state;
static sp_s32 bout[32],bright[32];
/* (differing words << 16) | blocks compared */
unsigned check_pcm(unsigned cases,unsigned start)
{
    unsigned i,k,block,bad=0,blocks=0;
    seed=start;
    for(i=0;i<2*6000;i++)pcm[i]=(short)(rnd(65536)-32768);
    pcm[100]=-32768;pcm[101]=32767;
    for(k=0;k<cases;k++){
        struct sp_state s,r;sp_s32 out[32],right[32],out2[32],right2[32];
        unsigned channels=1+rnd(2),frames=channels==2?6000:12000,play=rnd(4),want=rnd(3);
        unsigned start,end,loop;
        for(i=0;i<sizeof(s);i++)((unsigned char *)&s)[i]=0;
        s.source.pcm=pcm;s.source.channels=channels;
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
            for(i=0;i<32;i++)out[i]=out2[i]=right[i]=right2[i]=0x55555555;
            sp_pcm(&s,out,rp);sp_pcm_reference(&r,out2,rp2);
            for(i=0;i<32;i++)bad+=(out[i]!=out2[i])+(rp && right[i]!=right2[i]);
            bad+=(s.frame!=r.frame)+(s.fraction!=r.fraction)+(s.active!=r.active)+(s.wrapped!=r.wrapped);
            blocks++;
        }
    }
    return bad<<16|(blocks&0xffffu);
}
/* One block for instruction counts: channels 1 or 2, a forward one-shot at
 * unity pitch in the middle of a long sample; right only for stereo. */
void bench_pcm_setup(unsigned channels)
{
    unsigned i;struct sp_state *s=&bench_state;
    for(i=0;i<sizeof(*s);i++)((unsigned char *)s)[i]=0;
    s->source.pcm=pcm;s->source.channels=channels;s->source.frames=channels==2?6000:12000;
    s->step=4096;s->control[1]=3<<8;
    sp_window(s,0,s->source.frames,0,3,1);s->frame=1000;
}
void bench_pcm(void){sp_pcm(&bench_state,bout,bench_state.source.channels==2?bright:0);}
