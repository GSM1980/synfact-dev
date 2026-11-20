/* SPDX-License-Identifier: GPL-2.0-or-later
 * md.c - the MD machine's AUDIO: one Machinedrum voice per track, chosen by
 * TYPE, at the Machinedrum's 44.1 kHz and resampled to the Syntakt's 48 kHz,
 * then through the track's stock chain as the sample machines are.
 *
 * Controls 0..7 are the voice's eight knobs (0..127 in the Sound word's high
 * byte); control 8 is TYPE, which a note latches, as on the Machinedrum: a
 * p-locked TYPE changes the voice step by step. The knobs reach the voice
 * through its own control clock (mdkit), which applies them at its blocks: a
 * turned knob glides there, a note takes them at once (md_snap).
 *
 * A track with a voice chosen renders it on every block, from the moment the
 * voice is loaded, sounding or not, as the stock machines do: loading a voice
 * on a track shows what it costs (MD_REST 0, the default for now). With
 * MD_REST 1, a voice whose output has stayed within MD_REST_LEVEL of zero for
 * MD_REST_BLOCKS of its blocks rests until its next note: it is not rendered,
 * and the track is silent; most PI voices never get that quiet (their tails
 * stay a few units from zero, mdkit's too), so they would not rest anyway.
 * A track with no voice chosen (TYPE past the last) always rests.
 */
#include <syntakt/machines.h>
#include "md-voices.h"
#include "voices/md-native.h"

/* The stock routines and memory this file uses; tests/md_st_glue_check.c
 * defines its own before including it. */
#ifndef MD_STOCK
#define MD_EXTENDED st_machine_extended
#define MD_TRACK_BASE 0x80000000u
#define MD_SCRATCH_A ((int32_t *)0x80008c60u)
#define MD_SCRATCH_B ((int32_t *)0x80008d84u)
#define MD_TWINSHOT_UPDATE(pitch, voice, native)     ((void (*)(uint32_t, void *, const void *))0x400097dcu)(pitch, voice, native)
#define MD_TWINSHOT_CHAIN(a, b, out, locals, voice) do {     ((void (*)(void *, void *, void *))0x400068eau)(a, a, (unsigned char *)(voice) + 0x6f8);     ((void (*)(void *))0x40003d42u)(voice);     ((void (*)(void *, void *))0x400045a0u)(a, voice);     ((void (*)(void *, void *, void *, void *, void *))0x400049f4u)(a, b, out, locals, voice); } while (0)
#endif

#define MD_TRACKS 8u
#ifndef MD_REST
#define MD_REST 0                         /* 1: a silent voice rests until its next note */
#endif
#define MD_REST_BLOCKS 32u               /* about 23 ms of the voice's blocks */
#define MD_REST_LEVEL 4                   /* 2^-21 of full scale */
/* 44100 / 48000 = 147 / 160: the voice advances 147/160 of a sample per output */
#define MD_STEP 147u
#define MD_UNIT 160u

struct md_track {
    uint32_t loaded;                      /* voice index + 1; 0: none yet */
    uint32_t knob[8];                     /* the values the voice has been given */
    uint32_t type;                        /* TYPE as last read */
    uint32_t phase;                       /* 0..159, in 1/160 of a voice sample */
    short near[4];                        /* the last four words read, rounded to 16 bits */
    int32_t block[32];                    /* the voice's current block */
    uint32_t at;                          /* next sample of block; 32: render one */
    uint32_t quiet;                       /* blocks in a row within MD_REST_LEVEL */
    uint32_t resting;
    uint32_t blocked;                     /* its voice is past the MD's budget: silent */
    union md_voice_state state __attribute__((aligned(4)));
};
static struct md_track md_tracks[MD_TRACKS];

/* The MD's budget (md-voices.h, main.c): the MD tracks' voices' weights stay within
 * MD_BUDGET. MAIN's list offers only voices that fit; a project or a kit can still bring
 * more, and the voice that would pass it stays silent, unrendered, until it fits (a track
 * ahead of it in the block's order keeps its voice). A track counts while md_update
 * sees it: md_round counts the block's passes over the MD tracks (a track index that does
 * not grow starts one), md_seen when each was last updated. */
static uint32_t md_round, md_last = MD_TRACKS, md_seen[MD_TRACKS];
static int md_budget_fits(int t, uint32_t voice)
{
    uint32_t u, sum = md_voice_weight[voice];
    for (u = 0; u < MD_TRACKS; ++u) {
        const struct md_track *o = md_tracks + u;
        if ((int)u == t || md_round - md_seen[u] > 2u || !o->loaded || o->blocked || o->type >= MD_VOICES) continue;
        sum += md_voice_weight[o->loaded - 1u];
    }
    return sum <= MD_BUDGET;
}

