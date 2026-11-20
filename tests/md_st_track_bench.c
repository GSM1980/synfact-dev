/* SPDX-License-Identifier: GPL-2.0-or-later
 * md_st_track_bench.c - one track of the MD machine, md_update + md_render
 * (mods/md-st/audio/md.c) with the stock routines as no-ops, built for the
 * Syntakt's AUDIO CPU: tests/test_md_st.py counts its instructions a block in
 * Unicorn, the voice and the resampler together.
 */
#define MD_STOCK 1
static unsigned int track_blocks[2][0x708 / 4];
static int scratch_a[32], scratch_b[32];
static unsigned short ext_words[20];
static const unsigned short *ext_of(const void *p) { (void)p; return ext_words; }
#define MD_EXTENDED ext_of
#define MD_TRACK_BASE ((uint32_t)(uintptr_t)track_blocks)
#define MD_SCRATCH_A scratch_a
#define MD_SCRATCH_B scratch_b
#define MD_TWINSHOT_UPDATE(pitch, voice, native) ((void)(pitch), (void)(voice), (void)(native))
#define MD_TWINSHOT_CHAIN(a, b, out, locals, voice) do { \
    unsigned i_; for (i_ = 0; i_ < 32; ++i_) ((int *)(out))[i_] = (a)[i_]; (void)(b); (void)(locals); (void)(voice); } while (0)
#include "../mods/md-st/audio/md.c"

unsigned short bench_params[71];
unsigned int bench_type, bench_trigger;
int bench_track_out[32];
void bench_track_block(void)
{
    ext_words[0] = (unsigned short)(bench_type << 8);
    track_blocks[0][0x38 / 4] = bench_trigger; bench_trigger = 0;
    md_update(60u << 16, track_blocks[0], bench_params);
    md_render(bench_track_out, track_blocks[0]);
}
/* n blocks; the FNV-1a hash of their output (for analog-emu's cfbench) */
unsigned bench_track_run(unsigned n)
{
    unsigned h = 0x811c9dc5u, b, i;
    for (b = 0; b < n; ++b) {
        bench_track_block();
        for (i = 0; i < 32; ++i) h = (h ^ (unsigned)bench_track_out[i]) * 16777619u;
    }
    return h;
}
/* 1: the EMAC passed md.c's self-test and runs; 2: it failed (the C voices run) */
unsigned bench_emac_status(void) { return md_emac_status; }

#ifndef MD_ST_TARGET
#include <stdio.h>
#include <stdlib.h>
/* host: md_st_track_bench <type> <warm blocks> <blocks> <knob0..7>: the same
 * runs as tests/test_md_st.py's on the Syntakt's CPU, and the hash of the last */
int main(int argc, char **argv)
{
    static const unsigned char slots[8] = {0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x1a, 0x19};
    unsigned i, warm, blocks;
    if (argc < 12) return 2;
    bench_type = (unsigned)atoi(argv[1]); warm = (unsigned)atoi(argv[2]); blocks = (unsigned)atoi(argv[3]);
    for (i = 0; i < 8; ++i) bench_params[slots[i]] = (unsigned short)(atoi(argv[4 + i]) << 8);
    bench_trigger = 1;
    bench_track_run(warm);
    printf("%08x\n", bench_track_run(blocks));
    return 0;
}
#endif
