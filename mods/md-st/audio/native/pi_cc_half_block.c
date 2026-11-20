/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_cc_half_block.c - PI-CC's block: at half rate under MD_PI_HALF and MD_AP_STRUCT
 * (native/pi_metal_half.c: the same stages, in mdkit's order, half the noise pairs), else
 * mdkit's (mdkit/src/pi/cc/mdkit_pi_cc.c). Included by the generated voice in place of block
 * (tools/md_port.py, NATIVE).
 */
#if defined(MD_PI_HALF) && defined(MD_AP_STRUCT)
#include "pi_metal_half.c"
static void block(mdkit_pi_cc*s){
 s->storage.voice.ring_amplitude=metal_product(s->storage.voice.ring_amplitude,s->storage.voice.grab);
 uint32_t old=s->storage.voice.close_envelope;s->storage.voice.close_envelope=metal_product(old,s->storage.voice.close_decay);
 s->storage.voice.amplitude=mdkit_acc_to_word(mdkit_add(mdkit_from24(s->storage.voice.amplitude),mdkit_from24(old)));
 if(s->storage.voice.gate)s->storage.voice.close_memory=s->storage.voice.amplitude;
 metal_noise(s,8);md_mh_excitation(s,s->storage.voice.impulse_step,s->storage.voice.excitation_threshold);md_mh_body_filters(s);
 uint32_t step=metal_product(s->storage.voice.gate?0x733333:s->storage.voice.pitch,0x49);
 uint32_t gain=mdkit_s24(s->storage.voice.hardness)>=0x19999a?metal_product(s->storage.voice.hardness,0x19999a):0;
 md_mh_window(s,step,gain,0);md_mh_amplitude(s);md_mh_coefficients(s);md_mh_coupling(s);
 gain=mdkit_s24(s->storage.voice.hardness)>0x19999a?s->storage.voice.hardness-0x19999a:0;
 md_mh_output(s,gain);
}
#else
static void block(mdkit_pi_cc*s){
 s->storage.voice.ring_amplitude=metal_product(s->storage.voice.ring_amplitude,s->storage.voice.grab);
 uint32_t old=s->storage.voice.close_envelope;s->storage.voice.close_envelope=metal_product(old,s->storage.voice.close_decay);
 s->storage.voice.amplitude=mdkit_acc_to_word(mdkit_add(mdkit_from24(s->storage.voice.amplitude),mdkit_from24(old)));
 if(s->storage.voice.gate)s->storage.voice.close_memory=s->storage.voice.amplitude;
 metal_noise(s,16);metal_excitation(s,s->storage.voice.impulse_step,s->storage.voice.excitation_threshold);metal_body_filters(s);
 uint32_t step=metal_product(s->storage.voice.gate?0x733333:s->storage.voice.pitch,0x49);
 uint32_t gain=mdkit_s24(s->storage.voice.hardness)>=0x19999a?metal_product(s->storage.voice.hardness,0x19999a):0;
 metal_window(s,step,gain,0);metal_amplitude(s);metal_coefficients(s);metal_coupling(s);
 gain=mdkit_s24(s->storage.voice.hardness)>0x19999a?s->storage.voice.hardness-0x19999a:0;
 metal_output(s,gain);
}
#endif
#undef MD_PM_T
