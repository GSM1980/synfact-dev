/* SPDX-License-Identifier: GPL-2.0-or-later
 * native/efm_bd.c - EFM-BD in fixed point, after mdkit's (mdkit/src/efm/bd).
 *
 * mdkit's EFM-BD is not the Machinedrum's DSP code but a model of it in double
 * precision: two sine operators, three exponential envelopes, phases in doubles.
 * The Syntakt has no FPU, so this is a port of that model, not of its words:
 * tests/md_st_voice_model.c measures it against mdkit's (its envelopes, and without
 * feedback its attacks and its phases; with feedback the model is chaotic).
 *   - Envelopes are Q31 (fb Q27: a retrigger adds 0.630957 to it), and each is its
 *     value at the block's start (or the note) times its decay's k-th power: one
 *     rounding a block, not one a sample (mdkit's doubles decay smoothly to 0; a
 *     sample-by-sample Q31 product would drift or stall). The decays are kept as
 *     1 - decay in Q37, and the 32nd power, by which a base moves on, is made by
 *     squaring in that form (md_efm_bd_powers).
 *   - Phases are 32768 to a cycle in Q15.17 (wrapping at 2^32 is mdkit's modulo);
 *     the sine is TRX-BD's quarter table read between words. ptch is Q37 and y1 Q34,
 *     so that a low note's phase advance keeps its bits.
 *   - The control clock is mdkit's, in float32 (a few operations every 12 blocks,
 *     md-runtime.c). The coefficients (exponents from the float knobs into Q24,
 *     through mdkit's own 2^x, its polynomial: md_scale_exp2) and the powers are made
 *     again only when the clock's values move (md_efm_bd_refresh); mdkit recomputes
 *     them each render from the same values.
 * The output is the word of fb * s2, limited to 24 bits (mdkit's float can pass 1.0
 * after quick retriggers).
 */
#include "efm_bd.h"
#include "../voices/md-native.h"
#include "../voices/md-tables.h"

#ifdef MD_EMAC
static inline int32_t md_q31(int32_t a, int32_t b)          /* floor(a b / 2^31): the EMAC's fractional read */
{
    int32_t r;
    __asm__ volatile ("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0" : "=r"(r) : "r"(a), "r"(b));
    return r;
}
#else
static inline int32_t md_q31(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 31); }
#endif
/* round(a b / 2^31) for the envelopes (a floor's bias would add up over a note): the
 * floor plus the product's bit 30, from its low 32 bits (muls.l) */
static inline int32_t md_q31r(int32_t a, int32_t b) { return md_q31(a, b) + (int32_t)(((uint32_t)a * (uint32_t)b >> 30) & 1u); }
/* round(a b / 2^30): twice the floor, plus the product's bits 30 and 29 rounded */
static inline int32_t md_q30r(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)md_q31(a, b) << 1) + (int32_t)(((((uint32_t)a * (uint32_t)b) >> 29 & 3u) + 1u) >> 1);
}
#define MD_EFM_BD_FB_ADD 84685615     /* mdkit adds 0.630957 (about 0x50c335 / 2^23): round(0.630957 2^27) */

/* c * mdkit_exp2(x / 2^24): mdkit's own 2^x, the integer part as a shift and the
 * fraction f through its degree-4 polynomial 1 + f (c1 + f (c2 + f (c3 + f c4))),
 * Horner in Q31 (the polynomial, not the true 2^f: mdkit's pitch and decays) */
static int32_t md_scale_exp2(int32_t c, int32_t x, int e)       /* x Q24; c has e more fraction bits than the result */
{
    const int32_t n = x >> 24;                       /* floor */
    const int32_t f = (int32_t)(((uint32_t)x & 0xffffffu) << 7);      /* Q31 */
    int32_t p = 20654775;                                  /* 0.009618129107628477 */
    p = 119194166 + md_q31(f, p);                           /* 0.05550410866482158 */
    p = 515882496 + md_q31(f, p);                           /* 0.2402265069591007 */
    p = 1488522236 + md_q31(f, p);                           /* 0.6931471805599453 */
    const uint32_t v = 0x40000000u + (uint32_t)(md_q31(f, p) >> 1);  /* the polynomial, Q30 */
    const int64_t prod = (int64_t)c * (int64_t)v;
    const int sh = 30 + e - n;
    return (int32_t)(sh >= 63 ? 0 : sh >= 0 ? prod >> sh : prod << -sh);
}

