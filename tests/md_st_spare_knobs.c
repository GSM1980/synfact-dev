/* SPDX-License-Identifier: GPL-2.0-or-later
 * md_st_spare_knobs.c - a voice with fewer than eight knobs ignores the others: md.c
 * on the host (the stock TwinShot routines replaced as in md_st_glue_check.c), each
 * such voice rendered twice from a note, its unnamed knobs at 0 and then at 127 (and
 * turned while it sounds): the outputs must be the same, word for word.
 * Built and run by tests/test_md_st.py.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define MD_STOCK 1
static uint32_t blocks[8][0x708 / 4];
static int32_t scratch_a[32], scratch_b[32];
static unsigned short ext_words[20];
static const unsigned short *ext_of(const void *p) { (void)p; return ext_words; }
#define MD_EXTENDED ext_of
#define MD_TRACK_BASE ((uint32_t)(uintptr_t)blocks)
#define MD_SCRATCH_A scratch_a
#define MD_SCRATCH_B scratch_b
#define MD_TWINSHOT_UPDATE(pitch, voice, native) ((void)(pitch), (void)(voice), (void)(native))
#define MD_TWINSHOT_CHAIN(a, b, out, locals, voice) do { \
    (void)(b); (void)(locals); (void)(voice); memcpy((out), (a), 128); } while (0)
#include "../mods/md-st/audio/md.c"

#define BLOCKS 400
static const unsigned char slot_of[8] = {0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x1a, 0x19};
static int32_t out_a[BLOCKS * 32], out_b[BLOCKS * 32];

/* the voice on track t from a note; spare knobs at `spare`, turned to `turn` at block 100 */
static void play(unsigned type, unsigned t, unsigned spare, unsigned turn, int32_t *out)
{
    unsigned short params[71];
    unsigned b, i, n = md_voice[type]->control_count;
    memset(params, 0, sizeof params);
    memset(&md_tracks[t], 0, sizeof md_tracks[t]);
    ext_words[0] = (unsigned short)(type << 8);
    for (i = 0; i < 8; ++i) params[slot_of[i]] = (unsigned short)((i < n ? 40 + 9 * i : spare) << 8);
    for (b = 0; b < BLOCKS; ++b) {
        if (b == 100) for (i = n; i < 8; ++i) params[slot_of[i]] = (unsigned short)(turn << 8);
        blocks[t][0x38 / 4] = b == 0 || b == 200;
        md_update(60u << 16, blocks[t], params);
        md_render(out + 32 * b, blocks[t]);
    }
}

int main(void)
{
    unsigned type, bad = 0, checked = 0;
    for (type = 0; type < MD_VOICES; ++type) {
        if (md_voice[type]->control_count >= 8) continue;
        play(type, 2, 0, 0, out_a);
        play(type, 3, 127, 33, out_b);
        if (memcmp(out_a, out_b, sizeof out_a)) { printf("FAIL %s: an unnamed knob changes the sound\n", md_voice[type]->name); bad = 1; }
        ++checked;
    }
    if (!bad) printf("PASS unnamed knobs: %u voices with fewer than eight knobs ignore the others\n", checked);
    return bad;
}
