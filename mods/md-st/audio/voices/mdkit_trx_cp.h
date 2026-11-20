/* SPDX-License-Identifier: CC0-1.0
 * TRX-CP standalone synthesis voice.
 
 * Synfact: generated from mdkit/src/trx/cp by tools/md_port.py; do not edit.
 * The changes are listed there, and marked "Synfact" here.
 */
#ifndef MDKIT_TRX_CP_H
#define MDKIT_TRX_CP_H
#include "md-types.h"
#include "mdkit_voice.h"


typedef struct mdkit_trx_cp {
 mdkit_control_clock clock;
 unsigned pos,pending_trigger,triggered,target_mask;
 uint32_t queued_targets[8],last_raw[8],parameters[14];
 uint32_t noise_a,noise_b,burst_amplitude,room_amplitude;
 uint32_t burst_seed[4],burst_decay[4],burst_count[3];
 uint32_t prefilter[4],body_filter[6],tone_filter[4];
 uint32_t workspace[160],auxiliary[160],block[32];
} mdkit_trx_cp;
extern const mdkit_voice mdkit_voice_trx_cp;
/* Synfact: the 24-bit words, sign-extended, at 44.1 kHz */
void md_st_trx_cp_render_words(void *state, int32_t *out, unsigned frames);


#endif
