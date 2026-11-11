/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/digimono-st/mono2k.c"
void dm_ref_render(struct dm_m2k_voice *,unsigned,const uint16_t *,int32_t,int32_t,int16_t *);
static uint32_t seed=0x6d6e6d;
static uint32_t next(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}

int main(void)
{
    unsigned blocks=0;
    for(unsigned n=0;n<10000;n++){
        uint32_t a=next(),b=next();
        assert(wide_mul(a,b)==(uint64_t)a*b);
        assert(signed_mul((int32_t)a,(int32_t)b)==(int64_t)(int32_t)a*(int32_t)b);
        int32_t signal=(int32_t)a>>7,gain=(int32_t)(b%32769);
        assert(gain_q15(signal,gain)==((int64_t)signal*gain)>>15);
        uint32_t depth=b%293811;
        assert(phase_depth(signal,depth)==(uint32_t)(((int64_t)signal*depth)>>8));
        assert(mul64_sh((int64_t)(int32_t)a*64,(int32_t)(b&131071),16)==
               (((int64_t)(int32_t)a*64)*(int32_t)(b&131071))>>16);
    }
    for(unsigned type=DM_SID;type<=DM_FMDYN;type++)for(unsigned trial=0;trial<24;trial++){
        struct dm_m2k_voice actual,reference;
        uint16_t p[7];int16_t out[32],ref[32];
        for(unsigned i=0;i<7;i++)p[i]=trial<2?(trial?0x7f00:0):(next()%0x7f01);
        dm_m2k_init(&actual);dm_m2k_trig(&actual,type,p);reference=actual;
        for(unsigned n=0;n<48;n++){
            int32_t pitch=(trial*397+n*13)%(140*64+1),previous=48*64+trial*128;
            if(n==24){for(unsigned i=0;i<7;i++)p[i]=next()%0x7f01;}
            dm_m2k_render(&actual,type,p,pitch,previous,out);
            dm_ref_render(&reference,type,p,pitch,previous,ref);
            assert(!memcmp(out,ref,sizeof out));
            assert(!memcmp(&actual,&reference,sizeof actual));blocks++;
        }
    }
    /* Independent physical-unit check: with modulation off, every FM+
     * carrier must be A4 at 48 kHz, not pitched up by the old sample rate. */
    for(unsigned type=DM_FMSTAT;type<=DM_FMDYN;type++){
        struct dm_m2k_voice v;uint16_t p[7]={0};int16_t out[32];
        unsigned crossings=0;int last=0;
        if(type==DM_FMDYN){p[1]=p[3]=64<<8;}
        dm_m2k_init(&v);dm_m2k_trig(&v,type,p);
        for(unsigned n=0;n<1500;n++){
            dm_m2k_render(&v,type,p,69*64,60*64,out);
            for(unsigned i=0;i<32;i++){if(last<0 && out[i]>=0)crossings++;last=out[i];}
        }
        assert(crossings>=439 && crossings<=441);
    }
    /* All five SID wave selections are audible at their documented settings;
     * PW=64 is the special silent pulse position, not a missing parameter. */
    for(unsigned wave=0;wave<5;wave++){
        struct dm_m2k_voice v;uint16_t p[7]={0};int16_t out[32];unsigned peak=0;
        p[2]=127<<8;p[3]=(uint16_t)(((wave*128+4)/5)<<8);
        dm_m2k_init(&v);dm_m2k_trig(&v,DM_SID,p);
        for(unsigned n=0;n<64;n++){
            dm_m2k_render(&v,DM_SID,p,60*64,48*64,out);
            for(unsigned i=0;i<32;i++){unsigned a=out[i]<0?-out[i]:out[i];if(a>peak)peak=a;}
        }
        assert(peak>1000);
    }
    printf("PASS: %u MONO2K/native-64-bit reference blocks, full Q8 controls, all four engines, wide arithmetic, 48 kHz A4 tuning and five SID waves\n",blocks);
    return 0;
}
