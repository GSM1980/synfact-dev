/* SPDX-License-Identifier: GPL-3.0-only
 * AUDIO CPU: eight independent DDR voices, 48 kHz / 32 frames, no UI work.
 * Native update prepares declick/gains, then our oscillator replaces the PCM.
 * The stock render filter (TwinShot anti-imaging) is deliberately omitted:
 * this is a 48 kHz synth, not a resampled PCM reader. FLTR/AMP/FX follow in OS.
 */
#include <syntakt/machines.h>
#include <syntakt/stereo.h>
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
extern int *st_stereo_claim(unsigned) __attribute__((weak));
#define DM_STEREO_CLAIM(t) (st_stereo_claim?st_stereo_claim(t):0)
#define DM_STEREO_PREVIOUS(t) ST_STEREO_R(t)
#endif
#ifndef DM_STEREO_CLAIM
#define DM_STEREO_CLAIM(t) ((void)(t),(int *)0)
#define DM_STEREO_PREVIOUS(t) ((void)(t),(const int *)0)
#endif
#define DM_SIGNATURE 0x444d0101u
#define DM_REST_BLOCKS 32u
/* After ENS's or DENS's chorus spread stops (CHRL or CHRW 0, or another TYPE),
 * the R pass goes on this many blocks (about 85 ms) for the R chain's tails,
 * then the track is mono again. It used to go on while the voice sounded. */
#define DM_STEREO_TAIL 128u
/* Each DigiPRO TYPE on one track at a time (the unit: one is what 7 stock
 * tracks leave room for). A second track with the same TYPE stays silent,
 * unrendered, until it is the only one; the track first in the block's order
 * keeps it. MAIN's list strikes it through. */
#define DM_ONE_TRACK(type) DM_IS_DPRO(type)
struct dm_track {
    struct mono_voice engine;
    struct dm_m2k_voice m2k;
    struct dm_ens_voice ens;
    unsigned loaded,pending,live,quiet,increment,stereo_tail,blocked;
    int32_t pitch;
    uint16_t m2p[MONO_PARAMS];
    uint8_t p[MONO_PARAMS];
};
static struct dm_track dm_tracks[8];
/* the block's passes over the SY MnM tracks: a track index that does not grow
 * starts one; a track counts while dm_update sees it (as MD KIT's budget) */
static unsigned dm_round,dm_last=8,dm_seen[8];
static int dm_alone(int t,unsigned loaded)
{
    unsigned u;
    for(u=0;u<8;u++){
        const struct dm_track *o=dm_tracks+u;
        if((int)u!=t && dm_round-dm_seen[u]<=2u && !o->blocked && o->loaded==loaded)return 0;
    }
    return 1;
}

