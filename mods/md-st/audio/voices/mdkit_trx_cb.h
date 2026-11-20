/* SPDX-License-Identifier: CC0-1.0
 * TRX-CB standalone synthesis voice.
 
 * Synfact: generated from mdkit/src/trx/cb by tools/md_port.py; do not edit.
 * The changes are listed there, and marked "Synfact" here.
 */
#ifndef MDKIT_TRX_CB_H
#define MDKIT_TRX_CB_H
#include "md-types.h"
#include "mdkit_voice.h"
typedef struct { uint32_t queued[8],last_raw[8];unsigned targets,trigger,triggered;} mdkit_trx_cb_events;



typedef struct {
 mdkit_control_clock clock;mdkit_trx_cb_events events;unsigned pos;
 uint32_t decay,bump_decay,pitch[2],gain[2],clip[2],filter_index[2],bump;
 uint32_t envelope,delay,last_output,history[2][6];uint64_t phase[2];
 uint32_t workspace[128],block[32];
} mdkit_trx_cb;

extern const mdkit_voice mdkit_voice_trx_cb;
/* Synfact: the 24-bit words, sign-extended, at 44.1 kHz */
void md_st_trx_cb_render_words(void *state, int32_t *out, unsigned frames);


#endif
