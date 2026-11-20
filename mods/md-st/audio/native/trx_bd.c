/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/trx_bd.c - TRX-BD's block in plain 32-bit integers, word for word
 * mdkit's trx_bd_bd_run_block (mdkit/src/trx/bd/mdkit_trx_bd.c), which models
 * the Machinedrum's DSP code instruction by instruction.
 *
 * Included by the generated audio/voices/mdkit_trx_bd.c in place of that
 * function (tools/md_port.py, NATIVE). What changes, and why it is the same:
 *
 * - The DSP's circular pointers: every loop walks exactly 32 words of its
 *   window, so each returns to where it started, and window w's word k is
 *   always w + k. The windows (X at 0x20 and 0x40, Y at 0 and 0x20) are
 *   written before they are read in every block, so they are local arrays.
 *   The output ring (out, r7) is only ever read for its last word: tail_amp.
 * - mpy followed by a move of the high word: md_word_mpy / md_mid_mpy
 *   (md-native.h), floor(x*y / 2^23) from three multiplies.
 * - A value that is a word times 2^24, shifted, compared or saturated: the
 *   same integer arithmetic on the word (each step below says which).
 * - The once-a-block scalars and the first nine blocks after a note (alt)
 *   keep the accumulator helpers: they cost little and are proven already.
 * - Products marked _exact feed back on themselves (envelopes, steps); the
 *   others are the signal's, which MD_ST_FAST may compute in 16 bits
 *   (md-native.h).
 * tests/test_md_st.py compares every output word with mdkit's.
 */
#include "../voices/md-native.h"

