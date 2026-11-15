/* SPDX-License-Identifier: GPL-2.0-or-later
 * Synfact chorus, MAIN only: V56's DSP on a mono send. 32 frames at 48 kHz
 * in; mono at 24 kHz through the HPF and one delay ring; two modulated taps
 * make the stereo wet; 32 stereo frames at 48 kHz out. Since 1.2 the
 * per-block path is assembly (hot.s, kernels.s) on one state block. */
typedef unsigned int u32;
typedef int s32;
typedef unsigned short u16;
#define DATA __attribute__((section(".data")))
#include <syntakt/sram.h>
/* The state block, as state.inc lays it out (the asm's offsets; checked
 * below). In MAIN SRAM at CHORUS_SRAM_AT with the per-block code (hot.s)
 * right after it, or in the bank's DDR right before that code's original
 * (chorus_ddr_state): the code reaches it PC-relative either way. */
#define CHORUS_SRAM_AT ST_SRAM_CHORUS
#define CHORUS_SRAM_END ST_SRAM_TAIL0_END
struct chorus_record {             /* up to four tracks of the mono mix */
    s32 gain[4];
    const s32 *pcm[4];
    s32 step[4];
    u32 entry_stable,entry_ramp;   /* the first group skips its empty slots */
};
struct chorus_state {
    s32 target[12],current[12];
    u32 target_mask,current_mask,idle,quiet,dirty,ramp,groups;
    s32 return_gain[3],desired[3];
    s32 hp[2],hpf_c;
    u32 phase;
    s32 width,depth_raw,amplitude,previous_triangle[2],pole_state[2];
    s32 previous_delay[2],position[2][2];
    u32 write_pos,initialized;
    const u16 *fx;
    u16 key[8],values[8];
    u32 end,post;                  /* post: the wet at ev_main_mix (1), or none (2) */
    struct chorus_record rec[3];
    void *k[8];                    /* kernel addresses: kept by the reset */
};
#define CHORUS_KERNELS 8
#define CHORUS_RING 304            /* 256 samples and a mirror of the first 48 */
#define OFF(f) __builtin_offsetof(struct chorus_state,f)
_Static_assert(OFF(current)==48 && OFF(target_mask)==96 && OFF(groups)==120 && OFF(return_gain)==124 &&
               OFF(desired)==136 && OFF(hp)==148 && OFF(hpf_c)==156 && OFF(phase)==160 && OFF(previous_triangle)==176 &&
               OFF(previous_delay)==192 && OFF(position)==200 && OFF(write_pos)==216 && OFF(initialized)==220 &&
               OFF(fx)==224 && OFF(key)==228 && OFF(values)==244 && OFF(end)==260 && OFF(post)==264 &&
               OFF(rec)==268 && sizeof(struct chorus_record)==56 && OFF(k)==436 && sizeof(struct chorus_state)==468,"chorus_state does not match state.inc");
_Static_assert(sizeof(struct chorus_state)<=CHORUS_SRAM_END-CHORUS_SRAM_AT,"state outgrew its SRAM window");
extern struct chorus_state chorus_ddr_state;
extern struct chorus_state *chorus_ch;      /* the one in use */
extern s32 chorus_ring[CHORUS_RING];
/* The block's buffers, in the bank's DDR: 24 + 32 mono samples at 48 kHz,
 * 8 + 16 stereo frames at 24 kHz, the block's wet (32 stereo frames; first
 * the decimator's partial sums). */
extern s32 chorus_in[56],chorus_up[48],chorus_wet[64];
extern const s32 chorus24_decimator_coeffs[],chorus24_up_coeffs[],chorus24_hpf24[],chorus24_lut[];
/* hot.s, the per-block path between chorus_hot_c and chorus_hot_c_end: its
 * four entries, through these: the SRAM copy when chorus_place made one. */
struct st_track_sends;
struct chorus_entries {
    void (*sends)(const struct st_track_sends *);
    void (*before_delay)(void);
    void (*before_reverb)(void);
    void (*master_return)(void);
};
extern struct chorus_entries chorus_e;
extern const unsigned char chorus_hot_c[],chorus_hot_c_end[];
extern const unsigned char chorus_hot_code[],chorus_hot_code_end[];   /* kernels.s's span */
void chorus_hot_sends(const struct st_track_sends *);
void chorus_hot_before_delay(void),chorus_hot_before_reverb(void),chorus_hot_master_return(void);
void chorus_k_mixs(void),chorus_k_mixr(void),chorus_k_dec(void),chorus_k_delay(void),chorus_k_up(void),
     chorus_k_ret(void),chorus_k_build(void),chorus_k_mod(void);
void chorus_dsp_reset(void);
void chorus_state_reset(struct chorus_state *);