static int dm_track_of(const void *voice)
{
    uintptr_t offset=(uintptr_t)voice-DM_TRACK_BASE;
    return offset<8u*0x708u && offset%0x708u==0 ? (int)(offset/0x708u) : -1;
}
static int32_t dm_previous_pitch(const struct dm_track *s,unsigned track)
{
    unsigned prev=track?track-1:7;
    const unsigned *voice=(const unsigned *)(DM_TRACK_BASE+prev*0x708u);
    if(voice[0x6f8/4]==DM_SIGNATURE && dm_tracks[prev].loaded)return dm_tracks[prev].pitch;
    /* No public pitch service for other backends: use SID's MFRQ until the
     * preceding track runs SY MONOMACHINE. Never reuse a stale backend note. */
    return 48*64+((s->m2p[6]*24)>>8);
}
/* DPRO-WAVE's PRCH: the preceding SY MnM track's pitch, or -1 (none) */
static int32_t dm_previous_mnm(unsigned track)
{
    unsigned prev=track?track-1:7;
    const unsigned *voice=(const unsigned *)(DM_TRACK_BASE+prev*0x708u);
    return voice[0x6f8/4]==DM_SIGNATURE && dm_tracks[prev].loaded?dm_tracks[prev].pitch:-1;
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
    if((unsigned)t<=dm_last)++dm_round;
    dm_last=(unsigned)t;dm_seen[t]=dm_round;
    if(v[0x6f8/4]!=DM_SIGNATURE || !s->loaded) {
        mono_init(&s->engine);dm_m2k_init(&s->m2k);s->loaded=0;s->pending=DM_DEFAULT_TYPE;
        s->live=0;s->quiet=0;
    }
    ext=DM_EXTENDED(params);
    if(ext)s->pending=dm_clamp_type(ext[0]);
    /* TYPE is note-latched: no phase restarts in the middle of a held note. */
    if(!s->loaded || (trig && s->loaded!=s->pending+1u)) {
        mono_init(&s->engine);dm_m2k_init(&s->m2k);s->loaded=s->pending+1u;s->live=0;s->quiet=0;
        if(s->loaded==DM_ENS+1u)dm_ens_init(&s->ens);
    }
    for(i=0;i<MONO_PARAMS;i++) {
        unsigned k=params[slots[i+1]]>>8;
        s->p[i]=(uint8_t)(k>127?127:k);
        s->m2p[i]=params[slots[i+1]]>0x7f00?0x7f00:params[slots[i+1]];
    }
    tune=(int)params[slots[0]]-0x4000; /* signed Q8 semitones */
    if(tune < -0x4000)tune=-0x4000;
    if(tune > 0x3f00)tune=0x3f00;
    note=((int32_t)pitch>>9)+(tune>>1); /* Q16 -> 1/128 semitone */
    s->blocked=s->loaded && DM_ONE_TRACK(s->loaded-1u) && !dm_alone(t,s->loaded);
    s->increment=mono_pitch_inc(note);
    s->pitch=note>>1; /* MONO2K: 1/64 semitone, common TUNE already applied. */
    if(trig) {
        if(s->loaded-1u==DM_ENS)dm_ens_trig(&s->ens);
        else if(s->loaded-1u>=DM_SID)dm_m2k_trig(&s->m2k,s->loaded-1u,s->m2p);
        else mono_trig(&s->engine,dm_engine_type[s->loaded-1u]);
        s->live=1;s->quiet=0;
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
    int32_t a[32],b[32],ra[32];int16_t pcm[32],right[32];int locals[2]={0,1};
    int t=dm_track_of(voice);unsigned i;
    struct dm_track *s;
    if(t<0) {for(i=0;i<32;i++)((int32_t *)out)[i]=0;return;}
    s=dm_tracks+t;
    unsigned type=s->loaded-1u,ens=s->loaded==DM_ENS+1u;
    /* ENS and DPRO-DENS: CHRL/CHRW's chorus spreads them across the stereo. */
    /* ENS: CHRL/CHRW's chorus spreads it across the stereo. DPRO DENS has no
     * chorus of its own in this release (its CHRL/CHRW are hidden): the bank's
     * Chorus FX serves it, without a second (R) pass. dm_ens_chorus keeps the
     * chorus written for it (mono2k.c), unused. */
    unsigned chorus=ens,want_stereo=chorus && s->live && !s->blocked && s->m2p[5] && s->m2p[6];
    int *rout=0;
    if(want_stereo || s->stereo_tail){
        unsigned sounding=s->live;
        const int *previous=DM_STEREO_PREVIOUS((unsigned)t);
        if(previous)for(i=0;i<32 && !sounding;i++)sounding=previous[i]!=0 || ((const int *)out)[i]!=0;
        if(sounding)rout=DM_STEREO_CLAIM((unsigned)t);
        s->stereo_tail=!rout?0:want_stereo?DM_STEREO_TAIL:s->stereo_tail-1u;
    }
    if(s->loaded && s->live && !s->blocked) {
        if(ens)dm_ens_render(&s->ens,s->m2p,s->pitch,pcm,right);
        else if(DM_IS_DPRO(type)){
            dm_m2k_render(&s->m2k,type,s->m2p,s->pitch,dm_previous_mnm((unsigned)t),pcm);
        }else if(type>=DM_SID)dm_m2k_render(&s->m2k,type,s->m2p,s->pitch,
                                           dm_previous_pitch(s,(unsigned)t),pcm);
        else mono_render(&s->engine,dm_engine_type[s->loaded-1u],s->p,s->increment,pcm,32);
        for(i=0;i<32;i++){
            /* A bank without the shared stereo service gets a centered mono
             * fold-down; the complete bank keeps both independent channels. */
            int32_t l=pcm[i],r=chorus?right[i]:l;
            a[i]=(chorus && !rout?(l+r)>>1:l)*65536;
            if(rout)ra[i]=r*65536;
        }
    }else for(i=0;i<32;i++){a[i]=0;if(rout)ra[i]=0;}
    for(i=0;i<32;i++)b[i]=0;
    unsigned *v=voice,ramp,start[2];
    DM_DECLICK(voice);ramp=v[0x3d4/4];
    DM_GAIN(a,voice);
    if(rout){v[0x3d4/4]=ramp;DM_GAIN(ra,voice);}
    start[0]=v[0x18/4];start[1]=v[0x1c/4];
    DM_MIX(a,b,out,locals,voice);
    if(rout){v[0x18/4]=start[0];v[0x1c/4]=start[1];DM_MIX(ra,b,rout,locals,voice);}
}
