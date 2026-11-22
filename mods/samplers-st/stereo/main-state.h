/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ST_STEREO_MAIN_STATE_H
#define ST_STEREO_MAIN_STATE_H
#include <syntakt/modules.h>
#include <syntakt/stereo.h>
typedef st_u32 u32;typedef int s32;
/* A block as the window holds it, ST_STEREO_SEQ to ST_STEREO_SEQ + 0x230,
 * so that MAIN's eDMA can copy it whole (main.c, receive). */
struct block {
    u32 sequence,mask;unsigned char shift[8];u32 print,tag,count[2];
    short d[8][32];
    u32 sequence_end,spare[3];
} __attribute__((aligned(16)));
/* slot[0], the block MAIN's eDMA reads, lives in the state (SRAM when the
 * placement moves it there); slot[1], for the rare block AUDIO writes after
 * that read, stays in DDR (st_stereo_late_block). */
struct stereo_main_state {
    struct block first;
    const struct block *use;
    u32 tagged,have,resolved,last_used,boot_sequence,alive;
    s32 ramp_start[4][8];
    u32 ramp_primed[4];
    u32 pending;             /* the eDMA into slot[0] was started (main.c) */
};
_Static_assert(__builtin_offsetof(struct block,mask)==ST_STEREO_MASK-ST_STEREO_SEQ,"window mask");
_Static_assert(__builtin_offsetof(struct block,shift)==ST_STEREO_SHIFT-ST_STEREO_SEQ,"window shifts");
_Static_assert(__builtin_offsetof(struct block,print)==ST_STEREO_PRINT-ST_STEREO_SEQ,"window print");
_Static_assert(__builtin_offsetof(struct block,tag)==ST_STEREO_TAG-ST_STEREO_SEQ,"window tag");
_Static_assert(__builtin_offsetof(struct block,d)==ST_STEREO_WINDOW-ST_STEREO_SEQ,"window samples");
_Static_assert(__builtin_offsetof(struct block,sequence_end)==ST_STEREO_SEQ_END-ST_STEREO_SEQ,"window end");
_Static_assert(sizeof(struct block)==ST_STEREO_BLOCK,"window block");
#ifdef __mcoldfire__
_Static_assert(__builtin_offsetof(struct stereo_main_state,ramp_start)==588,"assembly gain rows offset");
_Static_assert(__builtin_offsetof(struct stereo_main_state,ramp_primed)==716,"assembly primed rows offset");
#endif
extern struct stereo_main_state *st_stereo_state;
#define stereo_state st_stereo_state
extern struct block st_stereo_late_block;
#define slot0 (stereo_state->first)
#define slot1 st_stereo_late_block
#define use (stereo_state->use)
#define tagged (stereo_state->tagged)
#define have (stereo_state->have)
#define resolved (stereo_state->resolved)
#define last_used (stereo_state->last_used)
#define boot_sequence (stereo_state->boot_sequence)
#define alive (stereo_state->alive)
#define ramp_start (stereo_state->ramp_start)
#define ramp_primed (stereo_state->ramp_primed)
#define pending (stereo_state->pending)
struct st_stereo_counters {u32 interrupts,received,torn,dropped,stale,late,mask,again,read,mix;};
extern struct st_stereo_counters st_stereo_main_counters;
u32 st_stereo_read_block(struct block *);
/* MAIN eDMA channel 46's TCD and the start register (main.c, receive). */
#ifndef ST_STEREO_MAIN_DMA
#define ST_STEREO_MAIN_DMA 0xfc0455c0u
#define ST_STEREO_MAIN_SSRT (*(volatile unsigned char *)0xfc04401eu)
#endif
u32 st_stereo_resolve(void);
#ifndef TRACK_PCM
#define TRACK_PCM ((const volatile s32 *)0x80003d50u)
#endif
#endif
