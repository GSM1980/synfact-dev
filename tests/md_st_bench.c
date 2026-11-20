/* SPDX-License-Identifier: GPL-2.0-or-later
 * md_st_bench.c - a voice of the MD machine on its own, built for the
 * Syntakt's AUDIO CPU. tests/test_md_st.py links it with the voice, the
 * tables and the runtime, runs it in Unicorn (ColdFire V4e), counts the
 * instructions of each 32-word block and compares the words with the host's.
 * The same file built for the host prints those words.
 */
#include "../mods/md-st/audio/voices/mdkit.h"
#include "../mods/md-st/audio/voices/md-types.h"
#include "../mods/md-st/audio/voices/md-native.h"
#define CAT(a, b) a##b
#define XCAT(a, b) CAT(a, b)
extern const mdkit_voice XCAT(mdkit_voice_, VOICE);
void XCAT(XCAT(md_st_, VOICE), _render_words)(void *state, int32_t *out, unsigned frames);

/* state for the largest voice; the host's harness checks state_bytes */
unsigned bench_state[16384] __attribute__((aligned(16)));
int32_t bench_out[32];
unsigned bench_knobs[8];

/* the voice's calls, with the EMAC set up as md.c sets it */
void bench_init(void)
{
    const mdkit_voice *v = &XCAT(mdkit_voice_, VOICE);
#ifdef MD_EMAC
    struct md_emac_saved e;
    md_emac_begin(&e);
#endif
    v->init(bench_state);
    for (unsigned i = 0; i < v->control_count; ++i) v->set_control(bench_state, i, bench_knobs[i]);
#ifdef MD_EMAC
    md_emac_end(&e);
#endif
}
void bench_trigger(void)
{
#ifdef MD_EMAC
    struct md_emac_saved e;
    md_emac_begin(&e);
#endif
    XCAT(mdkit_voice_, VOICE).trigger(bench_state);
#ifdef MD_EMAC
    md_emac_end(&e);
#endif
}
/* one 32-word block; on the Syntakt the EMAC set up as md_render sets it */
void bench_block(void)
{
#ifdef MD_EMAC
    struct md_emac_saved e;
    md_emac_begin(&e);
#endif
    XCAT(XCAT(md_st_, VOICE), _render_words)(bench_state, bench_out, 32);
#ifdef MD_EMAC
    md_emac_end(&e);
#endif
}
/* n blocks; returns the FNV-1a hash of every word, for analog-emu's cfbench,
 * which prints d0 and the instructions of the call */
unsigned bench_run(unsigned n)
{
    unsigned h = 0x811c9dc5u, b, i;
    for (b = 0; b < n; ++b) {
        bench_block();
        for (i = 0; i < 32; ++i) h = (h ^ (unsigned)bench_out[i]) * 16777619u;
    }
    return h;
}
unsigned bench_state_bytes(void) { return (unsigned)XCAT(mdkit_voice_, VOICE).state_bytes; }

#ifndef MD_ST_TARGET
#include <stdio.h>
#include <stdlib.h>
/* host: md_st_bench <blocks> <knob0..7>; prints the words, one block a line.
 * md_st_bench hash <blocks> <knob0..7>: prints bench_run's hash instead. */
int main(int argc, char **argv)
{
    int hash = argc > 1 && argv[1][0] == 'h';
    if (hash) { --argc; ++argv; }
    unsigned blocks = argc > 1 ? (unsigned)atoi(argv[1]) : 64;
    for (unsigned i = 0; i < 8; ++i) bench_knobs[i] = argc > 2 + (int)i ? (unsigned)atoi(argv[2 + i]) : 64;
    bench_init(); bench_trigger();
    if (hash) { printf("%08x\n", bench_run(blocks)); return 0; }
    for (unsigned b = 0; b < blocks; ++b) {
        bench_block();
        for (unsigned i = 0; i < 32; ++i) printf("%d%c", (int)bench_out[i], i == 31 ? '\n' : ' ');
    }
    return 0;
}
#endif