/* 4-point, third-order Hermite weights for each of the 160 phases, Q14:
 * w[p] = {w(-1), w(0), w(1), w(2)} at f = p / 160 (tools/md_port.py). */
#include "md-weights.h"

/* The EMAC. The first update tests it against the C forms of the same
 * arithmetic (md-native.h, md-acc.h) on fixed and random words; if any differs,
 * the machine stays silent (a unit whose EMAC is not what analog-emu models
 * would play wrong words). md_emac_status: 0 not tested, 1 used, 2 failed. */
unsigned md_emac_status;
#ifdef MD_EMAC
#include "voices/md-acc.h"
#define MD_VOICE(i) (md_voice[i])
#define MD_WORDS(i) (md_voice_words[i])
static uint32_t md_test_seed = 0x2545f491u;
static int32_t md_test_word(void)
{
    md_test_seed = md_test_seed * 1664525u + 1013904223u;
    return md_s(md_test_seed >> 8);
}
static int md_emac_selftest(void)
{
    static const int32_t edge[10] = {0, 1, -1, 0x7fffff, -0x800000, 0x400000, -0x400000, 0x1ff, -0x200, 0x123456};
    struct md_emac_saved e;
    unsigned i;
    int ok = 1;
    md_emac_begin(&e);
    for (i = 0; i < 400 && ok; ++i) {
        int32_t x = i < 100 ? edge[i % 10] : md_test_word(), y = i < 100 ? edge[i / 10] : md_test_word();
        int32_t u = md_test_word(), v = i < 100 ? edge[i % 10] : md_test_word(), f, h1, h2;
        ok &= md_mul_shift_exact(x, y, 23) == md_mul_shift_c(x, y, 23);
        ok &= md_mul_shift_exact(x, y, 27) == md_mul_shift_c(x, y, 27);
        ok &= md_mid_mpy_sum(x, y, u) == md_mid_mpy2_c(x, y, u, x);
        ok &= md_mul_floor48(x, y, 7, &h1) == md_mul_floor48_c(x, y, 7, &h2) && h1 == h2;
        ok &= md_mul_floor48(x, y, 4, &h1) == md_mul_floor48_c(x, y, 4, &h2) && h1 == h2;
        /* the reads the approximate forms are made of (MD_ST_APPROX), against their C forms */
        ok &= md_mpyr_approx(x, y) == md_mpyr_c_approx(x, y);
        ok &= md_ar2_first(x, y, u, v, &f) == md_ar2_c(x, y, u, v) && f == md_ar1_c(x, y);
        ok &= md_ar3(x, y, u, v, v, x) == md_ar3_c(x, y, u, v, v, x);
        ok &= md_ar3s(MD_EX(x), MD_EY(y), MD_EX(u), MD_EY(v), MD_EX(v), MD_EY(x)) == md_ar3_c(x, y, u, v, v, x);
        ok &= md_ar1(x, u + v) == md_ar1_c(x, u + v);     /* MD_AP_ACC's products (md_acc_from_read) */
        /* the exact ones (md.c sets no MD_AP_*) */
        ok &= md_mpyr(x, y) == md_mpyr_c(x, y);
        ok &= md_mid_mpy2_first(x, y, u, v, &f) == md_mid_mpy2_c(x, y, u, v) && f == md_mul_shift_c(x, y, 23);
        ok &= md_acc_product(x, y, 1) == md_acc_product_c(x, y, 1) && md_acc_product(x, y, 0) == md_acc_product_c(x, y, 0);
    }
    md_emac_end(&e);
    return ok;
}
static void md_pick_voices(void)
{
    if (md_emac_status) return;
    if (md_emac_selftest()) md_emac_status = 1;
    else md_emac_status = 2;
}
#else
#define MD_VOICE(i) (md_voice[i])
#define MD_WORDS(i) (md_voice_words[i])
static void md_pick_voices(void) {}
#endif

static int md_track_of(const void *voice)
{
    uint32_t at = (uint32_t)(uintptr_t)voice - MD_TRACK_BASE, t = at / 0x708u;
    return t < MD_TRACKS && at == t * 0x708u ? (int)t : -1;
}

static void md_load(struct md_track *s, uint32_t type)
{
    const mdkit_voice *v = MD_VOICE(type);
    uint32_t i;
    v->init(&s->state);
    s->loaded = type + 1u;
    for (i = 0; i < 8; ++i) s->knob[i] = 0xffffffffu;   /* every knob is sent again */
    for (i = 0; i < 4; ++i) s->near[i] = 0;
    s->phase = 0; s->at = 32u; s->quiet = 0; s->resting = 1;
}

