/* SPDX-License-Identifier: GPL-2.0-or-later
 * Synfact chorus, MAIN only (0.2: V56's DSP on a mono send). 32 frames at
 * 48 kHz in; mono at 24 kHz through the HPF and one delay ring; two
 * modulated taps make the stereo wet; 32 stereo frames at 48 kHz out. */
typedef unsigned int u32;
typedef int s32;
typedef unsigned short u16;
#define DATA __attribute__((section(".data")))
#define CH24_HISTORY 24        /* mono 48 kHz samples kept for the decimator */
#define CH24_UP_HISTORY 8      /* stereo 24 kHz frames kept for the reconstructor */
struct chorus_hot {
    s32 hp[2],target[12],current[12],return_gain[3],desired[3];
    u32 target_mask,current_mask,idle,quiet,phase;
    s32 width,depth_raw,amplitude,previous_triangle[2],pole_state[2];
    s32 previous_delay[2],position[2][2];
    u32 write_pos,initialized;
};
/* Hot state: in MAIN SRAM, as V56 had it, in its own window after LFO3's
 * kernel (<syntakt/sram.h>), followed there by the per-block code (hot.c);
 * in the bank's DDR when that window is not free at init (chorus_place). The PCM is the same in both: in DDR
 * every block refills the state's cache lines, which cost the unit MAIN CPU.
 * The delay ring (1 KB) stays in DDR. */
#include <syntakt/sram.h>
#define CHORUS_SRAM_AT ST_SRAM_CHORUS
#define CHORUS_SRAM_END ST_SRAM_TAIL0_END
extern struct chorus_hot *chorus_ch;
/* The block's buffers stay in the bank's DDR: written and read within each
 * block, their lines stay cached there, while the per-block code (hot.c)
 * takes the SRAM: on the unit each byte fetched from DDR per block costs the
 * same, and the code is the larger. */
struct chorus_buffers {
    s32 input[CH24_HISTORY+32],up[(CH24_UP_HISTORY+16)*2],wet[64];
};
extern struct chorus_buffers chorus_buf;
#define CH chorus_ch
_Static_assert(sizeof(struct chorus_hot)<=CHORUS_SRAM_END-CHORUS_SRAM_AT,"hot state outgrew its SRAM window");
extern const s32 chorus24_decimator_coeffs[],chorus24_up_coeffs[],chorus24_hpf24[],chorus24_lut[];
/* The kernels every active block runs (kernels.s, chorus_hot_code..
 * chorus_hot_code_end), called through these: their MAIN SRAM copy when
 * chorus_place made one, else the bank's DDR originals. */
struct chorus_kernels {
    void (*mix)(s32 *,const s32 *const *,s32 *,const s32 *,u32,u32);
    void (*decimate)(const s32 *,s32 *,const s32 *,s32 *);
    void (*hpf)(s32 *,s32 *,s32);
    void (*delay)(s32 *,const s32 *,const s32 *,u32);
    void (*upsample)(const s32 *,s32 *,const s32 *);
    void (*return_stable)(s32 *,const s32 *,s32);
};
extern struct chorus_kernels chorus_k;
/* hot.c, the per-block path between chorus_hot_c and chorus_hot_c_end: its
 * four entries, through these: the SRAM copy when chorus_place made one. */
struct st_track_sends;
struct chorus_entries {
    void (*sends)(const struct st_track_sends *);
    void (*before_delay)(void);
    void (*before_reverb)(void);
    void (*master_return)(void);
};
extern struct chorus_entries chorus_e;
extern const unsigned char chorus_hot_c[],chorus_hot_c_end[];      /* hot_begin.s, hot_end.s */
void chorus_hot_sends(const struct st_track_sends *);
void chorus_hot_before_delay(void),chorus_hot_before_reverb(void),chorus_hot_master_return(void);
void chorus24_mix(s32 *,const s32 *const *,s32 *,const s32 *,u32,u32);
void chorus24_decimate(const s32 *,s32 *,const s32 *,s32 *);
void chorus24_hpf(s32 *,s32 *,s32);
void chorus24_delay(s32 *,const s32 *,const s32 *,u32);
void chorus24_upsample(const s32 *,s32 *,const s32 *);
void chorus24_return_stable(s32 *,const s32 *,s32);
void chorus24_history(s32 *,const s32 *,s32 *,const s32 *);
void chorus_dsp_reset(void);
void chorus_ramp_return(s32 *,const s32 *,s32,s32,u32);
/* No "memory" clobber: the MAC touches only ACC0, and volatile keeps the
 * asm blocks in order. With it GCC reloaded every global (chorus_ch) after
 * each product: code the unit fetches from DDR every block. */
static inline s32 ch_mul(s32 a,s32 b) {
    s32 r;
    __asm__ volatile("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0":"=d"(r):"d"(a),"d"(b));
    return r;
}
static inline s32 ch_add_sat(s32 a,s32 b) {
    __asm__("add.l %1,%0\n\tsats.l %0":"+d"(a):"d"(b):"cc");return a;
}
