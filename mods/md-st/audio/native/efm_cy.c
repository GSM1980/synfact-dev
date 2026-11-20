/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/efm_cy.c - EFM-CY's render, word for word mdkit's mdkit_efm_cy_render
 * (mdkit/src/efm/cy/mdkit_efm_cy.c), in 32-bit integers. Four operators, each a
 * wave scanner then an FM operator with self feedback, mixed, scaled by the
 * amplitude envelope and through two resonators:
 * - scan: wave = mid(mpyr(MOD amplitude, sine(pointer))), the pointer stepped in
 *   its low 15 bits (its tag 0x148000 never reaches the sine).
 * - the four operators share one envelope, read before its step: acc_to_word of
 *   an mpyr is its limited word.
 * - FM: mdkit adds feedback + wave to the phase, masks A1 to 15 bits, reads the
 *   sine and takes the feedback off again before adding the rate. Every term is
 *   a word on A1 (A0 stays zero: from24 words and mpyr's rounded results), and
 *   only the low 15 bits of A1 reach the sine, so the phase steps by the rate
 *   modulo 2^15 and the feedback only offsets the index. The stored phase is
 *   mdkit's own: idx - (feedback + wave) + rate of the last sample, modulo 2^24.
 * - the mix is floor((w0 + w1 + w2 + w3) / 4), never past the limit.
 * - the amplitude gain is acc_to_word(asl(mpy_su(env, fb_env_decay), 1)): with
 *   fb_env_decay = 0x400000, which cy_build_controls always sets, that is env
 *   itself (env << 24 on A1, A0 zero, in range).
 * - the resonators are EFM-HH's (efm_hh.c, hh_resonance): the rounded MAC.
 * - The coefficients are made again only when the clock's current values move
 *   (efm_clock.c's boundary): they are a function of them alone.
 * Included by the generated voice in place of md_st_efm_cy_render_words
 * (tools/md_port.py, NATIVE). tests/test_md_st.py compares every word.
 */
#include "../voices/md-native.h"
/* a sine word, signed: the whole table's (md-tables.h FULL_SINES), cy_sine's words */
#ifdef MD_ST_TABLES
#define CY_SINE(i) (md_efm_cy_sine.full[(i) & 0x7fffu])
#else
#define CY_SINE(i) md_s(cy_sine((uint16_t)(i)))
#endif
/* mid(mpy(x, y)) for a decay or a gain of at most 1: exact, or (MD_AP_MID) the EMAC's
 * read alone, short by a unit at most */
#ifdef MD_AP_MID
#define CY_MID_MPY(x, y) (md_ar1((x), (y)) >> 6)
#else
#define CY_MID_MPY(x, y) md_mid_mpy_exact((x), (y))
#endif
/* round(x * y / 2^23) for a y of at most 16 bits (the scan gain, at most 0xca80; FB, at
 * most 12954): exact, or (MD_AP_MID) x's top 16 bits by y in one 32-bit multiply,
 * within 1.5 units */
#ifdef MD_AP_MID
#define CY_MPYR_SMALL(x, y) ((((x) >> 8) * (y) + 0x4000) >> 15)
#else
#define CY_MPYR_SMALL(x, y) md_mpyr((x), (y))
#endif

static void efm_cy_resonance(const int32_t in[32], int32_t out[32], uint32_t coefficient, uint32_t *history, uint32_t *delay)
{
    const uint32_t gain = mdkit_mid(mdkit_add(mdkit_asr(mdkit_from24(coefficient), 1), mdkit_from24(0x400000)));
    const int32_t g = md_s(gain), c = md_s(coefficient);
    int32_t hist = md_s(*history), previous = md_s(*delay);
    for (unsigned i = 0; i < 32; ++i) {
        const int32_t x = in[i], old = hist;
        hist = CY_MID_MPY(x, g);              /* g <= 1: in range */
        previous = md_round_sum2_less(x, g, previous, c, old);
        out[i] = previous;
    }
    *history = md_w24(hist); *delay = md_w24(previous);
}

void md_st_efm_cy_render_words(void *state, int32_t *out, unsigned frames)
{
    mdkit_efm_cy *s = (mdkit_efm_cy *)state;
    while (frames) {
        if (s->pos == 0u) {
            if (efm_clock_boundary(&s->clock)) cy_build_controls(s);
            s->have_block = 0;
        }
        if (!s->have_block) {
            int32_t wave[4][32], mix[32], tail[32], block[32];
            const int32_t AMP = md_s(s->wave_amp), WD = md_s(s->wave_env_decay), FB = md_s(s->fm_fb), D = md_s(s->env_decay);
            int32_t env;
            unsigned op, i;
            /* The ranges make wrap and limit no-ops below: MOD's amplitude is at most
             * 0xca80, the envelopes are 0..0x7fffff and their decays under 1, the mix
             * of four words divided by 4 is a word. */
#ifdef MD_AP_MID
            /* MD_AP_MID: the scans' amplitude times the shared envelope as one gain a
             * sample, then one product an operator (each within a unit of mdkit's two) */
            {
                int32_t gain[32];
                env = md_s(s->wave_env);
                for (i = 0; i < 32; ++i) {gain[i] = md_mpyr(AMP, env); env = CY_MID_MPY(WD, env);}
                s->wave_env = md_w24(env);
                for (op = 0; op < 4; ++op) {
                    uint32_t pointer = s->scan_ptr[op];
                    const uint32_t step = s->scan_step[op];
                    for (i = 0; i < 32; ++i) {
                        wave[op][i] = CY_MPYR_SMALL(CY_SINE(pointer), gain[i]);
                        pointer = 0x148000u | ((pointer + step) & 0x7fffu);
                    }
                    s->scan_ptr[op] = pointer;
                }
            }
#else
            /* the wave scans */
            for (op = 0; op < 4; ++op) {
                uint32_t pointer = s->scan_ptr[op];
                const uint32_t step = s->scan_step[op];
                for (i = 0; i < 32; ++i) {
                    wave[op][i] = md_mpyr(AMP, CY_SINE(pointer));
                    pointer = 0x148000u | ((pointer + step) & 0x7fffu);
                }
                s->scan_ptr[op] = pointer;
            }
            /* one envelope for the four, read before its step */
            env = md_s(s->wave_env);
            for (i = 0; i < 32; ++i) {
                for (op = 0; op < 4; ++op) wave[op][i] = md_mpyr(env, wave[op][i]);
                env = CY_MID_MPY(WD, env);
            }
            s->wave_env = md_w24(env);
#endif
            /* the FM scans */
            for (op = 0; op < 4; ++op) {
                uint32_t phase = s->fm_phase[op], f = 0, idx = 0;
                int32_t feedback = md_s(s->fm_fb_reg[op]), sample = 0;
                const uint32_t rate = s->fm_rate[op];
                for (i = 0; i < 32; ++i) {
                    f = (uint32_t)feedback + (uint32_t)wave[op][i];
                    idx = (phase + f) & 0x7fffu;
                    sample = CY_SINE(idx);
                    phase += rate;
                    wave[op][i] = sample;
                    feedback = CY_MPYR_SMALL(sample, FB);
                }
                s->fm_phase[op] = (idx - f + rate) & 0xffffffu;
                s->fm_fb_reg[op] = md_w24(md_wrap(feedback));
            }
            /* the mix */
            for (i = 0; i < 32; ++i) mix[i] = (wave[0][i] + wave[1][i] + wave[2][i] + wave[3][i]) >> 2;
            env = md_s(s->amp_env);
            for (i = 0; i < 32; ++i) {
                const int32_t next = CY_MID_MPY(D, env);
                mix[i] = md_mpyr(env, mix[i]);
                env = next;
            }
            s->amp_env = md_w24(env);
            efm_cy_resonance(mix, tail, s->hp_coef, &s->hp1_hist, &s->hp1_delay);
            efm_cy_resonance(tail, block, s->hp_coef, &s->hp2_hist, &s->hp2_delay);
            for (i = 0; i < 32; ++i) s->block[i] = md_w24(block[i]);
            s->have_block = 1;
        }
        {
            unsigned n = 32u - s->pos, i;
            if (n > frames) n = frames;
            for (i = 0; i < n; ++i) out[i] = md_s(s->block[s->pos + i]);
            out += n; frames -= n; s->pos += n;
            if (s->pos >= 32u) s->pos = 0;
        }
    }
}
