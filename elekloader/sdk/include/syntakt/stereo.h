/* SPDX-License-Identifier: GPL-2.0-or-later
 * Stereo tracks (core 1.8): fixed addresses shared by the AUDIO engine, the
 * MAIN mixer side and machines. docs/MACHINE-REFERENCE.md, "Stereo tracks",
 * has the reasons for each address.
 *
 * A digital track stays mono up to MAIN's mixer: its render writes L, the
 * native chain (drive, base-width, multimode, AMP) runs on it, and MAIN mixes
 * it with level x pan gains. A stereo machine also renders R into
 * ST_STEREO_R(t). At the chain end the engine runs the same chain on R, with
 * R's own filter memory, and sends D = R - L through the shared window. MAIN
 * adds gR x D to every right bus, so left = gL x L and right = gR x R:
 * pan acts as balance, and a lost D leaves the track mono (L on both sides).
 */
#ifndef ELEKLOADER_ST_STEREO_H
#define ELEKLOADER_ST_STEREO_H

/* AUDIO, core 1.8: the chain-end directory {magic, hook, version, multimode}.
 * The core calls word 1 once a block, after AMP and before the track DMA; it
 * is an rts until a bank writes its own hook there. An older core has no
 * magic. Version 2 (core 2.5): the stock chain loads its per-track multimode
 * routine from word 3 (40000a72, `movea.l 40040f3c,a4`), the native
 * 0x400019d6 until a bank writes its own entry there; the entry takes the
 * native's arguments (row + 0x52, buffer, x, track, trig, word) and must
 * leave what the native leaves. Version 1 has 0 in word 3. */
#define ST_CHAIN_DIRECTORY ((volatile unsigned int *)0x40040f30u)
#define ST_CHAIN_MAGIC 0x454c5301u
#define ST_CHAIN_MULTIMODE_VERSION 2u
#define ST_CHAIN_MULTIMODE_NATIVE 0x400019d6u

/* AUDIO SRAM 0x8000f398..0x8000ffff: unused by the stock OS and by every
 * other mod (device check 2026-11-10). The engine owns it
 * (docs/MACHINE-REFERENCE.md, "AUDIO's SRAM", has the map). */
#ifndef ST_STEREO_R
#define ST_STEREO_R(t) ((int *)(0x8000f400u+128u*(t)))      /* 8 x 32 Q31 */
#endif
#define ST_STEREO_SIGNAL ((unsigned int *)0x8000f800u)      /* 40 words: R's chain memory */
#define ST_STEREO_SNAP_A ((unsigned int *)0x8000f8a0u)      /* 40 words (of 172): the drive state from before the L drive */
#define ST_STEREO_SNAP_B ((unsigned int *)0x8000fb50u)      /* the window block's staging (ST_STEREO_BLOCK bytes) */
#define ST_STEREO_FILTER(t) ((unsigned int *)(0x8000fe00u+16u*(t))) /* render filter, 4 words */
#define ST_STEREO_SCRATCH ((int *)0x8000fe80u)              /* 32 words for a render; then the fused multimode's */
/* 0x8000ff00..0x8000ffff: the fused multimode's R buffer and second output
 * (mods/samplers-st/stereo/multimode.s). */
#define ST_STEREO_WORDS 172u
#define ST_STEREO_SIGNAL_WORDS 40u

/* The shared window, 0x5b0..0x7ef (free; device check 2026-11-10). AUDIO
 * sees it at 0, MAIN at 0x10000000. Each block AUDIO writes 0x5b0..0x7df in
 * ascending order (eDMA channel 46 since samplers-st 1.6, the whole block;
 * the mantissas of tracks outside the mask are stale): the sequence comes
 * first and again last, so a reader that reads SEQ_END, then the block, then
 * SEQ and finds them equal has read one whole block (MAIN's eDMA reads it
 * descending for that). D is sent as 16-bit mantissas with one shift per
 * track and block: D = mantissa << (shift + 1), Q31. */
#define ST_STEREO_SEQ 0x5b0u                    /* u32: the block's sequence */
#define ST_STEREO_MASK 0x5b4u                   /* u32: bit t, track t has D */
#define ST_STEREO_SHIFT 0x5b8u                  /* 8 bytes */
#define ST_STEREO_PRINT 0x5c0u                  /* u32: st_stereo_print() of the block's L buffers */
#define ST_STEREO_TAG 0x5c4u                    /* u32: ST_STEREO_MAGIC */
#define ST_STEREO_MAGIC 0x53545232u             /* "STR2"; the window is not cleared at boot */
#define ST_STEREO_COUNT 0x5c8u                  /* u32: blocks with any stereo track; +4 chain-end calls */
#define ST_STEREO_WINDOW 0x5d0u                 /* 8 x 32 halfwords */
#define ST_STEREO_SEQ_END 0x7d0u                /* u32: the sequence again */
#define ST_STEREO_BLOCK 0x230u                  /* bytes from ST_STEREO_SEQ (staged in AUDIO SRAM) */
#define ST_STEREO_PROFILE 0x7e0u                /* 4 u32 for test pages, written by the CPU:
                                                   block ticks / 1024, then permille pairs
                                                   STATE|DRIVE, MULTI|AMP, SEND|RENDER */
#define ST_STEREO_MAIN 0x10000000u

/* AUDIO, from a machine's render: claim track t as stereo for this block and
 * get the 32 words to write R into, or 0 on a core whose chain directory is
 * not version 2 (core 2.5). The engine is mods/samplers-st/stereo/audio.c
 * and multimode.s (linked into the bank) until it moves into the core. */
int *st_stereo_claim(unsigned int track);
/* AUDIO, for a machine deciding whether a track's tail still needs R: one of
 * 32 Q31 samples at or above ST_STEREO_QUIET in magnitude (2^16, -96 dBFS:
 * below one 16-bit step). Exact zero is not the test: the native multimode's
 * types 0 and 1 settle on a residual of a few hundred (about -138 dBFS) and
 * never reach 0, which kept a track stereo, and its R pass running, for good. */
#define ST_STEREO_QUIET 0x10000u
static __inline__ int st_stereo_audible(const int *x)
{
    unsigned int i;
    for(i=0;i<32;i++)if((unsigned int)x[i]+(ST_STEREO_QUIET-1u)>2u*(ST_STEREO_QUIET-1u))return 1;
    return 0;
}
/* Both CPUs: the fingerprint of eight tracks' 32-word L buffers (samples 0, 8,
 * 16 and 24 of each), as AUDIO sends them and MAIN receives them. */
static __inline__ unsigned int st_stereo_print(const volatile int *tracks)
{
    unsigned int t,i,h=0x811c9dc5u;
    for(t=0;t<8;t++)for(i=0;i<32;i+=8)h=(h*33u)^(unsigned int)tracks[32*t+i];
    return h;
}
/* MAIN, audio interrupt (from ev_render_in on): track t's D = R - L for the
 * block being mixed, 32 values in PCM16 units; 0 when the track is mono.
 * mods/samplers-st/stereo/main.c; reference it weak from other mods. */
int st_stereo_track_d(unsigned int track,int *out);
/* AUDIO, test pages: the profile's clock, and a render's R time since start. */
unsigned int st_stereo_clock(void);
void st_stereo_render_spent(unsigned int start);

#endif
