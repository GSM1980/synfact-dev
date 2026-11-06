/* SPDX-License-Identifier: GPL-2.0-or-later
 * MAIN SRAM the stock boot leaves zero, and which module owns each window.
 * SRAM never misses in cache: audio-IRQ code and state run faster there than
 * in the bank's DDR. Each owner checks only its own window is still zero at
 * init and falls back to DDR otherwise, so bank order does not matter.
 * Physical exclusivity against every stock IRQ/DMA path is not proven; the
 * evidence is that the stock boot leaves these tails zero and nothing writes
 * them in the traces taken (LFO3, Synfact's chorus).
 */
#ifndef ELEKLOADER_ST_SRAM_H
#define ELEKLOADER_ST_SRAM_H
#define ST_SRAM_TAIL0 0x80007330u        /* first stock-zero tail */
#define ST_SRAM_LFO3_CODE 0x80007340u    /* LFO3's render kernel, up to ... */
#define ST_SRAM_CHORUS 0x800077c0u       /* ... the chorus's hot state and per-block
                                           * code, up to ... (LFO3's kernel takes 1,088
                                           * of its 1,152 bytes; the unit ran V56's
                                           * state at 80007340..80007998 and chorus
                                           * test d's at 800079a0..80007ff8) */
#define ST_SRAM_TAIL0_END 0x80008000u    /* ... the tail's end */
#define ST_SRAM_TAIL1 0x8000f860u        /* second stock-zero tail */
#define ST_SRAM_LFO3_BANK 0x8000f870u    /* LFO3's state and parameter bank, up to ... */
#define ST_SRAM_CHORUS_CODE 0x8000fc20u  /* ... the chorus's hot kernels, up to ... */
#define ST_SRAM_CHORUS_CODE_END 0x8000ff94u /* ... what the unit ran them in (chorus test b,
                                           * 2026-11-03); a build that used up to 8000ffde
                                           * hung at boot. Nothing above this: unverified. */
#define ST_SRAM_TAIL1_END 0x80010000u    /* ... the tail's end */
#endif
