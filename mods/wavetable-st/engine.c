/* SPDX-License-Identifier: GPL-2.0-or-later
 * SP WAVETBL's oscillator (engine.h). Every step has its twin in
 * tests/wavetable_st_reference.py; keep them equal, operation for operation.
 */
#include "engine.h"
#include "tables.h"
#include <syntakt/wavetable.h>

wt_u32 wt_increment(wt_s32 pitch8)
{
    wt_u32 p,octave,r,i,f,v;
    if(pitch8<0)pitch8=0;
    if(pitch8>135*256)pitch8=135*256;
    p=(wt_u32)pitch8;octave=p/3072u;r=p%3072u;i=r>>4;f=r&15u;
    v=wt_inc[i]+(((wt_inc[i+1]-wt_inc[i])*f)>>4);
    return octave>=8u?v<<(octave-8u):v>>(8u-octave);
}
wt_u32 wt_level(wt_u32 inc)
{
    wt_u32 k;
    for(k=0;k<8u;k++)if(inc<=wt_limit[k])return k;
    return 8u;
}
wt_u32 wt_sample_starts(wt_u32 frames,wt_u32 stereo,wt_u32 *starts)
{
    wt_u32 n=frames/WT_SAMPLE_WAVE,w=n>64u?64u:n,i,k;
    for(i=0;i<w;i++){k=n>64u?(i*(n-1u)+31u)/63u:i;starts[i]=(k*WT_SAMPLE_WAVE)<<(stereo?1u:0u);}
    return w;
}
static wt_u32 next_random(struct wt_osc *o)
{
    wt_u32 s=o->seed;
    s^=s<<13;s^=s>>17;s^=s<<5;o->seed=s;
    return s;
}
/* A bipolar random value, -32767..32767. */
static wt_s32 random_value(struct wt_osc *o)
{
    wt_s32 v=(wt_s32)(next_random(o)>>16)-32768;
    return v<-32767?-32767:v;
}
void wt_osc_init(struct wt_osc *o,wt_u32 seed)
{
    o->phase=o->anim=0;o->seed=seed?seed:1u;o->fresh=1;
    o->pos=o->gain=0;o->held=o->from=o->to=0;
}
void wt_osc_trig(struct wt_osc *o)
{
    o->phase=0;o->anim=0;o->fresh=1;
    o->held=random_value(o);o->from=o->to=random_value(o);
}
static wt_u32 anim_increment(const struct wt_osc_in *in)
{
    wt_u32 spd=in->spd>>8,tempo=in->tempo,k;
    if(spd>127u)spd=127u;
    if((in->anim>>8)<WT_LOOP_RAMP_DOWN)return wt_shot[spd];
    if(tempo<3600u || tempo>36000u)tempo=WT_TEMPO_DEFAULT;
    k=wt_loop[spd*17u>>7];
    return tempo*(k>>8)+((tempo*(k&255u))>>8);
}
/* ANIM's value for this block, Q15: 0..32767, or -32767..32767 for Random,
 * Sample and Hold and Wavetable. Advances the ANIM phase by one block. */
static wt_s32 anim_value(struct wt_osc *o,const struct wt_osc_in *in,wt_u32 wave)
{
    wt_u32 shape=in->anim>>8,inc=anim_increment(in),u,wrapped=0;
    wt_s32 v,v2;
    if(shape>=WT_SHAPES)shape=WT_SHAPES-1u;
    if(shape<WT_LOOP_RAMP_DOWN){
        u=o->anim+inc;o->anim=u<o->anim?0xffffffffu:u;   /* one shot: holds its end */
    }else{
        u=o->anim+inc;wrapped=u<o->anim;o->anim=u;
    }
    u=o->anim>>17;                                       /* 0..32767 */
    switch(shape){
    case WT_EXP_DOWN:v=32767-(wt_s32)u;v2=(v*v)>>15;return (v2*v2)>>15;
    case WT_RAMP_DOWN:case WT_LOOP_RAMP_DOWN:return 32767-(wt_s32)u;
    case WT_TRI:case WT_LOOP_TRI:return u<16384u?(wt_s32)(u*2u):(wt_s32)((32767u-u)*2u);
    case WT_RAMP_UP:case WT_LOOP_RAMP_UP:return (wt_s32)u;
    case WT_EXP_UP:v=(wt_s32)u;v2=(v*v)>>15;return (v2*v2)>>15;
    case WT_LOOP_SQUARE:return u<16384u?32767:0;
    case WT_RANDOM:
        if(wrapped){o->from=o->to;o->to=random_value(o);}
        return o->from+(((o->to-o->from)*(wt_s32)u)>>15);
    case WT_SAMPLE_HOLD:
        if(wrapped)o->held=random_value(o);
        return o->held;
    default:                                              /* WT_WAVETABLE: its own wave, level 3 */
        if(!in->waves)return 0;
        if(in->starts)return in->pcm[in->starts[wave]+((o->anim>>21)<<in->shift)];
        return in->pcm[st_wt_offset(in->waves,3,wave)+(o->anim>>24)];
    }
}
/* The block's samples. sh (0, or 1 for a stereo sample's L) is a constant at
 * each call, so the table's loops keep plain indexing. */
