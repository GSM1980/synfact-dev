/* SPDX-License-Identifier: GPL-2.0-or-later
 * The tracks the OS 1.42 sequencer mutes, as its own trig filter decides
 * (400a3998..400a39b8 and 400a421a..400a422c, in the MAIN IRQ):
 *   track mute 80003048, or while any track is soloed (8000304c nonzero)
 *   every track not soloed; and the pattern/song mute 43bdd828, a 16-bit
 *   mask the pattern change (40019bfa) and song rows (4002332c) set through
 *   400a30b2. Bit t is track t (0..11 audio, 12 FX).
 * Shaped for the core: no module state, readable from the MAIN IRQ. */
#ifndef ST_MUTE_H
#define ST_MUTE_H
#define ST_MUTE_TRACK   (*(const volatile unsigned int *)0x80003048u)
#define ST_MUTE_SOLO    (*(const volatile unsigned int *)0x8000304cu)
#define ST_MUTE_PATTERN (*(const volatile unsigned short *)0x43bdd828u)

static __inline__ unsigned int st_muted_tracks(void)
{
    unsigned int solo = ST_MUTE_SOLO;
    return (solo ? ~solo : ST_MUTE_TRACK) | ST_MUTE_PATTERN;
}
#endif
