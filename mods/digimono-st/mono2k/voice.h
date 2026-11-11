/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef DM_M2K_VOICE_H
#define DM_M2K_VOICE_H
#include <stdint.h>
enum { DM_SID=8, DM_FMSTAT, DM_FMPAR, DM_FMDYN };
struct dm_m2k_voice {
    uint32_t ph[4], menv[3], lfsr;
    int32_t ms[6];
};
void dm_m2k_init(struct dm_m2k_voice *v);
void dm_m2k_trig(struct dm_m2k_voice *v,unsigned type,const uint16_t *p);
void dm_m2k_render(struct dm_m2k_voice *v,unsigned type,const uint16_t *p,
                   int32_t pitch,int32_t previous_pitch,int16_t *out);
#endif