/* A note plays its knobs as they are: a p-lock, or a Sound that a note brings, is
 * the voice's from that note's first block, as the Machinedrum's sequencer gives
 * them. mdkit's control clock moves its values a quarter of the way to the knobs
 * every 12 blocks, the way a turned knob glides; at a note that would be a slide
 * from the last note's values, or from a voice's defaults just after it is
 * loaded. So the note sets the knobs in the clock itself: target the knob, raw,
 * pending and current the same, and pending published at the voice's next block,
 * so the voice works its coefficients out again from them. Every voice's state
 * begins with the clock; it keeps floats or integers (MD_FLOAT_CLOCKS, md-voices.h:
 * the integers are the knob, and 128 times it for raw, pending and current). */
typedef uint32_t __attribute__((may_alias)) md_clock_word;
static uint32_t md_float_bits(uint32_t k)       /* (float)k, 0..127, as its bits */
{
    uint32_t e = 0;
    if (!k) return 0;
    while (k >> (e + 1u)) ++e;
    return (127u + e) << 23 | ((k << (23u - e)) & 0x7fffffu);
}
static void md_snap(struct md_track *s, uint32_t controls)
{
    mdkit_control_clock *c = (mdkit_control_clock *)(void *)&s->state;
    md_clock_word *target = (md_clock_word *)(void *)c->target, *raw = (md_clock_word *)(void *)c->raw,
                  *pending = (md_clock_word *)(void *)c->pending, *current = (md_clock_word *)(void *)c->current;
    const uint32_t floats = (MD_FLOAT_CLOCKS >> (s->loaded - 1u)) & 1u;
    uint32_t i;
    for (i = 0; i < controls && i < MDKIT_MAX_CONTROLS; ++i) {
        const uint32_t k = s->knob[i], t = floats ? md_float_bits(k) : k, r = floats ? t : k * 128u;
        target[i] = t; raw[i] = pending[i] = current[i] = r;
    }
    c->pending_valid = 1; c->publish_block = c->block;
}

/* the voice's next n words at 44.1 kHz, rounded to 16 bits (|error| <= 1/2;
 * 0x7fff80 and up kept at 32767), into x */
static void md_pull(struct md_track *s, short *x, uint32_t n)
{
    uint32_t i, k;
    while (n) {
        if (s->at >= 32u) {
            uint32_t quiet = 1;
            if (s->resting || md_emac_status == 2) { for (i = 0; i < 32; ++i) s->block[i] = 0; }
            else {
                MD_WORDS(s->loaded - 1u)(&s->state, s->block, 32);
                if (MD_REST) {
                    for (i = 0; i < 32 && quiet; ++i)
                        quiet = s->block[i] <= MD_REST_LEVEL && s->block[i] >= -MD_REST_LEVEL;
                    s->quiet = quiet ? s->quiet + 1u : 0;
                    if (s->quiet >= MD_REST_BLOCKS) s->resting = 1;
                }
            }
            s->at = 0;
        }
        k = 32u - s->at;
        if (k > n) k = n;
        for (i = 0; i < k; ++i) {
            int32_t v = (s->block[s->at + i] + 128) >> 8;
            x[i] = (short)(v > 32767 ? 32767 : v);
        }
        s->at += k; x += k; n -= k;
    }
}