/* floor(v 2^24), as mdkit's exp2 takes the integer part (a tiny negative x is n = -1 and
 * f near 1, where its polynomial is 1.99847, not 2). No float compare (the runtime has
 * none): v * 2^24 is exact, the truncation is __fixsfsi, and a negative non-integer is
 * told by the bits of v * 2^24 and of the truncation's float. */
typedef union { float f; uint32_t u; } md_f32_bits;
static inline int32_t md_q24(float v)
{
    md_f32_bits t, back;
    t.f = v * 16777216.0f;
    const int32_t i = (int32_t)t.f;
    back.f = (float)i;
    return (t.u & 0x80000000u) && back.u != t.u ? i - 1 : i;
}
/* max(1, k) for a knob k >= 0: positive floats order as their bits */
static inline float md_at_least_1(float k) { md_f32_bits b, one; b.f = k; one.f = 1.0f; return b.u < one.u ? 1.0f : k; }
static struct md_efm_bd_coeff md_efm_bd_coeff(const float k[8])
{
    struct md_efm_bd_coeff c;
    c.ptch = md_scale_exp2(319408128, md_q24(k[0] * (1.0f / 32.0f)), 2);                             /* 0.000581 2^(k/32), Q37 */
    /* the decays as 1 - decay, Q37 (a Q31 decay's rounding, 2^-32 a sample, would be 1e-5 a second) */
    c.dec  = md_scale_exp2(1301409450, md_q24(k[1] * (-1.0f / 13.5f)), 0);     /* 0.009469 2^(-k/13.5) */
    c.rdec = md_scale_exp2(1301272011, md_q24(k[3] * (-1.0f / 12.6f)), 0);     /* 0.009468 2^(-k/12.6) */
    c.mdec = md_scale_exp2(1302096645, md_q24(k[6] * (-1.0f / 11.52f)), 0);    /* 0.009474 2^(-k/11.52) */
    c.ramp = md_q24(k[2] - 16.0f);              /* (k - 16) / 128 in Q31 is (k - 16) 2^24 */
    c.mod  = md_q24(k[4]);                      /* k / 128 */
    c.mfb  = md_q24(k[7]);
    c.mfrq = md_q24(md_at_least_1(k[5]));      /* max(1, k) / 128 */
    return c;
}

static const unsigned char md_efm_bd_defaults[8] = {0, 64, 32, 127, 35, 14, 64, 24};

/* mdkit's control clock (efm_clock_seed, efm_clock_boundary), as it is */
static void md_efm_bd_init(void *state)
{
    md_efm_bd *s = state;
    mdkit_control_clock *c = &s->clock;
    c->count = 8;
    for (unsigned i = 0; i < 8; ++i) { const float v = (float)md_efm_bd_defaults[i]; c->target[i] = c->raw[i] = c->current[i] = c->pending[i] = v; }
    c->block = 0; c->next_update = 0; c->publish_block = 0; c->pending_valid = 0;
    s->pos = 0;
    s->ph1 = s->ph2 = s->mod = 0;
    s->base1 = s->base2 = s->basef = 0; s->k = 0;
    for (unsigned i = 0; i < 8; ++i) s->made[i] = 0xffffffffu;
}

static void md_efm_bd_set_control(void *state, unsigned i, unsigned v)
{
    md_efm_bd *s = state;
    if (i < s->clock.count) s->clock.target[i] = (float)(v > 127u ? 127u : v);
}

