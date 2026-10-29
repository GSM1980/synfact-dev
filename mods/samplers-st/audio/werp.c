/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "sampler.h"
#define UNIT 1382400000u
/* (a * 0x636ba875) >> 31 using 16-bit limbs, without a 64-bit helper. */
static sp_u32 q31(sp_u32 a)
{
    sp_u32 hi=(a>>16)*0x636b,mid=(a>>16)*0xa875+(a&65535)*0x636b;
    sp_u32 lo=(a&65535)*0xa875,add=mid<<16,before=lo;
    lo+=add;hi+=(mid>>16)+(lo<before);return (hi<<1)|(lo>>31);
}
void sp_werp_window(struct sp_state *s,sp_u32 pitch,sp_u32 tempo,int trigger)
{
    sp_u32 segment=sp_min(s->control[4]>>8,2),mode=sp_min(s->control[5]>>8,3);
    sp_u32 bars=sp_min(s->control[6]>>8,3),play=sp_min(s->control[1]>>8,3);
    sp_u32 exponent=segment+3-bars,span=5400000u<<exponent,step,point,end;int fresh=trigger;
    (void)pitch;
    if(trigger){s->running=1;s->position=play<2?UNIT-span:0;s->segment=0;}
    else if(s->running){
        step=(tempo<<12)>>(bars+7);
        if(play>=2){
            s->position+=step;
            if((int)s->position>=(int)(UNIT-step)){if(play==3)s->running=0;else s->position=0;}
        }else{
            s->position-=step;
            if((int)s->position<=0){if(!play)s->running=0;else s->position=UNIT-span;}
        }
    }else return;
    if(s->running){
        s->segment+=(tempo<<13)>>(segment+3);
        if(s->segment>UNIT-1){s->segment-=UNIT;fresh=1;}
    }
    if(!fresh)return;
    s->phase=(2*q31(s->position))&0x7ffffffeu;
    point=sp_min((s->phase+0x400000u)>>23,255);end=sp_min(point+(1u<<exponent),256);
    point=sp_point(&s->source,point);end=sp_point(&s->source,end);
    sp_window(s,point,end,point,mode,1);
}
void sp_werp_update(sp_u32 pitch,void *voice,const unsigned short *p){sp_update(SP_WERP,pitch,voice,p);}
void sp_werp_render(void *out,void *voice){sp_render(out,voice);}
