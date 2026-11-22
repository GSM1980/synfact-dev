/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef DM_M2K_VOICE_H
#define DM_M2K_VOICE_H
#include <stdint.h>
#include "../fmplus/voice.h"
#include "../dpro/voice.h"
enum { DM_SID=8, DM_FMSTAT, DM_FMPAR, DM_FMDYN };
#define DM_ENS 4u
struct dm_ens_voice {
    uint32_t ph[6];
    int32_t ms[6];
    int16_t line[1024];
};
void dm_ens_init(struct dm_ens_voice *v);
void dm_ens_trig(struct dm_ens_voice *v);
void dm_ens_render(struct dm_ens_voice *v,const uint16_t *p,int32_t pitch,int16_t *left,int16_t *right);
/* ENS's chorus alone, after another generator: 32 mono samples in, two out. */
void dm_ens_chorus(struct dm_ens_voice *v,const uint16_t *p,int16_t *left,int16_t *right);
struct dm_m2k_voice {
    uint32_t ph[4], menv[3], lfsr;
    int32_t ms[6];
    struct dm_fm_voice fm;
    struct dm_dp_voice dp;
};
void dm_m2k_init(struct dm_m2k_voice *v);
void dm_m2k_trig(struct dm_m2k_voice *v,unsigned type,const uint16_t *p);
void dm_m2k_render(struct dm_m2k_voice *v,unsigned type,const uint16_t *p,
                   int32_t pitch,int32_t previous_pitch,int16_t *out);
#endif
