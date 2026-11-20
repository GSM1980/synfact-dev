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
 * knob reaches the voice; a decayed voice rests (silence, inactive)
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

/* the voice's own words: a fresh state, knobs, a note at sample 0 */
static int32_t *stream(unsigned type, const unsigned char *knob, unsigned n)
{
    static union md_voice_state st;
    int32_t *s = calloc(n, sizeof *s);
    const mdkit_voice *v = md_voice[type];
    v->init(&st);
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
    /* 3. TYPE 1 arrives: nothing changes until the next note, which plays EFM-SD */
    ext_ready = 1; ext_words[0] = 1 << 8;
    for (b = 0; b < 4; ++b) block(2, 0, out + 32 * b);
    CHECK(md_tracks[2].loaded == 1, "TYPE changed the voice without a note");
    for (b = 0; b < N; ++b) block(2, b == 0, out + 32 * b);
    CHECK(md_tracks[2].loaded == 2, "a note did not load TYPE 1");
    free(s); s = stream(1, knob, 32 * N);
    compare("EFM-SD", out, s, 0, 60, 0);
    /* 4. a knob turned before a note reaches the voice */
    params[slot_of[0]] = 100 << 8; knob[0] = 100;
    ext_words[0] = 2 << 8;
    for (b = 0; b < 80; ++b) block(5, b == 0, out + 32 * b);
    free(s); s = stream(2, knob, 32 * 80);
    compare("PI-SD", out, s, 0, 40, 0);
    /* 5. TYPE past the last voice is no voice (the machine was just selected and its
     * picker is up): notes play nothing and the track rests, also after a voice sounded;
     * the voice chosen then plays at the next note */
    ext_words[0] = MD_VOICES << 8;
    for (b = 0; b < 8; ++b) block(6, b % 2 == 0, out + 32 * b);
    for (i = 0; i < 32 * 8; ++i) CHECK(out[i] == 0, "no voice chosen, yet sample %u is %d", i, out[i]);
    CHECK(md_tracks[6].resting && blocks[6][0x6c4 / 4] == 0, "no voice chosen, yet the track is active");
    ext_words[0] = 99 << 8;
    block(5, 1, out);
    for (b = 0; b < 8; ++b) block(5, 0, out + 32 * b);
    CHECK(md_tracks[5].resting && out[32 * 7 + 31] == 0, "TYPE 99 did not silence a sounding track");
    ext_words[0] = 0;
    for (b = 0; b < N; ++b) block(6, b == 0, out + 32 * b);
    CHECK(md_tracks[6].loaded == 1, "the chosen voice did not load at its note");
    free(s); s = stream(0, knob, 32 * N);
    compare("TRX-BD after none", out, s, 0, 60, 0);
    /* a pointer that is no track is ignored */
    md_update(0, (unsigned char *)blocks[1] + 4, params);
    md_render(out, (unsigned char *)blocks[1] + 4);
    CHECK(!md_tracks[1].loaded, "a pointer inside a block was taken for a track");
    free(s);
    if (!bad) printf("PASS md.c (MD_REST %d): resampling, TYPE at the note, no voice chosen, knobs, rest, track bounds\n", MD_REST);
    return bad;
}
