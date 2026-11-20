/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_hh_half_block.c - PI-HH's block: at half rate under MD_PI_HALF and MD_AP_STRUCT
 * (native/pi_metal_half.c: the same stages, in mdkit's order, half the noise pairs), else
 * mdkit's (mdkit/src/pi/hh/mdkit_pi_hh.c). Included by the generated voice in place of block
 * (tools/md_port.py, NATIVE).
 */
#if defined(MD_PI_HALF) && defined(MD_AP_STRUCT)
#include "pi_metal_half.c"
static void block(mdkit_pi_hh*s){
 uint32_t old=s->storage.voice.ring_amplitude;uint64_t ring=mdkit_mpy(old,s->storage.voice.grab);
 if(mdkit_s56(ring)>((int64_t)84<<24))s->storage.voice.excitation_threshold=mdkit_acc_to_word(mdkit_sub(mdkit_from24(s->storage.voice.hardness),mdkit_from24(0x4ccccd)));
 else if(mdkit_s24(old)>=84){s->storage.voice.gate=0x20000;s->storage.voice.impulse_step=0xccccd;s->storage.voice.excitation_decay=0x75c28f;s->storage.voice.excitation_threshold=s->storage.voice.close_envelope=0xccccd;s->storage.voice.close_decay=0x733333;}
 s->storage.voice.ring_amplitude=mdkit_acc_to_word(ring);old=s->storage.voice.close_envelope;
 s->storage.voice.close_envelope=metal_product(old,s->storage.voice.close_decay);
 s->storage.voice.amplitude=mdkit_acc_to_word(mdkit_add(mdkit_from24(s->storage.voice.amplitude),mdkit_from24(old)));
 if(s->storage.voice.gate)s->storage.voice.close_memory=s->storage.voice.amplitude;
 metal_noise(s,9);md_mh_excitation(s,s->storage.voice.impulse_step,s->storage.voice.excitation_threshold);md_mh_body_filters(s);
 uint32_t step=metal_product(s->storage.voice.gate?0x733333:s->storage.voice.pitch,0x49);
 md_mh_window(s,step,0x19999a,0);md_mh_amplitude(s);md_mh_coefficients(s);md_mh_coupling(s);
 md_mh_output(s,0x666666);
}
#else
static void block(mdkit_pi_hh*s){
 uint32_t old=s->storage.voice.ring_amplitude;uint64_t ring=mdkit_mpy(old,s->storage.voice.grab);
 if(mdkit_s56(ring)>((int64_t)84<<24))s->storage.voice.excitation_threshold=mdkit_acc_to_word(mdkit_sub(mdkit_from24(s->storage.voice.hardness),mdkit_from24(0x4ccccd)));
 else if(mdkit_s24(old)>=84){s->storage.voice.gate=0x20000;s->storage.voice.impulse_step=0xccccd;s->storage.voice.excitation_decay=0x75c28f;s->storage.voice.excitation_threshold=s->storage.voice.close_envelope=0xccccd;s->storage.voice.close_decay=0x733333;}
 s->storage.voice.ring_amplitude=mdkit_acc_to_word(ring);old=s->storage.voice.close_envelope;
 s->storage.voice.close_envelope=metal_product(old,s->storage.voice.close_decay);
 s->storage.voice.amplitude=mdkit_acc_to_word(mdkit_add(mdkit_from24(s->storage.voice.amplitude),mdkit_from24(old)));
 if(s->storage.voice.gate)s->storage.voice.close_memory=s->storage.voice.amplitude;
 metal_noise(s,17);metal_excitation(s,s->storage.voice.impulse_step,s->storage.voice.excitation_threshold);metal_body_filters(s);
 uint32_t step=metal_product(s->storage.voice.gate?0x733333:s->storage.voice.pitch,0x49);
 metal_window(s,step,0x19999a,0);metal_amplitude(s);metal_coefficients(s);metal_coupling(s);
 metal_output(s,0x666666);
}
#endif
#undef MD_PM_T
