/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/trx_ma_block.c - TRX-MA's ma_block, word for word mdkit's
 * (mdkit/src/trx/ma/mdkit_trx_ma.c), its loops on signed words.
 *
 * - The rattle: the floor and the noise come from from24 and sub (A0 = 0), so they
 *   are one signed word each; mpy(rate, growth) has the high word floor(r g / 2^23)
 *   and A0 = 2 r g mod 2^24, which only "value <= floor" reads (equal high words: A0
 *   must be 0), so it is made only then.
 * - acc_to_word(asr(from24(w), 3)) is w >> 3; from24(w) + mpy(x, y) has the high
 *   word w + floor(x y / 2^23); asr(mpy(w, e), 2) the high word floor(x y / 2^23) >> 2.
 * - The envelope adds and subtracts from24 words: one signed integer (A0 = 0).
 * The three filters and their order are mdkit's. Included by the generated voice in
 * place of the function (tools/md_port.py, NATIVE). tests/test_md_st.py compares every
 * word with mdkit's.
 */
static void trx_ma_ma_block(mdkit_trx_ma *s)
{
    uint32_t *w = s->workspace, *p = s->parameters;
    unsigned i;
    trx_ma_ma_noise(s, w + 2);
    trx_ma_cp_filter6(w, s->auxiliary, 0, 70, 112, s->source_filter, p[MA_SOURCE_FREQUENCY], 127);
    for (i = 0; i < 32; ++i) s->auxiliary[32 + i] = w[70 + i];
    {
        int32_t growth = md_s(s->rattle_growth), rate = md_s(s->rattle_rate), floor = md_s(s->rattle_floor);
        const int32_t threshold = md_s(p[MA_RATTLE_THRESHOLD]), decay = md_s(s->rattle_decay), attack = md_s(s->rattle_attack);
        for (i = 0; i < 32; ++i) {
            const int32_t noise = md_s(w[33 - i]);
            int32_t value;
            if (noise >= threshold) {
                growth = md_s(0xfffcb9u);
                floor = noise < 0 ? noise : -noise;     /* -|noise|, as sub(0, |noise|) */
                rate = decay;
            }
            value = md_mul_shift_exact(rate, growth, 23);
            if (floor) {
                const int32_t g = growth;
                value += g;
                growth = md_sat(value);
                if (value < floor || (value == floor && !(((uint32_t)rate * (uint32_t)g << 1) & 0xffffffu))) {
                    floor = -floor; rate = attack; growth = md_sat(floor); floor = 0;
                }
            } else growth = md_sat(value);
            w[34 + i] = md_w24(md_sat(value));
        }
        s->rattle_growth = md_w24(growth); s->rattle_floor = md_w24(md_sat(floor)); s->rattle_rate = md_w24(rate);
    }
    trx_ma_cp_filter6(w, s->auxiliary, 34, 70, 112, s->rattle_filter, p[MA_RATTLE_FREQUENCY], p[MA_RATTLE_WIDTH]);
    for (i = 0; i < 32; ++i) w[70 + i] = md_w24(md_s(w[70 + i]) >> 3);
    trx_ma_cp_filter6(w, s->auxiliary, 68, 2, 112, s->tone_filter, p[MA_TONE_FREQUENCY], 0);
    {
        const int32_t room = md_sat(md_s(s->room_amplitude) + md_s(p[MA_ROOM_STEP]));
        s->room_amplitude = md_w24(room);
        for (i = 0; i < 32; ++i)
            w[2 + i] = md_w24(md_sat(md_s(w[2 + i]) + md_mul_shift_exact(md_s(s->auxiliary[32 + i]), room, 23)));
    }
    {
        unsigned stage = 3;
        int32_t envelope = md_s(s->envelope);
        const int32_t up = md_s(p[MA_ATTACK_STEP]), down = md_s(p[MA_RELEASE_STEP]);
        if (s->attack_blocks) { stage = 0; --s->attack_blocks; }
        else if (s->sustain_blocks) { stage = 1; --s->sustain_blocks; }
        else if (s->release_blocks) { stage = 2; --s->release_blocks; }
        for (i = 0; i < 32; ++i) {
            int32_t output;
            if (stage == 0) envelope += up;
            if (stage == 2) envelope -= down;
            output = stage == 3 ? 0 : stage == 1 ? md_s(w[2 + i]) : md_mul_shift_exact(md_s(w[2 + i]), md_sat(envelope), 23);
            s->block[i] = md_w24(md_sat(output >> 2));
        }
        if (stage == 0 || stage == 2) s->envelope = md_w24(md_sat(envelope));
    }
}
