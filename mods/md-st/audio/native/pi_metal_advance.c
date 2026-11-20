/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_metal_advance.c - PI-CC, HH and RC's metal_advance (the DSP's
 * modulo address register; the same code in the three, mdkit/src/pi/{cc,hh,rc}),
 * as pi_wrap.c does for the PI voices' modal_wrap: the same address for every
 * input. mdkit finds the mask, the smallest 2^k - 1 >= modulo, by doubling in a
 * loop on every call; smearing modulo's bits right gives it at once. Addresses are
 * 24-bit and the steps 24-bit words, so int32_t holds what mdkit computes in
 * int64_t. The two cases mdkit tests first (modulo 0xffffff: linear; 0: no move)
 * stay. Included by the generated voice in place of the function (tools/md_port.py,
 * NATIVE).
 */
static inline uint32_t metal_advance(uint32_t old, int delta, uint32_t modulo)
{
    uint32_t mask, base;
    int32_t value, span;
    if (modulo == 0xffffffu) return (old + (uint32_t)delta) & 0xffffffu;
    if (!modulo) return old;
    mask = modulo | (modulo >> 1);
    mask |= mask >> 2; mask |= mask >> 4; mask |= mask >> 8; mask |= mask >> 16;
    /* modulo 2^k - 1 (every call the voices make) and 0 < |delta| <= modulo: one wrap at
     * most, within old's 2^k block (the same address) */
    if (mask == modulo && delta && delta >= -(int32_t)modulo && delta <= (int32_t)modulo)
        return ((old & ~mask) | ((old + (uint32_t)delta) & mask)) & 0xffffffu;
    base = old & ~mask;
    value = (int32_t)old + delta;
    span = ((uint32_t)delta & mask) ? (int32_t)modulo + 1 : 0;
    if (value < (int32_t)base) value += span;
    if (value > (int32_t)(base + modulo)) value -= span;
    return (uint32_t)value & 0xffffffu;
}
