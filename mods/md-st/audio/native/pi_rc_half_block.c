/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_rc_half_block.c - PI-RC's block: at half rate under MD_PI_HALF and MD_AP_STRUCT
 * (native/pi_metal_half.c: the same stages, in mdkit's order, half the noise pairs), else
 * mdkit's (mdkit/src/pi/rc/mdkit_pi_rc.c). Included by the generated voice in place of block
 * (tools/md_port.py, NATIVE).
 */
#if defined(MD_PI_HALF) && defined(MD_AP_STRUCT)
#include "pi_metal_half.c"
static void block(mdkit_pi_rc*s){
 s->storage.voice.ring_amplitude=metal_product(s->storage.voice.ring_amplitude,s->storage.voice.grab);
 metal_noise(s,8);
 uint32_t step=mdkit_acc_to_word(mdkit_asr(mdkit_mpy(s->storage.voice.pitch,0x41),2));
 md_mh_window(s,step,0x733333,1);
 uint32_t complement=mdkit_acc_to_word(mdkit_sub(mdkit_from24(0x7fffff),mdkit_from24(s->storage.voice.hardness)));
 md_mh_excitation(s,metal_product(s->storage.voice.impulse_step,complement),s->storage.voice.hardness);
 md_mh_body_filters(s);md_mh_amplitude(s);md_mh_coefficients(s);md_mh_coupling(s);
 md_mh_output(s,complement);
}
#else
static void block(mdkit_pi_rc*s){
 s->storage.voice.ring_amplitude=metal_product(s->storage.voice.ring_amplitude,s->storage.voice.grab);
 metal_noise(s,16);
 uint32_t step=mdkit_acc_to_word(mdkit_asr(mdkit_mpy(s->storage.voice.pitch,0x41),2));
 metal_window(s,step,0x733333,1);
 uint32_t complement=mdkit_acc_to_word(mdkit_sub(mdkit_from24(0x7fffff),mdkit_from24(s->storage.voice.hardness)));
 metal_excitation(s,metal_product(s->storage.voice.impulse_step,complement),s->storage.voice.hardness);
 metal_body_filters(s);metal_amplitude(s);metal_coefficients(s);metal_coupling(s);
 metal_output(s,complement);
}
#endif
#undef MD_PM_T
