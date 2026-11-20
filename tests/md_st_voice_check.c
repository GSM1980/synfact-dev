/* SPDX-License-Identifier: GPL-2.0-or-later
 * md_st_voice_check.c - one MD voice as the Syntakt builds it (the port in
 * mods/md-st/audio/voices, with MD_ST_TABLES) against the same voice as
 * mdkit wrote it (mods/md-st/mdkit), word for word.
 *
 * Built by tests/test_md_st.py with -DVOICE=<family>_<voice>; the mdkit copy
 * is compiled with its descriptor renamed to ref_voice. Every scenario plays
 * notes, turns knobs between and during notes, and renders in uneven chunks;
 * then (with a second argument, 1) each knob is swept 0..127 with the others at
 * 0, 64 and 127. The port is built with MD_ST_DOMAIN_CHECK: a table read outside
 * its table fails the check.
 * Usage: md_st_voice_check [scenarios] [sweeps]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../mods/md-st/audio/voices/mdkit.h"

#define CAT(a, b) a##b
#define XCAT(a, b) CAT(a, b)
extern const mdkit_voice ref_voice;
extern const mdkit_voice XCAT(mdkit_voice_, VOICE);
void XCAT(XCAT(md_st_, VOICE), _render_words)(void *state, int32_t *out, unsigned frames);

void md_st_domain_fail(const char *table, unsigned argument)
{
    printf("FAIL table md_%s read at %u, outside it\n", table, argument);
    exit(1);
}

static int same(const char *name, unsigned sc, unsigned long pos, const float *fa, const int32_t *wb, unsigned n)
{
    for (unsigned i = 0; i < n; ++i)
        if ((float)wb[i] / 8388608.0f != fa[i]) {
            printf("FAIL %s scenario %u sample %lu: %d, mdkit %.9g\n", name, sc, pos + i, (int)wb[i], (double)fa[i] * 8388608.0);
            return 0;
        }
    return 1;
}

static uint64_t seed = 0x2545f4914f6cdd1dull;
static unsigned rnd(unsigned n) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return (unsigned)(seed >> 33) % n; }

int main(int argc, char **argv)
{
    const mdkit_voice *ref = &ref_voice, *port = &XCAT(mdkit_voice_, VOICE);
    unsigned scenarios = argc > 1 ? (unsigned)atoi(argv[1]) : 40, sc, bad = 0;
    unsigned long samples = 0;
    void *a = calloc(1, ref->state_bytes), *b = calloc(1, port->state_bytes);
    static float fa[4096];
    static int32_t wb[4096];
    if (ref->control_count != port->control_count) {      /* a port may keep less state (md_port.py, EDITS) */
        printf("FAIL controls differ\n"); return 1; }
    for (sc = 0; sc < scenarios && !bad; ++sc) {
        unsigned pos = 0, length = 44100u * (sc % 4 + 1) / 2;
        ref->init(a); port->init(b);
        if (sc) for (unsigned i = 0; i < ref->control_count; ++i) {   /* scenario 0: defaults */
            unsigned v = sc < 9 ? (sc & 1 ? 0 : 127) : rnd(128);
            ref->set_control(a, i, v); port->set_control(b, i, v);
        }
        while (pos < length && !bad) {
            unsigned n = 1 + rnd(sc % 3 ? 700 : 64), event = rnd(100);
            if (n > length - pos) n = length - pos;
            if (event < 4 || pos == 0) { ref->trigger(a); port->trigger(b); }
            else if (event < 30) {
                unsigned i = rnd(ref->control_count), v = rnd(128);
                ref->set_control(a, i, v); port->set_control(b, i, v);
            }
            ref->render(a, fa, n);
            XCAT(XCAT(md_st_, VOICE), _render_words)(b, wb, n);
            bad = !same(ref->name, sc, pos, fa, wb, n);
            pos += n; samples += n;
        }
    }
    if (!bad && argc > 2 && atoi(argv[2])) {        /* the sweeps */
        static const unsigned base[3] = {0, 64, 127};
        unsigned sweeps = 0;
        for (unsigned bs = 0; bs < 3 && !bad; ++bs)
            for (unsigned c = 0; c < ref->control_count && !bad; ++c)
                for (unsigned k = 0; k < 128 && !bad; ++k) {
                    ref->init(a); port->init(b);
                    for (unsigned j = 0; j < ref->control_count; ++j) {
                        ref->set_control(a, j, j == c ? k : base[bs]); port->set_control(b, j, j == c ? k : base[bs]); }
                    ref->trigger(a); port->trigger(b);
                    ref->render(a, fa, 2048); XCAT(XCAT(md_st_, VOICE), _render_words)(b, wb, 2048);
                    bad = !same(ref->name, 1000 + sweeps, 0, fa, wb, 2048);
                    ref->set_control(a, c, 127 - k); port->set_control(b, c, 127 - k);
                    ref->render(a, fa, 2048); XCAT(XCAT(md_st_, VOICE), _render_words)(b, wb, 2048);
                    bad = bad || !same(ref->name, 1000 + sweeps, 2048, fa, wb, 2048);
                    samples += 4096; ++sweeps;
                }
        if (!bad) printf("PASS %s: %u scenarios and %u sweeps, %lu samples word for word\n", ref->name, scenarios, sweeps, samples);
    } else if (!bad) printf("PASS %s: %u scenarios, %lu samples word for word\n", ref->name, scenarios, samples);
    return bad;
}
