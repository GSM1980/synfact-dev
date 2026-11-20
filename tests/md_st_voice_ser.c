/* SPDX-License-Identifier: GPL-2.0-or-later
 * md_st_voice_ser.c - one MD voice as the Syntakt builds it against mdkit's,
 * as a signal-to-error ratio per scenario, for builds that are not mdkit's
 * word for word (MD_ST_FAST, and the approximate forms of tools/md_port.py's
 * APPROX). The scenarios are md_st_voice_check.c's. Built by tests/test_md_st.py
 * with -DVOICE=<family>_<voice>; prints the worst ratio and exits 1 below the
 * floor given (dB). With a third argument it also prints the worst level of the
 * error itself (RMS over a scenario, dB of full scale) and exits 1 above it: a
 * ratio says little where the voice is quiet, the error's level is what is heard.
 * Usage: md_st_voice_ser <scenarios> <floor dB> [<error ceiling dBFS>]
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "../mods/md-st/audio/voices/mdkit.h"

#define CAT(a, b) a##b
#define XCAT(a, b) CAT(a, b)
extern const mdkit_voice ref_voice;
extern const mdkit_voice XCAT(mdkit_voice_, VOICE);
void XCAT(XCAT(md_st_, VOICE), _render_words)(void *state, int32_t *out, unsigned frames);

static uint64_t seed = 0x2545f4914f6cdd1dull;
static unsigned rnd(unsigned n) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return (unsigned)(seed >> 33) % n; }

int main(int argc, char **argv)
{
    const mdkit_voice *ref = &ref_voice, *port = &XCAT(mdkit_voice_, VOICE);
    unsigned scenarios = argc > 1 ? (unsigned)atoi(argv[1]) : 40, sc, at = 0;
    double floor_db = argc > 2 ? atof(argv[2]) : 60.0, worst = 1e9, loudest = -400.0;
    const int levels = argc > 3;
    const double ceiling = levels ? atof(argv[3]) : 0.0;
    unsigned loud_at = 0;
    void *a = calloc(1, ref->state_bytes), *b = calloc(1, port->state_bytes);
    static float fa[4096];
    static int32_t wb[4096];
    for (sc = 0; sc < scenarios; ++sc) {
        unsigned pos = 0, length = 44100u * (sc % 4 + 1) / 2;
        double sig = 0, err = 0, db;
        ref->init(a); port->init(b);
        if (sc) for (unsigned i = 0; i < ref->control_count; ++i) {
            unsigned v = sc < 9 ? (sc & 1 ? 0 : 127) : rnd(128);
            ref->set_control(a, i, v); port->set_control(b, i, v);
        }
        while (pos < length) {
            unsigned n = 1 + rnd(sc % 3 ? 700 : 64), event = rnd(100);
            if (n > length - pos) n = length - pos;
            if (event < 4 || pos == 0) { ref->trigger(a); port->trigger(b); }
            else if (event < 30) {
                unsigned i = rnd(ref->control_count), v = rnd(128);
                ref->set_control(a, i, v); port->set_control(b, i, v);
            }
            ref->render(a, fa, n);
            XCAT(XCAT(md_st_, VOICE), _render_words)(b, wb, n);
            for (unsigned i = 0; i < n; ++i) {
                double e = wb[i] / 8388608.0 - fa[i];
                sig += (double)fa[i] * fa[i]; err += e * e;
            }
            pos += n;
        }
        db = err > 0 ? 10.0 * log10((sig + 1e-30) / err) : 200.0;
        if (sig < 1e-12) db = err < 1e-18 ? 200.0 : 0.0;          /* silence must stay silent */
        if (db < worst) { worst = db; at = sc; }
        if (err > 0 && 10.0 * log10(err / length) > loudest) { loudest = 10.0 * log10(err / length); loud_at = sc; }
    }
    if (levels) {
        printf("%s %s: error at most %.1f dBFS (scenario %u of %u), ceiling %.0f dBFS; worst ratio %.1f dB\n", ref->name,
               loudest <= ceiling ? "PASS" : "FAIL", loudest, loud_at, scenarios, ceiling, worst);
        return loudest > ceiling;
    }
    printf("%s %s: worst %.1f dB (scenario %u of %u), floor %.0f dB\n", ref->name,
           worst >= floor_db ? "PASS" : "FAIL", worst, at, scenarios, floor_db);
    return worst < floor_db;
}
