/* SPDX-License-Identifier: GPL-2.0-or-later
 * md_st_glue_check.c - the MD machine's AUDIO callbacks (mods/md-st/audio/md.c)
 * on the host, with the stock routines replaced: the TwinShot chain passes
 * the PCM through, so the track's output is md.c's own.
 *
 * Checks: the 48 kHz output is the voice's 44.1 kHz words through md.c's
 * Hermite weights (to 320 LSB of 24 bits: md.c interpolates the words
 * rounded to 16 bits, half a 16-bit step each and a whole one at full scale,
 * times the weights' 1.25); TYPE changes the voice only at a note, and TYPE past
 * the last voice (none chosen: the picker is up) is silent at every note; a
 * knob reaches the voice; a note plays its knobs (a p-lock, a Sound lock's voice)
 * whole from its first block, a turned knob glides; a decayed voice rests (silence, inactive)
 * and sounds again at the next note; a pointer that is no track is ignored.
 * Built and run by tests/test_md_st.py.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MD_STOCK 1
static uint32_t blocks[8][0x708 / 4];
static int32_t scratch_a[32], scratch_b[32];
static unsigned short ext_words[20];
static int ext_ready;
static unsigned updates;
static const unsigned short *ext_of(const void *p) { (void)p; return ext_ready ? ext_words : 0; }
#define MD_EXTENDED ext_of
#define MD_TRACK_BASE ((uint32_t)(uintptr_t)blocks)
#define MD_SCRATCH_A scratch_a
#define MD_SCRATCH_B scratch_b
#define MD_TWINSHOT_UPDATE(pitch, voice, native) (++updates, (void)(pitch), (void)(voice), (void)(native))
#define MD_TWINSHOT_CHAIN(a, b, out, locals, voice) do { \
    (void)(b); (void)(locals); (void)(voice); memcpy((out), (a), 128); } while (0)
#include "../mods/md-st/audio/md.c"

static unsigned short params[71];
static const unsigned char slot_of[8] = {0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x1a, 0x19};
static int bad;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL " __VA_ARGS__); printf("\n"); bad = 1; } } while (0)

/* one Syntakt block on track t; trigger sets the voice's +0x38 as the scheduler does */
static void block(unsigned t, int trigger, int32_t out[32])
{
    blocks[t][0x38 / 4] = trigger ? 1 : 0;
    md_update(60u << 16, blocks[t], params);
    md_render(out, blocks[t]);
}

/* a voice's clock as mdkit's init seeds it, from these knobs in place of its defaults:
 * floats, or integers (the knob; raw, pending and current 128 times it) */
static void seed(unsigned type, union md_voice_state *st, const unsigned char *knob)
{
    mdkit_control_clock *c = (mdkit_control_clock *)(void *)st;
    for (unsigned i = 0; i < md_voice[type]->control_count; ++i) {
        if ((MD_FLOAT_CLOCKS >> type) & 1u) c->target[i] = c->raw[i] = c->pending[i] = c->current[i] = (float)knob[i];
        else {
            md_clock_word *w = (md_clock_word *)(void *)c;
            w[1 + i] = knob[i];
            w[1 + 8 + i] = w[1 + 16 + i] = w[1 + 24 + i] = knob[i] * 128u;
        }
    }
}
/* the clock's value i for a knob k as it is kept: 128 k, or k as a float */
static int clock_is(unsigned type, const float *field, unsigned i, unsigned k, int is_target)
{
    if ((MD_FLOAT_CLOCKS >> type) & 1u) return field[i] == (float)k;
    return ((const md_clock_word *)(const void *)field)[i] == (is_target ? k : k * 128u);
}
static int clock_at(unsigned t, unsigned type, const unsigned char *knob)
{
    const mdkit_control_clock *c = (const mdkit_control_clock *)(const void *)&md_tracks[t].state;
    int ok = md_tracks[t].loaded == type + 1u;
    for (unsigned i = 0; i < md_voice[type]->control_count; ++i)
        ok &= clock_is(type, c->target, i, knob[i], 1) && clock_is(type, c->raw, i, knob[i], 0) &&
              clock_is(type, c->current, i, knob[i], 0);
    return ok;
}

/* the voice's own words: a fresh state, its clock at the knobs (a note plays them as
 * they are, md.c's md_snap), a note at sample 0 */
static int32_t *stream(unsigned type, const unsigned char *knob, unsigned n)
{
    static union md_voice_state st;
    int32_t *s = calloc(n, sizeof *s);
    const mdkit_voice *v = md_voice[type];
    v->init(&st);
    seed(type, &st, knob);
    for (unsigned i = 0; i < v->control_count; ++i) v->set_control(&st, i, knob[i]);
    v->trigger(&st);
    md_voice_words[type](&st, s, n);
    return s;
}

/* output k as md.c computes it, from the stream: at k * 147/160, the four
 * words before the m = floor(k * 147 / 160) pulled so far, md_weights' row */
