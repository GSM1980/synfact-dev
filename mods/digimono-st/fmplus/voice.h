/* SPDX-License-Identifier: AGPL-3.0-or-later */
#ifndef DM_FMPLUS_VOICE_H
#define DM_FMPLUS_VOICE_H
#include <stdint.h>
/* Internal signals Q29, six guard bits above the original Q23 words.
 * Phase state is De-Pattern's 32-bit domain; the native clock is 44.1 kHz. */
struct dm_fm_op {uint32_t phase;int32_t ramp,prior,lp,fb;};
struct dm_fm_voice {
    struct dm_fm_op op[3];
    uint32_t phase;
    int32_t ramp,lp,env[3],dec[6];
    int32_t pending[16],history[16];
    uint32_t cursor,write,frac,need;
};
void dm_fm_init(struct dm_fm_voice *);
void dm_fm_trig(struct dm_fm_voice *,unsigned,const uint16_t *);
void dm_fm_half(struct dm_fm_voice *,unsigned,const uint16_t *,int32_t,int32_t *);
void dm_fm_render(struct dm_fm_voice *,unsigned,const uint16_t *,int32_t,int16_t *);
#endif