static void md_efm_bd_boundary(md_efm_bd *s)
{
    mdkit_control_clock *c = &s->clock;
    if (c->pending_valid && c->block >= c->publish_block) {
        for (unsigned i = 0; i < c->count; ++i) c->current[i] = c->pending[i];
        c->pending_valid = 0;
    }
    if (c->block == c->next_update) {
        for (unsigned i = 0; i < c->count; ++i) { c->pending[i] = c->raw[i]; c->raw[i] = (3.0f * c->raw[i] + c->target[i]) * 0.25f; }
        c->pending_valid = 1;
        c->publish_block = c->block + 1;
        c->next_update += 12;
    }
    c->block += 1;
}

static inline int32_t md_efm_bd_env(int32_t base, const int32_t *p, unsigned k) { return k ? md_q31r(base, p[k]) : base; }

static void md_efm_bd_powers(int32_t p[33], int32_t x);
/* the coefficients and the powers, again only when the clock's values have moved (every
 * 12 blocks at most; float and 64-bit work otherwise twice a block) */
static void md_efm_bd_refresh(md_efm_bd *s)
{
    md_f32_bits v;
    unsigned i, moved = 0;
    for (i = 0; i < 8; ++i) { v.f = s->clock.current[i]; moved |= v.u ^ s->made[i]; s->made[i] = v.u; }
    if (!moved) return;
    s->c = md_efm_bd_coeff(s->clock.current);
    md_efm_bd_powers(s->p1, s->c.rdec); md_efm_bd_powers(s->p2, s->c.mdec); md_efm_bd_powers(s->pf, s->c.dec);
}

static void md_efm_bd_trigger(void *state)
{
    md_efm_bd *s = state;
    const int32_t fb = md_efm_bd_env(s->basef, s->pf, s->k);
    md_efm_bd_refresh(s);
    s->base1 = s->c.ramp;
    s->base2 = 0x7fffffff;                     /* 1.0 */
    s->basef = fb > 0x7fffffff - MD_EFM_BD_FB_ADD ? 0x7fffffff : fb + MD_EFM_BD_FB_ADD;   /* + 0.630957 (Q27: room to 16) */
    s->k = 0;
    s->ph1 = s->ph2 = 4000u << 17;             /* 0x000fa0, index units */
    s->mod = 0;
}

/* a block's powers of a decay 1 - x (x Q37, under 0.0095): the 1st to 31st in Q31 by
 * products (rounded once each, from the block's base: no error carries across blocks),
 * the 32nd, by which each base moves on, by squaring 1 - x five times in that form,
 * 1 - (2x - x^2), x one bit coarser each time (Q37 to Q32): its error, about 2^-32,
 * is what adds up over a note */
static void md_efm_bd_powers(int32_t p[33], int32_t x)
{
    p[0] = 0x7fffffff;
    p[1] = (int32_t)(0x80000000u - (uint32_t)((x + 32) >> 6));
    for (unsigned j = 1; j < 31; ++j) p[j + 1] = md_q31r(p[j], p[1]);
    for (int sh = 7; sh > 2; --sh) x -= (int32_t)(((uint32_t)md_q31(x, x) + (1u << (sh - 1))) >> sh);
    p[32] = (int32_t)(0x80000000u - (uint32_t)((x + 1) >> 1));
}

/* the sine at a Q15.17 phase, Q31: TRX-BD's quarter (mdkit's is the true sine; the
 * table's trough fix, a word from -1.0, is not read), folded once for the word and the
 * next, read between them */
static inline int32_t md_efm_bd_sine(uint32_t p)
{
    const unsigned i = p >> 17, q = i & 16383u;
    const uint32_t *w = md_trx_bd_sine_q;
    int32_t a, d;
    if (q < 8192u) { a = (int32_t)w[q]; d = (int32_t)w[q + 1u] - a; }
    else { a = (int32_t)w[16384u - q]; d = (int32_t)w[16383u - q] - a; }
    a = (a << 8) + ((d * (int32_t)(p & 0x1ffffu)) >> 9);
    return i & 16384u ? -a : a;
}

