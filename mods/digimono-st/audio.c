/* SPDX-License-Identifier: GPL-2.0-or-later
 * AUDIO CPU: eight independent DDR voices, 48 kHz / 32 frames, no UI work.
 * Native update prepares declick/gains, then our oscillator replaces the PCM.
 * The stock render filter (TwinShot anti-imaging) is deliberately omitted:
 * this is a 48 kHz synth, not a resampled PCM reader. FLTR/AMP/FX follow in OS.
 */
#include <syntakt/machines.h>
#include "types.h"

/* Test substitutes only the firmware boundaries, never the engine. */
#ifndef DM_STOCK_BOUNDARIES
#define DM_TRACK_BASE ((uintptr_t)0x80000000u)
#define DM_EXTENDED st_machine_extended
#define DM_NATIVE_UPDATE(p,v,n) ((void (*)(unsigned,void *,const void *))0x400097dcu)(p,v,n)
#define DM_DECLICK(v) ((void (*)(void *))0x40003d42u)(v)
#define DM_GAIN(a,v) ((void (*)(void *,void *))0x400045a0u)(a,v)
#define DM_MIX(a,b,o,l,v) ((void (*)(void *,void *,void *,void *,void *))0x400049f4u)(a,b,o,l,v)
#define DM_AMP_IDLE(t) (((const volatile unsigned *)0x800049e0u)[3*(t)]==0 && ((const volatile unsigned *)0x800049e0u)[3*(t)+2]==0)
#endif
#define DM_SIGNATURE 0x444d0101u
#define DM_REST_BLOCKS 32u
struct dm_track {
    struct mono_voice engine;
    unsigned loaded,pending,live,quiet,increment;
    uint8_t p[MONO_PARAMS];
};
static struct dm_track dm_tracks[8];

static int dm_track_of(const void *voice)
{
    uintptr_t offset=(uintptr_t)voice-DM_TRACK_BASE;
    return offset<8u*0x708u && offset%0x708u==0 ? (int)(offset/0x708u) : -1;
}

void dm_update(unsigned pitch,void *voice,const unsigned short *params)
{
    static const unsigned char slots[8]={0x12,0x13,0x14,0x15,0x16,0x17,0x1a,0x19};
    unsigned *v=voice,i,trig;
    unsigned short native[71];
    const unsigned short *ext;
    struct dm_track *s;
    int t=dm_track_of(voice),note,tune;
    if(t<0)return;
    s=dm_tracks+t;trig=v[0x38/4]!=0;
    if(v[0x6f8/4]!=DM_SIGNATURE || !s->loaded) {
        mono_init(&s->engine);s->loaded=0;s->pending=DM_DEFAULT_TYPE;
        s->live=0;s->quiet=0;
    }
    ext=DM_EXTENDED(params);
    if(ext)s->pending=dm_clamp_type(ext[0]);
    /* TYPE is note-latched: no phase restarts in the middle of a held note. */
    if(!s->loaded || (trig && s->loaded!=s->pending+1u)) {
        mono_init(&s->engine);s->loaded=s->pending+1u;s->live=0;s->quiet=0;
    }
    for(i=0;i<MONO_PARAMS;i++) {
        unsigned k=params[slots[i+1]]>>8;
        s->p[i]=(uint8_t)(k>127?127:k);
    }
    tune=(int)params[slots[0]]-0x4000; /* signed Q8 semitones */
    if(tune < -0x4000)tune=-0x4000;
    if(tune > 0x3f00)tune=0x3f00;
    note=((int32_t)pitch>>9)+(tune>>1); /* Q16 -> 1/128 semitone */
    s->increment=mono_pitch_inc(note);
    if(trig) {
        mono_trig(&s->engine,dm_engine_type[s->loaded-1u]);s->live=1;s->quiet=0;
    }
    /* No Digitakt overload shedding: eco remains zero on Syntakt's AUDIO CPU.
     * Sleep only after native AMP has finished; the next trig wakes the voice. */
    if(!trig && DM_AMP_IDLE(t)) {
        if(s->quiet<DM_REST_BLOCKS)s->quiet++;
        if(s->quiet==DM_REST_BLOCKS)s->live=0;
    }else s->quiet=0;
    for(i=0;i<71;i++)native[i]=params[i];
    native[0x12]=native[0x13]=native[0x14]=0x4000;
    native[0x15]=0;native[0x16]=native[0x17]=0x4000;
    native[0x18]=0;native[0x19]=0x7f00;
    DM_NATIVE_UPDATE(60u<<16,voice,native);
    v[2]=0x3fffffffu;v[3]=0;
    if(trig){v[6]=0x3fffffffu;v[7]=0;}
    v[0x6d4/4]=0;v[0x6d8/4]=0;v[0x6e8/4]=0;
    /* Stock reset invalidates this word; not passed to the PCM render filter. */
    v[0x6f8/4]=DM_SIGNATURE;
}

void dm_render(void *out,void *voice)
{
    int32_t a[32],b[32];int16_t pcm[32];int locals[2]={0,1};
    int t=dm_track_of(voice);unsigned i;
    struct dm_track *s;
    if(t<0) {for(i=0;i<32;i++)((int32_t *)out)[i]=0;return;}
    s=dm_tracks+t;
    if(s->loaded && s->live) {
        mono_render(&s->engine,dm_engine_type[s->loaded-1u],s->p,s->increment,pcm,32);
        for(i=0;i<32;i++)a[i]=(int32_t)pcm[i]*65536; /* Q15 -> Q31 */
    }else for(i=0;i<32;i++)a[i]=0;
    for(i=0;i<32;i++)b[i]=0;
    DM_DECLICK(voice);DM_GAIN(a,voice);DM_MIX(a,b,out,locals,voice);
}
