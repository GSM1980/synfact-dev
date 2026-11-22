/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DM_DPRO_VOICE_H
#define DM_DPRO_VOICE_H
#include <stdint.h>
/* Public TYPE values, appended after FM+DYN (11). */
enum { DM_DPWAVE=12, DM_DPDDRW, DM_DPDENS };
#define DM_IS_DPRO(t) ((t)>=DM_DPWAVE && (t)<=DM_DPDENS)
/* Signals Q26 (three guard bits above the original Q23 words); the native
 * clock is the Monomachine's 44.1 kHz / 16 frames, resampled to 48 kHz. */
struct dm_dp_voice {
    uint32_t u[4];                 /* WAVE's phase; DDRW's two waves; DENS's four oscillators */
    int32_t pos,sync;              /* WAVE: the WP sweep (24 bits) and the sync counter */
    int32_t now[2],old[2],fade[2]; /* DDRW: each wave, the one it fades from, the fade (Q23) */
    int32_t mix;                   /* DDRW: the ramped MIX (Q26) */
    int32_t dec[18];               /* WAVE's decimator (5) or the 2:1 allpass (18) */
    uint32_t inc[4],inc_b[2],extra;/* this render's increments; WAVE: sync period, DENS: muted bits */
    uint32_t fresh;                /* the machine's init runs on its first block */
    int32_t pending[16],history[16];
    uint32_t cursor,write,frac,need;
};
void dm_dp_init(struct dm_dp_voice *);
void dm_dp_trig(struct dm_dp_voice *,unsigned type,const uint16_t *p);
/* F: the Monomachine pitch word (2048 an octave); pre: the previous track's F (0: none). */
int32_t dm_dp_word(unsigned type,int32_t pitch);
void dm_dp_increments(struct dm_dp_voice *,unsigned type,const uint16_t *p,int32_t F,int32_t pre);
void dm_dp_half(struct dm_dp_voice *,unsigned type,const uint16_t *p,int32_t *out);
void dm_dp_render(struct dm_dp_voice *,unsigned type,const uint16_t *p,int32_t pitch,
                  int32_t previous_pitch,int16_t *out);
#endif
