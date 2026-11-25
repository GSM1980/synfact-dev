/* SPDX-License-Identifier: GPL-2.0-or-later
 * SY WAVETABLE on the AUDIO CPU: eight independent voices, 48 kHz, 32 frames.
 * The native update prepares declick and gains (as SY MnM does), then two
 * wavetable oscillators (engine.c) replace the PCM. The stock render filter
 * is left out: this is a 48 kHz synth, not a resampled PCM reader. FLTR, AMP
 * and FX follow in the OS.
 */
#include <syntakt/machines.h>
#include "engine.h"

/* Tests substitute only the firmware boundaries, never the engine. */
#ifndef WT_STOCK_BOUNDARIES
#define WT_TRACK_BASE 0x80000000u
#define WT_EXTENDED st_machine_extended
#define WT_SAMPLE st_machine_sample
#define WT_WAVES st_machine_sample_wavetable
#define WT_CHANNELS st_machine_sample_channels
#define WT_NATIVE_UPDATE(p,v,n) ((void (*)(unsigned,void *,const void *))0x400097dcu)(p,v,n)
#define WT_DECLICK(v) ((void (*)(void *))0x40003d42u)(v)
#define WT_GAIN(a,v) ((void (*)(void *,void *))0x400045a0u)(a,v)
#define WT_MIX(a,b,o,l,v) ((void (*)(void *,void *,void *,void *,void *))0x400049f4u)(a,b,o,l,v)
#define WT_AMP_IDLE(t) (((const volatile unsigned *)0x800049e0u)[3*(t)]==0 && ((const volatile unsigned *)0x800049e0u)[3*(t)+2]==0)
#define WT_CURSOR st_machine_cursor_publish
#endif
#define WT_SIGNATURE 0x57540101u
#define WT_REST_BLOCKS 32u
/* Extended controls 8..16, in this order (main.c): DTUN, then osc 2's LEV,
 * POS, SLOT, SPD, A.LEV, A.POS, ANIM, then the hidden tempo. */
#define WT_EXT 9u
static const unsigned short wt_ext_default[WT_EXT]={0x4000,0,0,0x4000,0x4000,0x4000,0x4000,0,WT_TEMPO_DEFAULT};
/* A plain sample's wave starts, kept while the slot holds the same PCM. */
struct wt_source {const short *pcm;unsigned frames,channels,waves,starts[64];};
struct wt_track {
    struct wt_osc osc[2];
    struct wt_osc_in in[2];
    struct wt_source source[2];
    unsigned loaded,live,quiet,frames[2];
    unsigned short ext[WT_EXT];
};
static struct wt_track wt_tracks[8];

static int wt_track_of(const void *voice)
{
    unsigned offset=(unsigned)voice-WT_TRACK_BASE;
    return offset<8u*0x708u && offset%0x708u==0?(int)(offset/0x708u):-1;
}
/* The slot's source: a canonical wavetable, else a plain sample played as
 * one (2048-frame waves, at most 64, as the Tonverk takes a sample), else
 * nothing (an empty slot or a sample shorter than one wave: silent). */