void md_update(uint32_t pitch, void *voice, const unsigned short *params)
{
    static const unsigned char slots[8] = {0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x1a, 0x19};
    unsigned short native[71];
    uint32_t *v = voice, i, trigger, type;
    const unsigned short *ext;
    struct md_track *s;
    int t = md_track_of(voice);
    (void)pitch;
    if (t < 0) return;
    md_pick_voices();
    s = md_tracks + t;
    if ((uint32_t)t <= md_last) ++md_round;
    md_last = (uint32_t)t; md_seen[t] = md_round;
    trigger = v[0x38 / 4] != 0;
    ext = MD_EXTENDED(params);
    /* TYPE is a configuration control (main.c); past the last voice (an older Sound's "none
     * chosen yet") it is the first voice, as the page shows it */
    if (ext) { type = ext[0] >> 8; s->type = type < MD_VOICES ? type : 0; }
    else if (!s->loaded) s->type = 0;       /* not arrived yet */
    s->blocked = s->type < MD_VOICES && !md_budget_fits(t, s->type);
    {
#ifdef MD_EMAC
        struct md_emac_saved emac;
        md_emac_begin(&emac);           /* a voice's init, knobs or note may multiply */
#endif
        const mdkit_voice *d;
        const uint32_t none = s->type >= MD_VOICES || s->blocked;
        if (!s->loaded || (trigger && !none && s->loaded != s->type + 1u)) md_load(s, none ? 0u : s->type);
        d = MD_VOICE(s->loaded - 1u);
        for (i = 0; i < d->control_count; ++i) {
            uint32_t k = params[slots[i]] >> 8;
            if (k > 127u) k = 127u;
            if (k != s->knob[i]) { d->set_control(&s->state, i, k); s->knob[i] = k; }
        }
        if (none) s->resting = 1;
        else if (trigger) { md_snap(s, d->control_count); d->trigger(&s->state); s->resting = 0; s->quiet = 0; }
        else if (!MD_REST) s->resting = 0;  /* a chosen voice renders from its load */
#ifdef MD_EMAC
        md_emac_end(&emac);
#endif
    }
    /* The stock TwinShot update sets up the track's gain and AMP as for a
     * sample; its reader stays off (md_render writes the PCM). As the sample
     * machines do (mods/samplers-st/audio/sampler.c). */
    for (i = 0; i < 71; ++i) native[i] = params[i];
    native[0x12] = 0x4000; native[0x13] = 0x4000; native[0x14] = 0x4000;
    native[0x15] = 0; native[0x16] = 0x4000; native[0x17] = 0x4000; native[0x18] = 0;
    native[0x19] = 0x7f00;
    MD_TWINSHOT_UPDATE(60u << 16, voice, native);
    v[2] = 0x3fffffffu; v[3] = 0; v[0x6e8 / 4] = 0;
    if (trigger) { v[6] = 0x3fffffffu; v[7] = 0; }
    v[0x6d4 / 4] = 0; v[0x6d8 / 4] = 0;
    v[0x6c4 / 4] = !s->resting;
}

void md_render(void *out, void *voice)
{
    int32_t *a = MD_SCRATCH_A, *b = MD_SCRATCH_B;
    int locals[2] = {0, 1};
    uint32_t i;
    struct md_track *s;
    int t = md_track_of(voice);
    if (t < 0 || !md_tracks[t].loaded) return;
    s = md_tracks + t;
    if (s->resting && s->at >= 32u && !(s->near[0] | s->near[1] | s->near[2] | s->near[3])) {
        for (i = 0; i < 32; ++i) a[i] = 0;
    } else {
        /* Each output advances 147/160 of a word, so at most one new word, and
         * the block needs (phase + 32 * 147) / 160 of them: 29 or 30. x holds
         * the last four words read and those; output i reads x[j..j+3], j the
         * words read before it. */
        short x[4 + 30];
        uint32_t n = (s->phase + 32u * MD_STEP) / MD_UNIT, p = s->phase;
#ifdef MD_EMAC
        struct md_emac_saved emac;
        md_emac_begin(&emac);           /* the voices' products use the EMAC */
#endif
        x[0] = s->near[0]; x[1] = s->near[1]; x[2] = s->near[2]; x[3] = s->near[3];
        md_pull(s, x + 4, n);
#ifdef MD_EMAC
        md_emac_end(&emac);
#endif
        /* phase p's row, and x + j: stepped, not recomputed. The rows are stored in the
         * order the outputs read them (md-weights.h), so each output takes the next row */
        const short *w = MD_WEIGHT(p), *v = x;
        for (i = 0; i < 32; ++i) {
            /* 16 x 16 products (muls.w); the sum fits: sum |w| <= 1.25 * 2^14.
             * The words' rounding costs half a 16-bit step each (a whole one at
             * full scale), the level of the Syntakt's own 16-bit samples
             * (tests/md_st_glue_check.c). */
            int32_t y = ((int32_t)v[0] * w[0] + (int32_t)v[1] * w[1] +
                         (int32_t)v[2] * w[2] + (int32_t)v[3] * w[3]) >> 6;
            if (y > 0x7fffff) y = 0x7fffff;
            if (y < -0x800000) y = -0x800000;
            a[i] = y << 8;                                     /* Q31 */
            /* p + 147 < 160: phase p + 147; else p - 13 and the next word. Either is
             * the next row */
            w += 4;
            if (p < MD_UNIT - MD_STEP) p += MD_STEP;
            else { p -= MD_UNIT - MD_STEP; ++v; }
        }
        s->phase = p;
        s->near[0] = x[n]; s->near[1] = x[n + 1]; s->near[2] = x[n + 2]; s->near[3] = x[n + 3];
    }
    for (i = 0; i < 32; i += 4) { b[i] = 0; b[i + 1] = 0; b[i + 2] = 0; b[i + 3] = 0; }
    /* the four stock calls of the TwinShot render, on our PCM */
    MD_TWINSHOT_CHAIN(a, b, out, locals, voice);
    ((uint32_t *)voice)[0x6c4 / 4] = !s->resting;
}
