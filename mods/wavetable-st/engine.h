/* SPDX-License-Identifier: GPL-2.0-or-later
 * SP WAVETBL's oscillator: portable integer C, no firmware calls. The AUDIO
 * glue (audio.c) and the host test (tests/test_wavetable_st.py, against
 * tests/wavetable_st_reference.py) both build this file. No FPU, no 64-bit
 * arithmetic, no division per sample.
 */
#ifndef WT_ENGINE_H
#define WT_ENGINE_H
typedef unsigned int wt_u32;
typedef int wt_s32;
typedef short wt_s16;
#define WT_BLOCK 32u
#define WT_SHAPES 12u
#define WT_TEMPO_DEFAULT 14400u    /* 120 BPM x 120 */
enum {WT_EXP_DOWN,WT_RAMP_DOWN,WT_TRI,WT_RAMP_UP,WT_EXP_UP,          /* one shot */
      WT_LOOP_RAMP_DOWN,WT_LOOP_TRI,WT_LOOP_SQUARE,WT_LOOP_RAMP_UP,   /* looping, tempo divisions */
      WT_RANDOM,WT_SAMPLE_HOLD,WT_WAVETABLE};
/* One oscillator's state. pos is the wave position, Q16 waves; gain Q15. */
struct wt_osc {
    wt_u32 phase,anim,seed,fresh;
    wt_s32 pos,gain,held,from,to;
};
/* What one block of one oscillator needs. Controls are the Sound's Q8 words
 * (0..0x7f00; A.LEV and A.POS centred on 0x4000). pcm/waves: a canonical
 * wavetable (wavetable.h, starts null), or waves 0 for none.
 * A plain sample played as a wavetable (as the Tonverk does): starts[w] is
 * where wave w (2048 frames) begins in pcm, in 16-bit words, and shift is 1
 * for a stereo sample (frames are L, R pairs: L is read) or 0. One level, no
 * band limit: high notes alias. */
#define WT_SAMPLE_WAVE 2048u
struct wt_osc_in {
    const wt_s16 *pcm;
    wt_u32 waves,inc,pos,lev,spd,alev,apos,anim,tempo;
    const wt_u32 *starts;
    wt_u32 shift;
};
/* Phase increment per sample for a pitch in Q8 semitones (MIDI note << 8). */
wt_u32 wt_increment(wt_s32 pitch8);
/* The band-limited level (0..8) for a phase increment. */
wt_u32 wt_level(wt_u32 inc);
/* A plain sample's waves, as the Tonverk takes a sample: 2048-frame waves,
 * at most 64, evenly spaced when there are more (the first and the last kept).
 * Fills starts (in 16-bit words; stereo: frame pairs) and returns the count,
 * 0 for a sample shorter than one wave. */
wt_u32 wt_sample_starts(wt_u32 frames,wt_u32 stereo,wt_u32 *starts);
void wt_osc_init(struct wt_osc *o,wt_u32 seed);
/* A new note: phases and ANIM restart; the next block starts on its values. */
void wt_osc_trig(struct wt_osc *o);
/* One block: adds sample x gain (Q30, at most 2^30) to acc[0..31]. */
void wt_osc_block(struct wt_osc *o,const struct wt_osc_in *in,wt_s32 *acc);
#endif