static double expected(const int32_t *s, unsigned k)
{
    unsigned p = k * 147u, m = p / 160u, f = p % 160u;
    double y = 0;
    for (int j = 0; j < 4; ++j) {
        int at = (int)m - 4 + j;
        y += (at >= 0 ? s[at] : 0) * (double)MD_WEIGHT(f)[j];
    }
    y /= 16384.0;
    return y > 8388607.0 ? 8388607.0 : y < -8388608.0 ? -8388608.0 : y;   /* md.c clips the overshoot */
}

static void compare(const char *what, const int32_t *out, const int32_t *s, unsigned k0, unsigned blocks_, unsigned base)
{
    for (unsigned b = 0; b < blocks_ && !bad; ++b)
        for (unsigned i = 0; i < 32 && !bad; ++i) {
            double want = expected(s, base + 32 * b + i);
            double got = out[32 * (k0 + b) + i] / 256.0;
            CHECK(fabs(got - want) <= 320.0 + (fabs(want) < 8 ? 6 : 0), "%s: block %u frame %u: %.1f, want %.1f",
                  what, k0 + b, i, got, want);
        }
}

int main(void)
{
    enum { N = 400 };
    static int32_t out[32 * N];
    unsigned char knob[8];
    unsigned b, i;
    int32_t *s;
    /* the weights: each row sums to 16384 and is Catmull-Rom within 1/16384 */
    for (unsigned p = 0; p < 160; ++p) {
        double f = p / 160.0, w[4] = {(-f * f * f + 2 * f * f - f) / 2, (3 * f * f * f - 5 * f * f + 2) / 2,
                                       (-3 * f * f * f + 4 * f * f + f) / 2, (f * f * f - f * f) / 2};
        int sum = 0;
        for (i = 0; i < 4; ++i) { sum += MD_WEIGHT(p)[i]; CHECK(fabs(MD_WEIGHT(p)[i] - w[i] * 16384) <= 1.5, "weight %u.%u", p, i); }
        CHECK(sum == 16384, "weights %u sum %d", p, sum);
    }
    /* 1. TYPE not yet arrived: the first voice, its knobs, a note */
    for (i = 0; i < 8; ++i) { knob[i] = (unsigned char)(i * 17 + 5); params[slot_of[i]] = (unsigned short)(knob[i] << 8); }
    for (b = 0; b < N; ++b) block(2, b == 0, out + 32 * b);
    s = stream(0, knob, 32 * N);
    compare("TRX-BD", out, s, 0, 60, 0);
    CHECK(updates == N, "TwinShot update ran %u times", updates);
#if MD_REST
    /* 2. it rests once decayed: silent, inactive */
    CHECK(md_tracks[2].resting && blocks[2][0x6c4 / 4] == 0, "TRX-BD does not rest after %u blocks", N);
    for (i = 0; i < 32; ++i) CHECK(out[32 * (N - 1) + i] == 0, "a resting track is not silent");
#else
    /* 2. MD_REST 0: it keeps rendering once decayed, and a voice renders from its load,
     * before any note */
    CHECK(!md_tracks[2].resting && blocks[2][0x6c4 / 4] == 1, "TRX-BD rested with MD_REST 0");
    for (b = 0; b < 4; ++b) block(7, 0, out + 32 * b);
    CHECK(md_tracks[7].loaded && !md_tracks[7].resting && blocks[7][0x6c4 / 4] == 1, "a loaded voice does not render before its note");
#endif
    /* 3. TYPE 1 arrives: nothing changes until the next note, which plays TRX-SD */
    ext_ready = 1; ext_words[0] = 1 << 8;
    for (b = 0; b < 4; ++b) block(2, 0, out + 32 * b);
    CHECK(md_tracks[2].loaded == 1, "TYPE changed the voice without a note");
    for (b = 0; b < N; ++b) block(2, b == 0, out + 32 * b);
    CHECK(md_tracks[2].loaded == 2, "a note did not load TYPE 1");
    free(s); s = stream(1, knob, 32 * N);
    compare("TRX-SD", out, s, 0, 60, 0);
    /* 4. a knob turned before a note reaches the voice */
    params[slot_of[0]] = 100 << 8; knob[0] = 100;
    ext_words[0] = 2 << 8;
    for (b = 0; b < 80; ++b) block(5, b == 0, out + 32 * b);
    free(s); s = stream(2, knob, 32 * 80);
    compare("PI-SD", out, s, 0, 40, 0);
    /* 5. TYPE past the last voice (an older Sound's "none chosen yet") is the first voice,
     * as the page shows it */
    ext_words[0] = MD_VOICES << 8;
    for (b = 0; b < N; ++b) block(6, b == 0, out + 32 * b);
    CHECK(md_tracks[6].loaded == 1 && !md_tracks[6].blocked, "an old none did not play the first voice");
    free(s); s = stream(0, knob, 32 * N);
    compare("TRX-BD from an old none", out, s, 0, 60, 0);
    /* 6. the budget: PI-RC sounds on track 6; PI-SD brought to track 5 beside it stays
     * silent and inactive; once track 6 is no MD track (no longer updated), it plays */
    {
        unsigned rc = MD_VOICES, sd = MD_VOICES, v, loud = 0;
        static int32_t other[32];
        for (v = 0; v < MD_VOICES; ++v) {
            if (!strcmp(md_voice[v]->name, "PI-RC")) rc = v;
            if (!strcmp(md_voice[v]->name, "PI-SD")) sd = v;
        }
        CHECK(rc < MD_VOICES && sd < MD_VOICES && md_voice_weight[rc] + md_voice_weight[sd] > MD_BUDGET, "PI-RC and PI-SD should not fit together");
        for (b = 0; b < 16; ++b) {
            ext_words[0] = (unsigned short)(rc << 8); block(6, b == 0, other);
            ext_words[0] = (unsigned short)(sd << 8); block(5, b == 0, out + 32 * b);
        }
        CHECK(md_tracks[6].loaded == rc + 1u && !md_tracks[6].blocked, "PI-RC did not keep its place");
        CHECK(md_tracks[5].blocked && md_tracks[5].resting && blocks[5][0x6c4 / 4] == 0, "PI-SD past the budget is not silent");
        /* from the second block: the first ends the resampler's words of what track 5 played before */
        for (i = 32 * 2; i < 32 * 16; ++i) CHECK(out[i] == 0, "PI-SD past the budget: sample %u is %d", i, out[i]);
        for (b = 0; b < 40; ++b) { ext_words[0] = (unsigned short)(sd << 8); block(5, b == 4, out + 32 * b); }
        for (i = 32 * 5; i < 32 * 40; ++i) loud |= out[i] != 0;
        CHECK(!md_tracks[5].blocked && md_tracks[5].loaded == sd + 1u && loud, "PI-SD did not play once the budget was free");
    }
    /* 7. a note plays its knobs as they are, from its first block: a p-lock on the same
     * voice (TRX-BD's pitch, after a note at other values), a Sound lock that brings
     * another voice (TRX-CP) and an EFM voice (floats); a knob turned between notes
     * glides there (mdkit's clock), the next note takes it whole */
    {
        unsigned bd = MD_VOICES, cp = MD_VOICES, efm = MD_VOICES, v;
        const mdkit_control_clock *c = (const mdkit_control_clock *)(const void *)&md_tracks[4].state;
        for (v = 0; v < MD_VOICES; ++v) {
            if (!strcmp(md_voice[v]->name, "TRX-BD")) bd = v;
            if (!strcmp(md_voice[v]->name, "TRX-CP")) cp = v;
            if (!strcmp(md_voice[v]->name, "EFM-SD")) efm = v;
        }
        CHECK(bd < MD_VOICES && cp < MD_VOICES && efm < MD_VOICES && ((MD_FLOAT_CLOCKS >> efm) & 1u) && !((MD_FLOAT_CLOCKS >> bd) & 1u),
              "TRX-BD, TRX-CP or EFM-SD not found, or their clocks not as md-voices.h says");
        for (i = 0; i < 8; ++i) { knob[i] = (unsigned char)(i * 9 + 30); params[slot_of[i]] = (unsigned short)(knob[i] << 8); }
        ext_words[0] = (unsigned short)(bd << 8);
        for (b = 0; b < 32; ++b) block(4, b == 2, out);       /* two blocks first: track 5 (6.) leaves the budget */
        CHECK(clock_at(4, bd, knob), "TRX-BD: a note did not take its knobs");
        knob[0] = 120; params[slot_of[0]] = 120 << 8;          /* PTCH p-locked on the next note */
        block(4, 1, out);
        CHECK(clock_at(4, bd, knob), "TRX-BD: a p-locked note did not take PTCH whole");
        knob[0] = 10; params[slot_of[0]] = 10 << 8;             /* turned, no note: it glides */
        for (b = 0; b < 13; ++b) block(4, 0, out);
        CHECK(clock_is(bd, c->target, 0, 10, 1) && !clock_is(bd, c->current, 0, 10, 0), "TRX-BD: a turned knob did not glide");
        block(4, 1, out);
        CHECK(clock_at(4, bd, knob), "TRX-BD: the note after a turn did not take it whole");
        for (i = 0; i < 8; ++i) { knob[i] = (unsigned char)(127 - i * 11); params[slot_of[i]] = (unsigned short)(knob[i] << 8); }
        ext_words[0] = (unsigned short)(cp << 8);               /* a Sound lock: TRX-CP and its knobs */
        block(4, 1, out);
        CHECK(clock_at(4, cp, knob), "TRX-CP: the note that brought it did not take its knobs");
        ext_words[0] = (unsigned short)(efm << 8);
        block(4, 1, out);
        CHECK(clock_at(4, efm, knob), "EFM-SD: the note that brought it did not take its knobs");
        block(4, 0, out);
        CHECK(clock_at(4, efm, knob), "EFM-SD: its knobs moved without a turn");
    }
    /* a pointer that is no track is ignored */
    md_update(0, (unsigned char *)blocks[1] + 4, params);
    md_render(out, (unsigned char *)blocks[1] + 4);
    CHECK(!md_tracks[1].loaded, "a pointer inside a block was taken for a track");
    free(s);
    if (!bad) printf("PASS md.c (MD_REST %d): resampling, TYPE at the note, an old none, the budget, knobs, a note's knobs whole, rest, track bounds\n", MD_REST);
    return bad;
}
