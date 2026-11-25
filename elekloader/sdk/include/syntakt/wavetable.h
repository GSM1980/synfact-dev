/* SPDX-License-Identifier: GPL-2.0-or-later
 * Core 2.8: canonical wavetables in HZFS and the project pool
 * (docs/SP-WAVETBL.md, section 4; mods/core-st/HZFS.md, "Wavetables").
 *
 * A wavetable is a mono HZFS sample with entry flag ST_HZ_WAVETABLE. Its PCM
 * holds W waves (1..64) at nine band-limited levels: level k has waves of
 * 2048 >> k samples, the waves of a level are contiguous, and levels follow
 * each other (level-major). So a wave uses 4088 samples over all levels and
 * frames == W * 4088. Any other count is an ordinary mono sample.
 * Both flag bits set reads as stereo, as before core 2.8.
 */
#ifndef ELEKLOADER_ST_WAVETABLE_H
#define ELEKLOADER_ST_WAVETABLE_H
#define ST_HZ_WAVETABLE 2u          /* HZFS entry flag */
#define ST_WT_WAVE 2048u            /* samples of a level 0 wave */
#define ST_WT_LEVELS 9u             /* 2048, 1024, ..., 8 */
#define ST_WT_FRAMES 4088u          /* samples of one wave over all levels */
#define ST_WT_MAX 64u               /* waves in a table */
/* The pool's per-slot kind byte (machines.h st_machine_sample_channels):
 * 1 mono, 2 stereo, this value a mono wavetable. Readers that test == 2 see
 * mono. */
#define ST_POOL_KIND_WAVETABLE 0x81u
/* Waves in a table with these entry flags and frames (16-bit samples), or 0. */
static __inline__ unsigned int st_wt_waves(unsigned int flags,unsigned int frames)
{
    unsigned int w;
    if((flags&3u)!=ST_HZ_WAVETABLE || !frames || frames%ST_WT_FRAMES)return 0;
    w=frames/ST_WT_FRAMES;return w<=ST_WT_MAX?w:0;
}
/* First sample of a wave at a level, from the start of the PCM. */
static __inline__ unsigned int st_wt_offset(unsigned int waves,unsigned int level,unsigned int wave)
{return waves*(2u*ST_WT_WAVE-(2u*ST_WT_WAVE>>level))+wave*(ST_WT_WAVE>>level);}
#endif
