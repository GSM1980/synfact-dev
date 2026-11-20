/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/trx_cl_scan.c - TRX-CL's trx_cl_cl_scan, word for word mdkit's
 * (mdkit/src/trx/cl/mdkit_trx_cl.c), without the 56-bit accumulator. Each sample
 * and_mid(phase, 0x7fff) leaves the middle word's 15 low bits (the index), then the
 * increment is added: its high word, and as its A0 the sine word read last (mdkit's
 * step_lo is overwritten by it). The extension byte never reaches the middle word, so
 * the phase is a 32-bit high word of which 24 bits are kept, and A0 with its carry.
 * Included by the generated voice in place of the function (tools/md_port.py,
 * NATIVE). tests/test_md_st.py compares every word.
 */
static void trx_cl_cl_scan(uint32_t *hi, uint32_t *lo, uint64_t increment, uint32_t *w, unsigned start, unsigned count)
{
    const uint32_t step_hi = (uint32_t)md_s(md_acc_mid(increment));
    uint32_t t = *hi & 0xffffffu, l = *lo & 0xffffffu, step_lo = md_acc_low(increment) & 0xffffffu;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned index = t & 0x7fffu;
        l += step_lo;
        t = index + step_hi + (l >> 24);
        l &= 0xffffffu;
        step_lo = trx_cl_local_sine(index);
        w[(start + i) & 31] = step_lo;
    }
    *hi = t & 0xffffffu; *lo = l;
}
