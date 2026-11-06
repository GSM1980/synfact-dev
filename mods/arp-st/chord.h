/* SPDX-License-Identifier: GPL-2.0-or-later
 * OS 1.41 chord lanes of an audio track: the Digitone's trig NOTE and
 * NOT2..NOT4, dormant on the Syntakt's audio tracks. The sequencer's copy
 * (43bd7430, 499h bytes a track) and the UI pattern model's track data share
 * this layout; POLY reads the same lanes. Per step, 255 inherits the track
 * default. NOTE is a MIDI note; NOT2..NOT4 an interval + 64, where 64 and
 * anything above 127 are OFF, and 0 too here (POLY plays it as -64): zeroed
 * lanes never add a note five octaves down. The track model's native setters
 * (the TRIG page's own, with its notification) take (track model, value) for
 * a default and (track model, step, value) for a step, which they refuse past
 * the track length. Shaped to move to the core with POLY's reader
 * (docs/CORE-CONTRIBUTING.md).
 */
#ifndef SYNFACT_ST_CHORD_H
#define SYNFACT_ST_CHORD_H
#define ST_CHORD_TRACK_BYTES 0x499u
#define ST_CHORD_STEPS 64u
#define ST_CHORD_STEP(lane,step) (0x2c0u+(lane)*0x40u+(step))
#define ST_CHORD_DEFAULT(lane) (0x3c5u+(lane))
#define ST_CHORD_INHERIT 255u
#define ST_CHORD_OFF 64u
#define ST_CHORD_SET_DEFAULT {0x40027552u,0x400275d6u,0x4002765au,0x400276deu}
#define ST_CHORD_SET_STEP {0x4002817au,0x400281f4u,0x4002826eu,0x400282e8u}
/* The lane's value at a step (any step past 63: the default). */
static __inline__ unsigned int st_chord_lane(const volatile unsigned char *track,unsigned int lane,unsigned int step)
{
    unsigned int raw=step<ST_CHORD_STEPS?track[ST_CHORD_STEP(lane,step)]:ST_CHORD_INHERIT;
    return raw==ST_CHORD_INHERIT?track[ST_CHORD_DEFAULT(lane)]:raw;
}
/* NOT2..NOT4 at a step as nonzero semitone intervals, in lane order;
 * returns how many are on. */
static __inline__ unsigned int st_chord_intervals(const volatile unsigned char *track,unsigned int step,int *out)
{
    unsigned int lane,raw,count=0;
    for(lane=1;lane<4;lane++){
        raw=st_chord_lane(track,lane,step);
        if(raw && raw!=ST_CHORD_OFF && raw<=127u)out[count++]=(int)raw-(int)ST_CHORD_OFF;
    }
    return count;
}
#endif