static __attribute__((always_inline)) inline wt_u32 render(const struct wt_osc_in *in,wt_s32 *acc,
    const wt_s16 *base,wt_u32 shift,wt_u32 size,wt_u32 last,wt_u32 ph,
    wt_s32 pos,wt_s32 gain,wt_s32 dpos,wt_s32 dgain,const wt_u32 sh)
{
    wt_u32 i,xf,idx,fr,wave,mask=size-1u,inc=in->inc;
    wt_s32 a,b,s0,s1;const wt_s16 *w0,*w1;
    if(!dpos){
        /* The position holds over the block (no ANIM on it, or a settled one):
         * the same samples as the loop below, with the wave chosen once. */
        if(pos<=0){wave=0;xf=0;}
        else{wave=(wt_u32)pos>>16;xf=((wt_u32)pos>>1)&0x7fffu;
            if(wave>=last){wave=last;xf=0;}}
        if(in->starts){w0=base+in->starts[wave];w1=xf?base+in->starts[wave+1u]:w0;}
        else{w0=base+wave*size;w1=w0+size;}
        if(!xf)for(i=0;i<WT_BLOCK;i++){
            gain+=dgain;ph+=inc;idx=ph>>shift;fr=(ph>>(shift-15u))&0x7fffu;
            a=w0[idx<<sh];b=w0[((idx+1u)&mask)<<sh];s0=a+(((b-a)*(wt_s32)fr)>>15);
            acc[i]+=s0*(gain<0?0:gain);
        }
        else for(i=0;i<WT_BLOCK;i++){
            gain+=dgain;ph+=inc;idx=ph>>shift;fr=(ph>>(shift-15u))&0x7fffu;
            a=w0[idx<<sh];b=w0[((idx+1u)&mask)<<sh];s0=a+(((b-a)*(wt_s32)fr)>>15);
            a=w1[idx<<sh];b=w1[((idx+1u)&mask)<<sh];s1=a+(((b-a)*(wt_s32)fr)>>15);
            s0+=((s1-s0)*(wt_s32)xf)>>15;
            acc[i]+=s0*(gain<0?0:gain);
        }
        return ph;
    }
    for(i=0;i<WT_BLOCK;i++){
        pos+=dpos;gain+=dgain;
        if(pos<=0){wave=0;xf=0;}                         /* a ramp's rounding can dip below 0 */
        else{wave=(wt_u32)pos>>16;xf=((wt_u32)pos>>1)&0x7fffu;
            if(wave>=last){wave=last;xf=0;}}
        ph+=inc;idx=ph>>shift;fr=(ph>>(shift-15u))&0x7fffu;
        w0=in->starts?base+in->starts[wave]:base+wave*size;
        a=w0[idx<<sh];b=w0[((idx+1u)&mask)<<sh];s0=a+(((b-a)*(wt_s32)fr)>>15);
        if(xf){
            w1=in->starts?base+in->starts[wave+1u]:w0+size;
            a=w1[idx<<sh];b=w1[((idx+1u)&mask)<<sh];s1=a+(((b-a)*(wt_s32)fr)>>15);
            s0+=((s1-s0)*(wt_s32)xf)>>15;
        }
        acc[i]+=s0*(gain<0?0:gain);
    }
    return ph;
}
static wt_s32 clamp(wt_s32 v){return v<0?0:v>0x7f00?0x7f00:v;}
void wt_osc_block(struct wt_osc *o,const struct wt_osc_in *in,wt_s32 *acc)
{
    wt_u32 w,last,level=0,shift,size,wave,inc=in->inc,sh;
    wt_s32 m,p,g,pos,gain,dpos,dgain;
    const wt_s16 *base;
    if(!in->waves){o->phase+=inc*WT_BLOCK;o->fresh=1;return;}
    last=in->waves-1u;
    wave=(wt_u32)o->pos>>16;if(wave>last)wave=last;
    m=anim_value(o,in,wave);
    p=clamp((wt_s32)in->pos+(((((wt_s32)in->apos-0x4000)*2)*m)>>15));
    g=clamp((wt_s32)in->lev+(((((wt_s32)in->alev-0x4000)*2)*m)>>15));
    w=(wt_u32)p*last;
    pos=(wt_s32)(((w/32512u)<<16)+((w%32512u)*65536u/32512u));
    gain=g;
    if(o->fresh){o->pos=pos;o->gain=gain;o->fresh=0;}
    /* Linear ramps over the block from the last block's values; the next
     * block starts exactly on this one's targets. */
    dpos=(pos-o->pos)>>5;dgain=(gain-o->gain)>>5;
    {wt_s32 t=pos;pos=o->pos;o->pos=t;t=gain;gain=o->gain;o->gain=t;}
    if(!gain && !dgain){o->phase+=inc*WT_BLOCK;return;}
    if(in->starts){            /* a plain sample: its 2048-frame waves, one level */
        shift=21u;size=WT_SAMPLE_WAVE;base=in->pcm;sh=in->shift;
    }else{
        level=wt_level(inc);shift=21u+level;size=ST_WT_WAVE>>level;
        base=in->pcm+st_wt_offset(in->waves,level,0);sh=0;
    }
    if(sh)o->phase=render(in,acc,base,shift,size,last,o->phase,pos,gain,dpos,dgain,1u);
    else o->phase=render(in,acc,base,shift,size,last,o->phase,pos,gain,dpos,dgain,0u);
}
