/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/trx_sd_tone.c - TRX-SD's trx_sd_sd_tone, word for word mdkit's
 * (mdkit/src/trx/sd/mdkit_trx_sd.c), without the 56-bit accumulator:
 *   - step = asr(asr(from24(pitch), 1) + from24(bump), 4) is (pitch + 2 bump) * 2^19
 *     exactly (pitch * 2^23 + bump * 2^24 is a multiple of 16): its high word is
 *     d >> 5 and its A0 (d mod 32) << 19, d = pitch + 2 bump (signed words).
 *   - the phase is read as its middle word's 15 low bits (index) and kept as such
 *     by and_mid(.., 0x7fff); bits 15..23 are cleared each sample and the extension
 *     byte never reaches a stored or read bit, so the phase is those 15 bits and
 *     A0 (24), added with A0's carry.
 *   - acc_to_word(mpy(sine, gain)) is the limit of floor(sine gain / 2^23), one
 *     exact product.
 * Included by the generated voice in place of the function (tools/md_port.py,
 * NATIVE). tests/test_md_st.py compares every word.
 */
static void trx_sd_sd_tone(uint64_t *phase, uint32_t pitch, uint32_t bump, uint32_t gain, uint32_t *out)
{
    const int32_t d = md_s(pitch) + 2 * md_s(bump), g = md_s(gain);
    const uint32_t step_hi = (uint32_t)(d >> 5), step_lo = ((uint32_t)d & 31u) << 19;
    uint32_t hi = (uint32_t)MD_T(*phase) & 0x7fffu, lo = MD_L(*phase) & 0xffffffu;
    for (unsigned i = 0; i < 32; ++i) {
        const int32_t sine = md_s(trx_sd_local_sine(hi));
        lo += step_lo;
        hi = (hi + step_hi + (lo >> 24)) & 0x7fffu;
        lo &= 0xffffffu;
        out[i] = md_acc_limit(md_mid_mpy_exact(sine, g));
    }
    *phase = md_acc_make((int32_t)hi, lo);
}
