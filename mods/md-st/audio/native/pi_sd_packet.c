/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_sd_packet.c - PI-SD's mdkit_pi_sd_packet with the integer clock
 * (native/pi_clock.c): (unsigned)(128.0f * k[i]) is the stored integer. The
 * knob each word reads is mdkit's (mdkit/src/pi/sd/mdkit_pi_sd.c).
 */
static void mdkit_pi_sd_packet(const float k[8], uint32_t out[8])
{
    const md_pclock_u32 *q = (const md_pclock_u32 *)(const void *)k;
    out[0] = mdkit_pi_sd_word0(q[0]);
    out[1] = mdkit_pi_sd_word1(q[1]);
    out[2] = mdkit_pi_sd_word2(q[2]);
    out[3] = mdkit_pi_sd_word3(q[7]);
    out[4] = mdkit_pi_sd_word4(q[4]);
    out[5] = mdkit_pi_sd_word5(q[5]);
    out[6] = mdkit_pi_sd_word6(q[6]);
    out[7] = mdkit_pi_sd_word7(q[3]);
}
