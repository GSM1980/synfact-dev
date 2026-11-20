/* SPDX-License-Identifier: GPL-2.0-or-later
 * md_st_voice_spectrum.c - one MD voice as the Syntakt builds it against mdkit's, by
 * what is heard rather than word for word: for voices whose approximate forms change
 * the waveform itself (MD_AP_FM: an FM operator with feedback is chaotic, so a phase
 * off by 2^-31 of a turn ends up anywhere; MD_AP_STRUCT: a PI voice with modes left out),
 * compare each scenario's level and its
 * spectrum in third-octave bands. The scenarios are md_st_voice_check.c's.
 * Built by tests/test_md_st.py with -DVOICE=<family>_<voice>. Prints, for the worst
 * scenario, the level difference (dB) and the largest band difference among the bands
 * within 30 dB of the loudest; exits 1 above the limits given.
 * Usage: md_st_voice_spectrum <scenarios> <level limit dB> <band limit dB> [-v]
 */
#include <math.h>
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

static uint64_t seed = 0x2545f4914f6cdd1dull;
static unsigned rnd(unsigned n) { seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return (unsigned)(seed >> 33) % n; }

#define N 2048
#define BANDS 28                       /* third octaves from 25 Hz */
static void fft(double *re, double *im)
{
    for (unsigned i = 1, j = 0; i < N; ++i) {
        unsigned bit = N >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (unsigned len = 2; len <= N; len <<= 1) {
        const double a = -2.0 * 3.14159265358979323846 / len;
        for (unsigned i = 0; i < N; i += len)
            for (unsigned k = 0; k < len / 2; ++k) {
                const double c = cos(a * k), s = sin(a * k);
                const double xr = re[i + k + len / 2] * c - im[i + k + len / 2] * s, xi = re[i + k + len / 2] * s + im[i + k + len / 2] * c;
                re[i + k + len / 2] = re[i + k] - xr; im[i + k + len / 2] = im[i + k] - xi;
                re[i + k] += xr; im[i + k] += xi;
            }
    }
}
/* the signal's power in third-octave bands, Hann windows hopping by N / 2 */
static void bands(const float *x, unsigned length, double out[BANDS])
{
    static double re[N], im[N];
    memset(out, 0, sizeof(double) * BANDS);
    for (unsigned at = 0; at + N <= length; at += N / 2) {
        for (unsigned i = 0; i < N; ++i) {
            re[i] = x[at + i] * (0.5 - 0.5 * cos(2.0 * 3.14159265358979323846 * i / N)); im[i] = 0;
        }
        fft(re, im);
        for (unsigned k = 1; k < N / 2; ++k) {
            const double f = k * 44100.0 / N;
            const int b = (int)floor(3.0 * log2(f / 25.0));
            if (b >= 0 && b < BANDS) out[b] += re[k] * re[k] + im[k] * im[k];
        }
    }
}

int main(int argc, char **argv)
{
    const mdkit_voice *ref = &ref_voice, *port = &XCAT(mdkit_voice_, VOICE);
    const unsigned scenarios = argc > 1 ? (unsigned)atoi(argv[1]) : 40;
    const double level_limit = argc > 2 ? atof(argv[2]) : 1.0, band_limit = argc > 3 ? atof(argv[3]) : 3.0;
    const int verbose = argc > 4;
    double worst_level = 0, worst_band = 0;
    unsigned level_at = 0, band_at = 0;
    void *a = calloc(1, ref->state_bytes), *b = calloc(1, port->state_bytes);
    float *fa = malloc(sizeof(float) * 44100 * 2), *fb = malloc(sizeof(float) * 44100 * 2);
    static int32_t wb[4096];
    for (unsigned sc = 0; sc < scenarios; ++sc) {
        unsigned pos = 0, length = 44100u * (sc % 4 + 1) / 2;
        double sa = 0, sb = 0, pa[BANDS], pb[BANDS], top = 0, level, band = 0;
        int band_k = -1;
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
            ref->render(a, fa + pos, n);
            XCAT(XCAT(md_st_, VOICE), _render_words)(b, wb, n);
            for (unsigned i = 0; i < n; ++i) fb[pos + i] = (float)(wb[i] / 8388608.0);
            pos += n;
        }
        for (unsigned i = 0; i < length; ++i) { sa += (double)fa[i] * fa[i]; sb += (double)fb[i] * fb[i]; }
        if (sa < 1e-9 * length && sb < 1e-9 * length) continue;          /* both quieter than -90 dBFS */
        level = 10.0 * log10((sb + 1e-30) / (sa + 1e-30));
        bands(fa, length, pa); bands(fb, length, pb);
        for (unsigned k = 0; k < BANDS; ++k) if (pa[k] > top) top = pa[k];
        for (unsigned k = 0; k < BANDS; ++k)
            if (pa[k] > top * 1e-3) {
                const double d = fabs(10.0 * log10((pb[k] + 1e-30) / pa[k]));
                if (d > band) { band = d; band_k = (int)k; }
            }
        if (verbose) printf("scenario %2u: level %+5.2f dB, bands at most %5.2f dB (%.0f Hz, %.0f dB under the top)\n", sc, level, band,
                            25.0 * pow(2.0, band_k / 3.0), band_k < 0 ? 0.0 : 10.0 * log10(top / pa[band_k]));
        if (fabs(level) > fabs(worst_level)) { worst_level = level; level_at = sc; }
        if (band > worst_band) { worst_band = band; band_at = sc; }
    }
    {
        const int ok = fabs(worst_level) <= level_limit && worst_band <= band_limit;
        printf("%s %s: level within %.2f dB (scenario %u of %u), bands within %.2f dB (scenario %u); limits %.1f, %.1f dB\n",
               ref->name, ok ? "PASS" : "FAIL", fabs(worst_level), level_at, scenarios, worst_band, band_at, level_limit, band_limit);
        return !ok;
    }
}
