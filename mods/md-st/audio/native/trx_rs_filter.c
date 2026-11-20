/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/trx_rs_filter.c - TRX-RS's trx_rs_rs_filter, word for word mdkit's
 * (mdkit/src/trx/rs/mdkit_trx_rs.c). Its two accumulators persist from block to
 * block and are stored whole (store_long), so they stay md_acc values; only their
 * runs of MACs are taken together, the same sums exactly:
 * - mac x,0x400000 adds x * 2^23 and mac x,0x800000 adds -x * 2^24: a run of them
 *   is one integer (md_acc_add_halves);
 * - "mac f,a / mac f,a" is one product of f and 2a (25 bits: md_acc_product takes it);
 *   with the product before it, md_acc_mac2.
 * The workspace's reads, the stores and their order are mdkit's. Included by the
 * generated voice in place of the function (tools/md_port.py, NATIVE).
 */
static void trx_rs_rs_filter(mdkit_trx_rs *s)
{
    uint32_t *w = s->workspace;
    const uint32_t norm_index = (0x200 + trx_rs_trx_filter_memory(0)) & 0xffffffu;
    const uint32_t coef_index = (0x200 + trx_rs_trx_filter_memory(0x80)) & 0xffffffu;
    const uint32_t resonance = trx_rs_trx_filter_memory(0x1900 + coef_index);
    const md_acc coupling = md_acc_asl(md_acc_mpy_x(resonance, trx_rs_trx_filter_memory(0x900 + norm_index)), 1);
    const uint32_t feedback = md_acc_acc_to_word(md_acc_add(coupling, md_acc_from24(0xc00000)));
    const uint32_t square = md_acc_acc_to_word(md_acc_asl(md_acc_sub(md_acc_make(0, 0), md_acc_mpy_x(resonance, resonance)), 1));
    const int32_t sq = md_s(square), fb = md_s(feedback);
    w[66] = s->first_delay; w[67] = s->first_hi;
    w[32] = s->second_delay; w[33] = s->second_hi;
    w[94] = s->input_b; w[95] = s->input_a;
    md_acc first = md_acc_from48(md_acc_pack48(s->first_hi, s->first_low));
    md_acc second = md_acc_from48(md_acc_pack48(s->second_hi, s->second_low));
    first = md_acc_mac2(first, sq, md_s(w[66]), md_s(w[67]), 2 * fb);
    unsigned history = 67, input = 95, output = 33;
    uint32_t source = w[94];
    for (unsigned i = 0; i < 32; ++i) {
        first = md_acc_add_halves(first, md_s(source) - 2 * md_s(w[input]) + md_s(w[input + 1]));   /* 1/2, -1, 1/2 */
        const int32_t delayed = md_s(w[output - 1]), adjacent = md_s(w[output]);
        ++output;
        const uint32_t first_word = md_acc_mid(first);
        second = md_acc_mac2(second, sq, delayed, fb, 2 * adjacent);
        second = md_acc_add_halves(second, md_s(first_word) - 2 * md_s(w[history]) + md_s(w[history ^ 1]));
        history ^= 1;
        trx_rs_rs_store_long(s, history, first);
        const int32_t old = md_s(w[history ^ 1]), new_history = md_s(w[history]);
        first = md_acc_mac2(first, sq, old, new_history, 2 * fb);
        trx_rs_rs_store_long(s, output, second);
        source = w[input++];
    }
    s->first_hi = w[history]; s->first_low = s->fraction[history]; history ^= 1;
    s->first_delay = w[history];
    s->second_hi = w[output]; s->second_low = s->fraction[output]; --output;
    s->second_delay = w[output]; s->input_a = w[input]; s->input_b = w[input - 1];
}