void md_st_efm_bd_render_words(void *state, int32_t *out, unsigned frames)
{
    md_efm_bd *s = state;
    while (frames) {
        if (s->pos == 0u) { md_efm_bd_boundary(s); md_efm_bd_refresh(s); }
        const struct md_efm_bd_coeff c = s->c;
        unsigned n = 32u - s->pos, k = s->k;
        if (n > frames) n = frames;
        uint32_t ph1 = s->ph1, ph2 = s->ph2, mod = s->mod;
        for (unsigned i = 0; i < n; ++i, ++k) {
            /* the envelopes before this sample's decay, mdkit's op1_prev, op2_prev and fb. In
             * the loop the products are floors (p[0] is 1 - 2^-31): their bias, 2^-32, adds up
             * nowhere but in op1's phase, which is rounded (md_q30r; ph2's, 2^-17 an index a
             * sample, is under 1e-6 of its advance), and at the block's end, rounded */
            const int32_t op1_prev = md_q31(s->base1, s->p1[k]);
            const int32_t op2_prev = md_q31(s->base2, s->p2[k]);
            const int32_t fb = md_q31(s->basef, s->pf[k]);
            /* y1 = ptch (1 + 8 op1), Q34: 1 + 8 op1 is 2^28 + op1 in Q28, exactly (the two nearly
             * cancel at low RAMP); ptch, under 0.0091, is Q37 (at PTCH 0 and MFRQ 1, op1's phase
             * would otherwise advance by a word of 14 bits) */
            const int32_t y1 = md_q31(c.ptch, (int32_t)(0x10000000 + op1_prev));
            const int32_t s1 = md_efm_bd_sine(ph1 + mod);
            ph1 += (uint32_t)md_q30r(c.mfrq, y1);                                     /* 262144 mfrq y1 */
            mod = (uint32_t)md_q31(c.mfb, s1) << 1;                                  /* 32768 mfb s1 */
            const int32_t x8 = md_q31(md_q31(op2_prev, s1), y1);                     /* 8 x1c = 8 op2 s1 y1 */
            const int32_t s2 = md_efm_bd_sine(ph2);
            ph2 += ((uint32_t)md_q31(c.mod, x8) << 1) + ((uint32_t)(y1 + 2) >> 2);    /* 32768 (8 mod x1c + y1) */
            const int32_t o = md_q31(fb, s2);                                        /* Q27 */
            const int32_t w = (o + 8) >> 4;                                          /* Q23 */
            out[i] = w > 0x7fffff ? 0x7fffff : w < -0x800000 ? -0x800000 : w;
        }
        s->ph1 = ph1; s->ph2 = ph2; s->mod = mod; s->k = k;
        out += n; frames -= n; s->pos += n;
        if (s->pos >= 32u) {                   /* the block's end: the envelopes' new bases */
            if (k) { s->base1 = md_q31r(s->base1, s->p1[k]); s->base2 = md_q31r(s->base2, s->p2[k]);
                     s->basef = md_q31r(s->basef, s->pf[k]); }
            s->k = 0;
            s->pos = 0;
        }
    }
}

#ifndef MD_ST_TARGET
static void md_efm_bd_render(void *state, float *out, unsigned frames)
{
    int32_t w[64];
    while (frames) {
        const unsigned n = frames > 64u ? 64u : frames;
        md_st_efm_bd_render_words(state, w, n);
        for (unsigned i = 0; i < n; ++i) out[i] = (float)w[i] / 8388608.0f;
        out += n; frames -= n;
    }
}
#endif

static const char *const md_efm_bd_names[8] = {"PTCH", "DEC", "RAMP", "RDEC", "MOD", "MFRQ", "MDEC", "MFB"};
const mdkit_voice mdkit_voice_efm_bd = {
    "EFM-BD", 32u, 8, md_efm_bd_names, sizeof(md_efm_bd),
    md_efm_bd_init, md_efm_bd_set_control, md_efm_bd_trigger, MD_ST_RENDER(md_efm_bd_render), md_efm_bd_defaults
};
