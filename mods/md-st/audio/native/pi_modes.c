/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/pi_modes.c - a PI voice's eight modes, word for word mdkit's
 * drum_modes (mdkit/src/pi/sd/mdkit_pi_sd.c), without the DSP's address
 * registers and with the three MACs of a sample taken together.
 *
 * What mdkit's loop does, and what this keeps:
 * - Mode m reads its coefficients from Y 0x64 + 4m: the modulo-3 register
 *   steps through + 0, 1, 2 once a sample and the +6 at the end of a mode
 *   lands on the next group, so c0, c1, c2 are fixed for a mode.
 * - The input register walks X 0x60..0x7f once a mode: sample i reads
 *   X 0x60 + i, for every mode.
 * - Each sample: old = word(state); state += 2 (prev c0 + old c1 + c2 in);
 *   prev = old. A sum is a sum: md_acc_mac3 adds the three products at once.
 * - The output register writes X 0x3f + i the word of the previous sample's
 *   carry and reads X 0x40 + i before writing it, so the bus X 0x40..0x5f
 *   gains each mode's state: carry_i = from24(X 0x40 + i) + state (its A0
 *   adds no carry), limited, stored at X 0x40 + i and block[i]. Each mode
 *   first writes X 0x3f and block[31] with the carry coming in. Nothing reads
 *   the bus, X 0x3f or block between modes, so here the bus is a local array of
 *   signed words and they are stored once: X 0x40.. and block as the last
 *   mode leaves them, X 0x3f as the last mode's start writes it.
 * Included by the generated voice in place of drum_modes (tools/md_port.py,
 * NATIVE). tests/test_md_st.py compares every word with mdkit's.
 */
#ifndef MD_PI_T                       /* the voice's types: PI-SD's unless the voice names its own */
#define MD_PI_T mdkit_pi_sd
#define MD_PI_MODAL_T mdkit_pi_sd_modal
#endif
#ifdef MD_AP_ACC
/* MD_AP_STRUCT (a voice judged by its spectrum, tools/md_port.py): the bus is limited once,
 * after the last mode, and the modes in MD_PI_SKIP_MODES are left out */
#ifdef MD_AP_STRUCT
#define MD_PM_BUSSAT(x) (x)
#else
#define MD_PM_BUSSAT(x) md_sat(x)
#endif
#if defined(MD_AP_STRUCT) && defined(MD_PI_SKIP_MODES)
#define MD_PM_SKIP(mode) ((MD_PI_SKIP_MODES >> (mode)) & 1u)
#else
#define MD_PM_SKIP(mode) 0
#endif
/* MD_AP_ACC (MD_ST_APPROX): a mode's state as one word, st = floor(state / 2^18), stepped
 * by the EMAC's read of the three products and half a unit a sample (pi_modal_filter.c
 * says why). Not mdkit's word for word: tests/md_st_voice_ser.c measures it. */
#if defined(MD_PI_HALF) && defined(MD_AP_STRUCT)
#include "pi_sd_half_modes.c"
#else
static __attribute__((unused)) void drum_modes(MD_PI_T *s, uint64_t carry)
{
    MD_PI_MODAL_T *c = &s->body;
    uint32_t *w = c->work, *y = c->fraction;
    int32_t in[32], bus[32];
    unsigned mode, i;
    (void)carry;                      /* X 0x3f gets the carry out of mode 6: bus[31] */
    for (i = 0; i < 32; ++i) { in[i] = MD_EY(md_s(w[0x60 + i])); bus[i] = md_s(w[0x40 + i]); }
    for (mode = 0; mode < 8; ++mode) {
        if (mode == 7) w[0x3f] = md_w24(md_sat(bus[31]));
        if (MD_PM_SKIP(mode)) continue;
        /* the EMAC's operands shifted once (MD_EX, MD_EY: md_ar3s); two samples a turn,
         * the second taking the half unit */
        const int32_t c0 = MD_EY(md_s(y[0x64 + 4 * mode])), c1 = MD_EY(md_s(y[0x65 + 4 * mode])),
                      c2 = MD_EX(md_s(y[0x66 + 4 * mode]));
        int32_t st = (int32_t)((uint32_t)md_s(c->history[mode][0]) << 6) | (int32_t)((c->history[mode][1] & 0xffffffu) >> 18);
        int32_t prev = MD_EX(md_s(c->history[mode][2]));
        for (i = 0; i < 32; i += 2) {
            const int32_t first = MD_EX(md_sat(st >> 6));
            st += md_ar3s(prev, c0, first, c1, c2, in[i]);
            bus[i] = MD_PM_BUSSAT(bus[i] + (st >> 6));
            prev = MD_EX(md_sat(st >> 6));
            st += md_ar3s(first, c0, prev, c1, c2, in[i + 1]) + 1;
            bus[i + 1] = MD_PM_BUSSAT(bus[i + 1] + (st >> 6));
        }
        c->history[mode][0] = md_w24(md_sat(st >> 6));
        c->history[mode][1] = (((uint32_t)st & 63u) << 18) | 0x20000u;
        c->history[mode][2] = md_w24(prev >> 8);
    }
    for (i = 0; i < 32; ++i) s->block[i] = w[0x40 + i] = md_w24(md_sat(bus[i]));
}
#endif
#else
static __attribute__((unused)) void drum_modes(MD_PI_T *s, uint64_t carry)
{
    MD_PI_MODAL_T *c = &s->body;
    uint32_t *w = c->work, *y = c->fraction;
    int32_t in[32], bus[32], carried = md_acc_word_s(carry);
    unsigned mode, i;
    for (i = 0; i < 32; ++i) { in[i] = md_s(w[0x60 + i]); bus[i] = md_s(w[0x40 + i]); }
    for (mode = 0; mode < 8; ++mode) {
        const int32_t c0 = md_s(y[0x64 + 4 * mode]), c1 = md_s(y[0x65 + 4 * mode]), c2 = md_s(y[0x66 + 4 * mode]);
        md_acc state = md_acc_from48(md_acc_pack48(c->history[mode][0], c->history[mode][1]));
        int32_t prev = md_s(c->history[mode][2]);
        if (mode == 7) w[0x3f] = md_w24(carried);
        for (i = 0; i < 32; ++i) {
            const int32_t old = md_acc_word_s(state);
            state = md_acc_mac3(state, prev, c0, old, c1, c2, in[i]);
            prev = old;
            bus[i] = carried = md_sat(bus[i] + MD_T(state));
        }
        {
            uint64_t stored = md_acc_to48(state);
            c->history[mode][0] = md_acc_mid(stored);
            c->history[mode][1] = md_acc_low(stored);
            c->history[mode][2] = md_w24(prev);
        }
    }
    for (i = 0; i < 32; ++i) s->block[i] = w[0x40 + i] = md_w24(bus[i]);
}
#endif
#undef MD_PM_BUSSAT
#undef MD_PM_SKIP
#undef MD_PI_T
#undef MD_PI_MODAL_T