static void trx_bd_bd_run_block(mdkit_trx_bd *s, uint32_t output[32])
{
    int32_t y0w[32], y20[32], x20[32], x40[32];
    uint64_t a, b;
    int32_t t;
    unsigned k;

    /* the 32-word noise of Y at 0: a running sum whose high byte (A2) stays */
    t = md_s(s->kv_a);
    {
        uint32_t x1 = s->kv_b, x0, n = 0;
        for (k = 0; k < 10; ++k) {
            t = (int32_t)((uint32_t)t + (uint32_t)md_s(x1));          /* add from24 */
            x0 = x1;
            y0w[n++] = (int32_t)md_w24(t); y0w[n++] = (int32_t)md_w24(t); y0w[n++] = (int32_t)md_w24(t);
            x1 = md_w24(t);
            t = (int32_t)(((uint32_t)t & 0xff000000u) | (x0 & 0xffffffu));  /* setmid */
        }
        t = (int32_t)((uint32_t)t + (uint32_t)md_s(x1));
        s->kv_a = x1;
        s->kv_b = md_w24(t);
        y0w[30] = y0w[31] = (int32_t)md_w24(t);
    }
    /* scaled by kv_c, pair by pair */
    {
        int32_t c = md_s(s->kv_c);
        for (k = 0; k < 32; ++k) y0w[k] = (int32_t)md_w24(md_word_mpy(md_s((uint32_t)y0w[k]), c));
        s->kv_c = md_w24(md_word_mpy_exact(md_s(s->spread), c));     /* feeds back */
    }

    if (s->env) {
        s->env -= 1;
        if (s->env == 9) {                                  /* tail */
            int32_t x1 = md_s(s->tail_amp);
            for (k = 0; k < 32; ++k) {
                x1 = md_word_mpy_exact(x1, 0x733333);       /* feeds back; > 0: never limited */
                output[k] = md_w24(x1);
            }
        } else {                                            /* alt: the attack's board */
            const uint32_t xoff = s->board_ofs, x0 = s->xfade, x1 = s->strt;
            for (k = 0; k < 32; ++k) {
                uint32_t y1 = trx_bd_trx_bd_board_b(xoff + k), y0 = trx_bd_trx_bd_board_a(xoff + k);
                a = trx_bd_md_mpy(y1, x1);
                a = trx_bd_md_asl(a, 2);
                a = trx_bd_md_mac(a, y0, x0);
                a = trx_bd_md_add(a, trx_bd_md_from24(md_w24(y0w[k])));
                output[k] = trx_bd_md_acc_to_word(a);
            }
            s->board_ofs = 0x20 + xoff;
        }
        return;
    }

    /* osc 1 into X at 0x20: a 48-bit phase whose index is A1 & 0x7fff */
    {
        uint32_t ph, pl, sh, sl;
        a = trx_bd_md_asr(trx_bd_md_from24(s->ptch), 6);
        b = trx_bd_md_asr(trx_bd_md_mpy(s->clip_th, s->ramp), 1);
        a = trx_bd_md_add(a, b);
        sh = trx_bd_md_mid(a); sl = trx_bd_md_low(a);
        ph = s->phase_a & 0xffffffu; pl = s->phase_b & 0xffffffu;
        /* A2 never reaches A1 or A0, and and_mid clears A1's top 9 bits each
         * step: the phase is A1 & 0x7fff : A0 plus the step's 48 bits */
        for (k = 0; k < 32; ++k) {
            uint32_t lo;
            ph &= 0x7fffu;
            x20[k] = md_s(trx_bd_bd_sine((uint16_t)ph)) >> 2;      /* asr 2: never limited */
            lo = pl + sl;
            ph = ph + sh + (lo >> 24);
            pl = lo & 0xffffffu;
        }
        s->phase_a = ph & 0xffffffu;
        s->phase_b = pl;
    }
    /* osc 2 into Y at 0x20, an octave down: asl 1, limited */
    {
        uint32_t ph, pl, sh, sl, y1 = s->clip_th;
        a = trx_bd_md_asr(trx_bd_md_from24(s->ptch), 7);
        b = trx_bd_md_asr(trx_bd_md_mpy(y1, s->ramp), 1);
        a = trx_bd_md_add(a, b);
        s->clip_th = md_w24(md_mid_mpy_exact(md_s(y1), md_s(s->rdec)));
        sh = trx_bd_md_mid(a); sl = trx_bd_md_low(a);
        ph = s->ph2_a & 0xffffffu; pl = s->ph2_b & 0xffffffu;
        for (k = 0; k < 32; ++k) {
            uint32_t lo;
            ph &= 0x7fffu;
            y20[k] = md_sat(2 * md_s(trx_bd_bd_sine((uint16_t)ph)));
            lo = pl + sl;
            ph = ph + sh + (lo >> 24);
            pl = lo & 0xffffffu;
        }
        s->ph2_a = ph & 0xffffffu;
        s->ph2_b = pl;
    }
    /* X += Y * ph2_step / 16: from24(X) + asr(mpy, 4), limited */
    {
        int32_t step = md_s(s->ph2_step);
        for (k = 0; k < 32; ++k) x20[k] = md_sat(x20[k] + md_mul_shift(y20[k], step, 27));
        s->ph2_step = md_w24(md_word_mpy_exact(step, md_s(s->slew)));
    }
    /* the fold's references, once a block */
    {
        uint32_t x0 = s->fold_ref, x1 = s->fold_ref2;
        int32_t r0 = md_s(x0), r1 = md_s(x1), lo = md_s(s->fold_lo), hi = md_s(s->fold_hi), e = md_s(s->exc);
        a = trx_bd_md_add(trx_bd_md_from24(s->exc), trx_bd_md_from24(x0));
        s->fold_ref = trx_bd_md_acc_to_word(a);
        a = trx_bd_md_sub(0, trx_bd_md_from24(s->exc));
        a = trx_bd_md_add(a, trx_bd_md_from24(x1));
        s->fold_ref2 = trx_bd_md_acc_to_word(a);
        /* b = -X * 2^23 against r0 * 2^24 and r1 * 2^24: compare -X with 2 r */
        for (k = 0; k < 32; ++k) {
            int32_t v = -x20[k];
            if (v > 2 * r0) {
                t = lo - e;                                 /* from24(y0) - exc, A2 kept */
                hi = r1; lo = md_s(md_w24(t));
                x20[k] = md_sat(2 * t);
            } else if (v < 2 * r1) {
                t = hi + e;
                lo = r0; hi = md_s(md_w24(t));
                x20[k] = md_sat(2 * t);
            } else x20[k] = md_sat(v);                      /* asl of -X * 2^23 */
        }
        s->fold_lo = md_w24(lo);
        s->fold_hi = md_w24(hi);
    }
    /* the decay: X = env * (X + noise), env *= dec; mpy + mac, then the word */
    {
        int32_t env = md_s(s->decay_acc), dec = md_s(s->dec);
        for (k = 0; k < 32; ++k) {
            x20[k] = md_sat(md_mid_mpy_sum(env, x20[k], md_s((uint32_t)y0w[k])));   /* env (X + noise) */
            env = md_mid_mpy_exact(env, dec);                        /* feeds back */
            env = md_s(md_w24(env));
        }
        s->decay_acc = md_w24(env);
    }
    /* The clipper into X at 0x40. Above c0 (or below c1) the DSP takes
     * q = the word of X - c, makes a = 2 q c0 + c * 2^24, then a += 2 y1 mid(a)
     * and keeps the word: c + floor((q c0 + y1 r) / 2^23), r = mid(a). In
     * between, a = X * 2^24 and the word is X + floor(y1 X / 2^23). */
    {
        int32_t y1 = md_s(s->clip[4]), c0 = md_s(s->clip[2]), c1 = md_s(s->clip[3]);
        for (k = 0; k < 32; ++k) {
            int32_t v = x20[k], c, q, r;
            if (v >= c0) c = c0;
            else if (v <= c1) c = c1;
            else { x40[k] = md_sat(v + md_mul_shift(y1, v, 23)); continue; }
            q = md_s(md_w24(v - c));
            r = md_s(md_w24(c + md_mul_shift(q, c0, 23)));
            x40[k] = md_sat(c + md_mid_mpy2(q, c0, y1, r));
        }
    }
    /* out = 2 (X40 + X20), pairwise as the DSP reads them, limited */
    for (k = 0; k < 32; ++k) output[k] = md_w24(md_sat(2 * (x40[k] + x20[k])));
    s->tail_amp = output[31];
}
