/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/trx_cl_filter.c - TRX-CL's trx_cl_cl_filter, word for word mdkit's
 * (mdkit/src/trx/cl/mdkit_trx_cl.c): its coefficients as mdkit makes them, then
 * the two-pole loop of trx_metal.c's cascade. first and second are only read as
 * acc_to_word(asl(acc, 1)): md_floor_word(floor(S / 2^22) + d), d the 1/2, -1, 1/2
 * terms (md-floor.h). The workspace's reads and writes keep mdkit's order.
 * Included by the generated voice in place of the function (tools/md_port.py,
 * NATIVE). tests/test_md_st.py compares every word with mdkit's.
 */
#include "md-floor.h"
static void trx_cl_cl_filter(mdkit_trx_cl *s)
{
    uint32_t *w = s->workspace, shape = s->filter_shape;
    const uint32_t norm_index = (s->filter_offset + trx_cl_trx_filter_memory(shape)) & 0xffffffu;
    const uint32_t coef_index = (s->filter_offset + trx_cl_trx_filter_memory(0x80 + shape)) & 0xffffffu;
    const uint32_t resonance = trx_cl_trx_filter_memory(0x1900 + coef_index);
    const uint32_t feedback = md_acc_acc_to_word(md_acc_asl(md_acc_mpy_x(resonance, trx_cl_trx_filter_memory(0x900 + norm_index)), 1));
    const uint32_t square = md_acc_acc_to_word(md_acc_sub(md_acc_make(0, 0), md_acc_mpy_x(resonance, resonance)));
    const int32_t sq = md_s(square), fb = md_s(feedback);
    w[0] = s->input_b; w[1] = s->input_a; w[64] = s->output_b; w[65] = s->output_a;
    w[30] = s->delay_b; w[31] = s->delay_a;
    unsigned history = 1, input = 31, output = 65;
    uint32_t source = w[30];
    int32_t first = md_floor22_2(sq, md_s(w[0]), fb, md_s(w[1]));        /* floor(S / 2^22), d to come */
    for (unsigned i = 0; i < 32; ++i) {
        const int32_t d = md_s(source) - 2 * md_s(w[input]) + md_s(w[input + 1]);   /* 1/2, -1, 1/2 */
        const int32_t previous = md_s(w[output - 1]);
        const uint32_t first_word = md_floor_word(first + d);
        const int32_t second = md_floor22_2(sq, previous, fb, md_s(w[output]))
                               + md_s(first_word) - 2 * md_s(w[history]) + md_s(w[history ^ 1]);
        ++output;
        w[history ^ 1] = first_word;
        first = md_floor22_2(sq, md_s(w[history]), md_s(first_word), fb);
        history ^= 1;
        source = w[input++];
        w[output] = md_floor_word(second);
    }
    s->input_a = w[history]; history ^= 1; s->input_b = w[history];
    s->output_a = w[output--]; s->output_b = w[output];
    s->delay_a = w[input]; s->delay_b = w[input - 1];
}
