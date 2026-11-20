/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_wrap.c - mdkit's mdkit_pi_<voice>_modal_wrap (the DSP's modulo
 * address register) without its loop: the same address for every input.
 *
 * mdkit finds the mask, the smallest 2^k - 1 that is >= m, by doubling in a
 * loop on every call (three times a MAC in the modal filter). Smearing m's
 * bits right gives that mask at once (m = 0 gives 0, as the loop does). The
 * addresses are 24-bit and the steps small, so int32_t holds what mdkit
 * computes in int64_t. Included by the generated voice in place of the
 * function (tools/md_port.py, NATIVE); MD_WRAP names it.
 */
static inline __attribute__((unused)) uint32_t MD_WRAP(uint32_t p, int step, unsigned m)
{
    uint32_t mask = m | (m >> 1), base;
    int32_t v = (int32_t)p + step, span;
    mask |= mask >> 2; mask |= mask >> 4; mask |= mask >> 8; mask |= mask >> 16;
    base = p & ~mask;
    span = ((unsigned)step & mask) ? (int32_t)m + 1 : 0;
    if (v < (int32_t)base) v += span;
    if (v > (int32_t)(base + m)) v -= span;
    return (uint32_t)v & 0xffffffu;
}
#undef MD_WRAP