static void wt_table(struct wt_osc_in *in,struct wt_source *src,unsigned *frames,unsigned raw)
{
    unsigned slot=raw>>8,channels;const struct st_pool_sample *s;
    if(slot>127u)slot=127u;
    s=WT_SAMPLE(slot);in->starts=0;in->shift=0;
    in->pcm=s?s->pcm:0;*frames=s?s->frames:0;
    in->waves=in->pcm?WT_WAVES(slot):0;
    if(!in->pcm || in->waves)return;
    channels=WT_CHANNELS(slot);
    if(src->pcm!=in->pcm || src->frames!=s->frames || src->channels!=channels){
        src->pcm=in->pcm;src->frames=s->frames;src->channels=channels;
        src->waves=wt_sample_starts(s->frames,channels==2,src->starts);
    }
    in->waves=src->waves;in->starts=src->starts;in->shift=channels==2;
}
void wt_update(unsigned pitch,void *voice,const unsigned short *params)
{
    unsigned *v=voice,i,trig;
    unsigned short native[71];
    const unsigned short *ext;
    struct wt_track *s;
    int t=wt_track_of(voice),note;
    if(t<0)return;
    s=wt_tracks+t;trig=v[0x38/4]!=0;
    if(v[0x6f8/4]!=WT_SIGNATURE || !s->loaded){
        wt_osc_init(s->osc,0x9e3779b9u*(2u*(unsigned)t+1u));
        wt_osc_init(s->osc+1,0x9e3779b9u*(2u*(unsigned)t+2u));
        for(i=0;i<WT_EXT;i++)s->ext[i]=wt_ext_default[i];
        s->loaded=1;s->live=0;s->quiet=0;
    }
    /* Null until MAIN's values for this machine arrived: keep the last ones. */
    ext=WT_EXTENDED(params);
    if(ext)for(i=0;i<WT_EXT;i++)s->ext[i]=ext[i];
    /* Osc 1 from the native words: TUNE LEV POS SLOT SPD A.LEV A.POS ANIM. */
    note=(int)(pitch>>8)+((int)params[0x12]-0x4000);
    s->in[0].inc=wt_increment(note);
    s->in[0].lev=params[0x13];s->in[0].pos=params[0x14];wt_table(s->in,s->source,s->frames,params[0x15]);
    s->in[0].spd=params[0x16];s->in[0].alev=params[0x17];s->in[0].apos=params[0x1a];s->in[0].anim=params[0x19];
    /* Osc 2: DTUN (+-64 = +-1 semitone) against osc 1, then its own seven. */
    s->in[1].inc=wt_increment(note+(((int)s->ext[0]-0x4000)>>6));
    s->in[1].lev=s->ext[1];s->in[1].pos=s->ext[2];wt_table(s->in+1,s->source+1,s->frames+1,s->ext[3]);
    s->in[1].spd=s->ext[4];s->in[1].alev=s->ext[5];s->in[1].apos=s->ext[6];s->in[1].anim=s->ext[7];
    s->in[0].tempo=s->in[1].tempo=s->ext[8];
    if(trig){wt_osc_trig(s->osc);wt_osc_trig(s->osc+1);s->live=1;s->quiet=0;}
    /* Sleep only after the native AMP has finished; the next trig wakes the voice. */
    if(!trig && WT_AMP_IDLE(t)){
        if(s->quiet<WT_REST_BLOCKS)s->quiet++;
        if(s->quiet==WT_REST_BLOCKS)s->live=0;
    }else s->quiet=0;
    for(i=0;i<71;i++)native[i]=params[i];
    native[0x12]=native[0x13]=native[0x14]=0x4000;
    native[0x15]=0;native[0x16]=native[0x17]=0x4000;
    native[0x18]=0;native[0x19]=0x7f00;
    WT_NATIVE_UPDATE(60u<<16,voice,native);
    v[2]=0x3fffffffu;v[3]=0;
    if(trig){v[6]=0x3fffffffu;v[7]=0;}
    v[0x6d4/4]=0;v[0x6d8/4]=0;v[0x6e8/4]=0;
    /* The stock reset clears this word; it is not passed to the PCM render filter. */
    v[0x6f8/4]=WT_SIGNATURE;
}
void wt_render(void *out,void *voice)
{
    int acc[32],b[32],locals[2]={0,1};
    int t=wt_track_of(voice);unsigned i;
    struct wt_track *s;
    if(t<0){for(i=0;i<32;i++)((int *)out)[i]=0;return;}
    s=wt_tracks+t;
    for(i=0;i<32;i++){acc[i]=0;b[i]=0;}
    if(s->loaded && s->live){wt_osc_block(s->osc,s->in,acc);wt_osc_block(s->osc+1,s->in+1,acc);}
    /* The display's live position (page.c): osc 1 in track t's cursor record,
     * osc 2 in record 8 + t; the core lends it only for this slot's PCM. The
     * position is the wave position, Q8 waves x 8, below the slot's frames
     * for a table (waves x 4088) and a sample (waves x 2048) alike. */
    for(i=0;i<2;i++){
        const struct wt_osc_in *in=s->in+i;unsigned on=s->loaded && s->live && in->waves;
        WT_CURSOR((unsigned)t+8u*i,on?(unsigned)in->pcm:0,on?s->frames[i]:0,
                  on?(unsigned)s->osc[i].pos>>5:0,on);
    }
    WT_DECLICK(voice);
    WT_GAIN(acc,voice);
    WT_MIX(acc,b,out,locals,voice);
}
