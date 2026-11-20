/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/trx_sd_noise.c - TRX-SD's trx_sd_sd_noise_filter, word for word mdkit's
 * (mdkit/src/trx/sd/mdkit_trx_sd.c): its coefficients as mdkit makes them, then
 * the two-pole loop with no 56-bit accumulator. first and second are only read
 * as acc_to_word(asl(acc, 1)), which md-floor.h computes from the products'
 * floor(S / 2^22). The workspace's reads and writes keep mdkit's order; history
 * alternates between words 3 and 2. Included by the generated voice in place of
 * the function (tools/md_port.py, NATIVE). tests/test_md_st.py compares every word.
 */
#include "md-floor.h"
static void trx_sd_sd_noise_filter(mdkit_trx_sd *s)
{
    uint32_t *w = s->workspace;
    const uint32_t resonance = trx_sd_sd_filter(s->filter_index - 0x241);
    const md_acc product = md_acc_mpy_x(resonance, trx_sd_sd_norm(s->filter_index - 3));
    const md_acc negative_square = md_acc_sub(md_acc_make(0, 0), md_acc_mpy_x(resonance, resonance));
    const md_acc doubled = md_acc_asl(product, 1);
    const uint32_t feedback = md_acc_acc_to_word(doubled);
    const md_acc feed = md_acc_sub(md_acc_from24(0x400000),
                                   md_acc_add(doubled, md_acc_from24(md_acc_acc_to_word(negative_square))));
    const uint32_t square = md_acc_acc_to_word(negative_square), input_gain = md_acc_acc_to_word(feed);
    const int32_t sq = md_s(square), fb = md_s(feedback), ig = md_s(input_gain);
    w[3] = s->filter_a; w[2] = s->filter_b;
    w[30] = s->filter_c; w[31] = s->filter_d;
    unsigned history = 3, input = 33, delayed = 30, adjacent = 31;
    int32_t first = md_floor22(sq, md_s(w[3]), fb, md_s(w[2]), md_s(w[32]), ig);
    for (unsigned i = 0; i < 32; ++i) {
        const int32_t d = md_s(w[delayed++]), a = md_s(w[adjacent++]);
        const uint32_t first_word = md_floor_word(first);
        const int32_t second = md_floor22(sq, d, fb, a, md_s(first_word), ig);
        w[history] = first_word; history ^= 1;
        const int32_t old_history = md_s(w[history]);
        history ^= 1;
        const int32_t new_history = md_s(w[history]);
        history ^= 1;
        first = md_floor22(sq, old_history, fb, new_history, md_s(w[input]), ig);
        --input;
        w[input] = md_floor_word(second); input += 2;
    }
    s->filter_a = w[history]; history ^= 1;
    s->filter_b = w[history];
    s->filter_d = w[adjacent]; s->filter_c = w[adjacent - 1];
}
