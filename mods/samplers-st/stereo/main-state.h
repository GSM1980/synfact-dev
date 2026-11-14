/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ST_STEREO_MAIN_STATE_H
#define ST_STEREO_MAIN_STATE_H
#include <syntakt/modules.h>
#include <syntakt/stereo.h>
typedef st_u32 u32;typedef int s32;
struct block {u32 sequence,mask,print;unsigned char shift[8];short d[8][32];};
struct stereo_main_state {
    struct block slot[2];
    const struct block *use;
    u32 tagged,have,resolved,last_used,boot_sequence,alive;
    s32 ramp_start[4][8];
    u32 ramp_primed[4];
};
#ifdef __mcoldfire__
_Static_assert(__builtin_offsetof(struct stereo_main_state,ramp_start)==1092,"assembly gain rows offset");
_Static_assert(__builtin_offsetof(struct stereo_main_state,ramp_primed)==1220,"assembly primed rows offset");
#endif
extern struct stereo_main_state *st_stereo_state;
#define stereo_state st_stereo_state
#define slot (stereo_state->slot)
#define use (stereo_state->use)
#define tagged (stereo_state->tagged)
#define have (stereo_state->have)
#define resolved (stereo_state->resolved)
#define last_used (stereo_state->last_used)
#define boot_sequence (stereo_state->boot_sequence)
#define alive (stereo_state->alive)
#define ramp_start (stereo_state->ramp_start)
#define ramp_primed (stereo_state->ramp_primed)
struct st_stereo_counters {u32 interrupts,received,torn,dropped,stale,late,mask,again,read,mix;};
extern struct st_stereo_counters st_stereo_main_counters;
u32 st_stereo_read_block(struct block *);
u32 st_stereo_resolve(void);
#ifndef TRACK_PCM
#define TRACK_PCM ((const volatile s32 *)0x80003d50u)
#endif
#endif
