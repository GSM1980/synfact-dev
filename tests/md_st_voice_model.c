/* SPDX-License-Identifier: GPL-2.0-or-later
 * md_st_voice_model.c - EFM-BD, written by hand from mdkit's model (tools/md_port.py,
 * HAND; mods/md-st/audio/native/efm_bd.c), against that model.
 *
 * mdkit's EFM-BD feeds its first operator back into its own phase (mod = 32768 MFB s1)
 * with a loop gain of 2 pi MFB cos, past 1 for MFB from about 21: any difference, even
 * double rounding, grows there sample by sample, so with feedback no other arithmetic
 * follows its phases far. What is checked:
 *   1. the envelopes, which do not depend on the phases: op1, op2 and fb, read from both
 *      states after every render, over the random scenarios (knobs, notes and renders as
 *      md_st_voice_ser.c's), within 1e-5 of mdkit's: relative to the value, or to 0.1
 *      below it (1e-6 absolute, -120 dB: the tail of a decay, where the port's 31 bits
 *      round; fb past 15.9, mdkit's unbounded, not compared);
 *   2. without feedback (MFB 0, the other knobs random, the control clock settled before
 *      the note), the voice is deterministic: its first 2048 samples (46 ms) must have a
 *      signal to error ratio of at least <floor> dB, and over a one-second note each
 *      operator's phase error, summed every 32 samples (where it is far below a cycle,
 *      so unwrapped), must stay within 1e-5 of the phase mdkit's operator has advanced
 *      since the note, once that is past a cycle (0.02 cent of frequency; op2's phase
 *      moves back and forth with op1, so one block's advance alone would not measure it).
 * Built by tests/test_md_st.py with -DVOICE=efm_bd.
 * Usage: md_st_voice_model <scenarios> <floor dB>
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../mods/md-st/mdkit/src/efm/bd/mdkit_efm_bd.h"
#include "../mods/md-st/audio/native/efm_bd.h"

extern const mdkit_voice ref_voice;

static uint64_t seed = 0x2545f4914f6cdd1dull;
static unsigned rnd(unsigned n) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return (unsigned)(seed >> 33) % n; }

static double env(int32_t base, const int32_t *p, unsigned k) { return (k ? (double)base * p[k] / 2147483648.0 : base); }
static double relative(double r, double p) { return fabs(r - p) / fmax(fabs(r), 0.1); }
static double envelope_error(const mdkit_efm_bd *a, const md_efm_bd *b)     /* relative to the value (or 0.1) */
{
    double e = relative(a->op1, env(b->base1, b->p1, b->k) / 2147483648.0);
    e = fmax(e, relative(a->op2, env(b->base2, b->p2, b->k) / 2147483648.0));
    return fmax(e, a->fb > 15.9 ? 0 : relative(a->fb, env(b->basef, b->pf, b->k) / 134217728.0));
}

int main(int argc, char **argv)
{
    const mdkit_voice *ref = &ref_voice, *port = &mdkit_voice_efm_bd;
    unsigned scenarios = argc > 1 ? (unsigned)atoi(argv[1]) : 40, sc;
    double floor_db = argc > 2 ? atof(argv[2]) : 60.0, worst_env = 0, worst_db = 1e9;
    mdkit_efm_bd *a = calloc(1, sizeof *a);
    md_efm_bd *b = calloc(1, sizeof *b);
    static float fa[44100];
    static int32_t wb[44100];
    for (sc = 0; sc < scenarios; ++sc) {                       /* 1. the envelopes */
        unsigned pos = 0, length = 44100u * (sc % 4 + 1) / 2;
        ref->init(a); port->init(b);
        if (sc) for (unsigned i = 0; i < ref->control_count; ++i) {
            unsigned v = sc < 9 ? (sc & 1 ? 0 : 127) : rnd(128);
            ref->set_control(a, i, v); port->set_control(b, i, v);
        }
        while (pos < length) {
            unsigned n = 1 + rnd(sc % 3 ? 700 : 64), event = rnd(100);
            if (n > length - pos) n = length - pos;
            if (event < 4 || pos == 0) { ref->trigger(a); port->trigger(b); }
            else if (event < 30) { unsigned i = rnd(ref->control_count), v = rnd(128); ref->set_control(a, i, v); port->set_control(b, i, v); }
            ref->render(a, fa, n);
            md_st_efm_bd_render_words(b, wb, n);
            const double e = envelope_error(a, b);
            if (e > worst_env) worst_env = e;
            pos += n;
        }
    }
    double worst_freq = 0;
    for (sc = 0; sc < scenarios; ++sc) {                       /* 2. no feedback */
        double sig = 0, err = 0, db;
        ref->init(a); port->init(b);
        for (unsigned i = 0; i < ref->control_count; ++i) {
            const unsigned v = i == MDKIT_EFM_BD_MFB ? 0 : (sc < 2 ? 127 * sc : rnd(128));
            ref->set_control(a, i, v); port->set_control(b, i, v);
        }
        for (unsigned k = 0; k < 2; ++k) { ref->render(a, fa, 44100); md_st_efm_bd_render_words(b, wb, 44100); }   /* settle */
        ref->trigger(a); port->trigger(b);
        const double start1 = a->ph1, start2 = a->ph2;
        double sum1 = 0, sum2 = 0;                             /* the phase errors, unwrapped */
        for (unsigned at = 0; at < 44096; at += 32) {
            const double r1 = a->ph1, r2 = a->ph2;
            const uint32_t p1 = b->ph1, p2 = b->ph2;
            ref->render(a, fa + at, 32);
            md_st_efm_bd_render_words(b, wb + at, 32);
            const double d1 = a->ph1 - r1, d2 = a->ph2 - r2, t1 = fabs(a->ph1 - start1), t2 = fabs(a->ph2 - start2);
            sum1 += (double)(int32_t)(b->ph1 - p1 - (uint32_t)(int64_t)(d1 * 131072.0)) / 131072.0;
            sum2 += (double)(int32_t)(b->ph2 - p2 - (uint32_t)(int64_t)(d2 * 131072.0)) / 131072.0;
            if (t1 > 32768.0 && fabs(sum1) / t1 > worst_freq) worst_freq = fabs(sum1) / t1;
            if (t2 > 32768.0 && fabs(sum2) / t2 > worst_freq) worst_freq = fabs(sum2) / t2;
        }
        for (unsigned i = 0; i < 2048; ++i) { const double e = wb[i] / 8388608.0 - fa[i]; sig += (double)fa[i] * fa[i]; err += e * e; }
        db = err > 0 ? 10.0 * log10((sig + 1e-30) / err) : 200.0;
        if (sig < 1e-12) db = err < 1e-18 ? 200.0 : 0.0;
        if (db < worst_db) worst_db = db;
    }
    const int bad = worst_env > 1e-5 || worst_db < floor_db || worst_freq > 1e-5;
    printf("%s %s: envelopes worst %.2g (relative, limit 1e-5); without feedback: attacks worst %.1f dB (floor %.0f dB), "
           "frequencies worst %.2g (limit 1e-5); %u scenarios each\n", ref->name, bad ? "FAIL" : "PASS", worst_env, worst_db, floor_db, worst_freq, scenarios);
    return bad;
}
