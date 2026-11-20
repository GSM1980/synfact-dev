/* SPDX-License-Identifier: CC0-1.0
 * TRX-OH standalone synthesis voice.
 
 * Synfact: generated from mdkit/src/trx/oh by tools/md_port.py; do not edit.
 * The changes are listed there, and marked "Synfact" here.
 */
#ifndef MDKIT_TRX_OH_H
#define MDKIT_TRX_OH_H
#include "md-types.h"
#include "mdkit_voice.h"


typedef struct {
 mdkit_control_clock clock;
 unsigned machine,pos,pending_trigger,triggered,target_mask;
 unsigned queued_targets[8],last_raw[8];
 uint32_t linear_step,decay,lowpass_index,lowpass_resonance,noise_gain;
 uint32_t highpass_index,highpass_resonance,metal_gain,hold_blocks,fade_blocks;
 uint32_t latched_metal_gain;
 uint32_t amplitude,noise_a,noise_b,cursors[6],resonant_history[4],difference_history[2][4];
 uint32_t workspace[128],block[32];
} mdkit_trx_oh;


extern const mdkit_voice mdkit_voice_trx_oh;
/* Synfact: the 24-bit words, sign-extended, at 44.1 kHz */
void md_st_trx_oh_render_words(void *state, int32_t *out, unsigned frames);


#endif
