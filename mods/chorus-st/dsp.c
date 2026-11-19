/* SPDX-License-Identifier: GPL-2.0-or-later
 * The chorus's memory: its state block, delay ring and code, and where they
 * run. The per-block code itself is hot.s and kernels.s (assembly). */
#include "chorus.h"
struct chorus_state chorus_ddr_state DATA __attribute__((aligned(16)));
struct chorus_state *chorus_ch DATA=&chorus_ddr_state;
s32 chorus_ring[CHORUS_RING] DATA={0};
s32 chorus_in[56] DATA __attribute__((aligned(16)))={0};
s32 chorus_up[48] DATA __attribute__((aligned(16)))={0};
s32 chorus_wet[64] DATA __attribute__((aligned(16)))={0};
static const struct chorus_entries ddr_entries={chorus_hot_sends,chorus_hot_before_delay,
    chorus_hot_before_reverb,chorus_hot_master_return};
struct chorus_entries chorus_e DATA={chorus_hot_sends,chorus_hot_before_delay,
    chorus_hot_before_reverb,chorus_hot_master_return};
/* In state.inc's K_* order. */
static void (*const kernels[CHORUS_KERNELS])(void)={chorus_k_mixs,chorus_k_mixr,chorus_k_dec,
    chorus_k_delay,chorus_k_up,chorus_k_ret,chorus_k_build,chorus_k_mod};
extern void chorus_sync(void);
static int zero(u32 at,u32 end)
{
    const volatile u32 *w=(const volatile u32 *)at;u32 i;
    for(i=0;i<(end-at)/4;i++)if(w[i])return 0;
    return 1;
}
/* At run time: folded, "symbol + SRAM address" became a relocation whose
 * addend points outside the bank image, which the core's parser refuses. */
static void *moved(const void *f,const void *from,u32 to)
{
    u32 a=(u32)f,b=(u32)from;
    __asm__("" : "+d"(a),"+d"(b));
    return (void *)(a-b+to);
}
/* Copy [from, from+n) to SRAM at `to` and check the copy; 0 if it differs. */
static int copy(u32 to,const unsigned char *from,u32 n)
{
    volatile unsigned char *code=(volatile unsigned char *)to;u32 i;
    for(i=0;i<n;i++)code[i]=from[i];
    for(i=0;i<n && code[i]==from[i];i++);
    if(i==n)return 1;
    for(i=0;i<n;i++)code[i]=0;
    return 0;
}
static int same(u32 at,const unsigned char *from,u32 n)
{
    const volatile unsigned char *code=(const volatile unsigned char *)at;u32 i;
    for(i=0;i<n;i++)if(code[i]!=from[i])return 0;
    return 1;
}
#define SRAM_STATE ((struct chorus_state *)CHORUS_SRAM_AT)
#define HOT_C_BYTES ((u32)(chorus_hot_c_end-chorus_hot_c))
#define HOT_C_AT (CHORUS_SRAM_AT+sizeof(struct chorus_state))
#define HOT_K_BYTES ((u32)(chorus_hot_code_end-chorus_hot_code))
static u32 kernels_placed DATA=0;
static int in(const void *f,const unsigned char *lo,const unsigned char *hi)
{
    return (const unsigned char *)f>=lo && (const unsigned char *)f<hi;
}
/* Each state's kernel table: a kernel of hot.s's span runs where that state's
 * code runs (SRAM state: the SRAM copy), one of kernels.s's span from its
 * SRAM copy while that copy is good, the others from DDR. */
static void tables(void)
{
    u32 i;
    for(i=0;i<CHORUS_KERNELS;i++){
        void *f=(void *)kernels[i],*sram=f;
        if(kernels_placed && in(f,chorus_hot_code,chorus_hot_code_end))
            f=sram=moved(f,chorus_hot_code,ST_SRAM_CHORUS_CODE);
        else if(in(f,chorus_hot_c,chorus_hot_c_end))
            sram=moved(f,chorus_hot_c,HOT_C_AT);
        chorus_ddr_state.k[i]=f;
        if(chorus_ch==SRAM_STATE)SRAM_STATE->k[i]=sram;
    }
}
/* Module init, task context, before any block. Each SRAM window
 * (<syntakt/sram.h>) still all zero as the boot leaves it takes its part:
 * the first the state block and, after it, hot.s's code; the second
 * kernels.s's span. A checked copy, then the instruction caches are
 * invalidated (as LFO3). Returns bit 0: state, 1: kernels, 2: per-block
 * code (the state and its code go together). */
u32 chorus_place(void)
{
    u32 placed=0;
    chorus_ch=&chorus_ddr_state;chorus_e=ddr_entries;kernels_placed=0;
    if(HOT_C_AT+HOT_C_BYTES<=CHORUS_SRAM_END && zero(CHORUS_SRAM_AT,CHORUS_SRAM_END) &&
       copy(HOT_C_AT,chorus_hot_c,HOT_C_BYTES))placed|=5;
    if(ST_SRAM_CHORUS_CODE+HOT_K_BYTES<=ST_SRAM_CHORUS_CODE_END && zero(ST_SRAM_CHORUS_CODE,ST_SRAM_CHORUS_CODE_END) &&
       copy(ST_SRAM_CHORUS_CODE,chorus_hot_code,HOT_K_BYTES))placed|=2;
    if(placed)chorus_sync();
    kernels_placed=placed&2;
    if(placed&4){
        chorus_ch=SRAM_STATE;
        chorus_e.sends=moved(chorus_hot_sends,chorus_hot_c,HOT_C_AT);
        chorus_e.before_delay=moved(chorus_hot_before_delay,chorus_hot_c,HOT_C_AT);
        chorus_e.before_reverb=moved(chorus_hot_before_reverb,chorus_hot_c,HOT_C_AT);
        chorus_e.master_return=moved(chorus_hot_master_return,chorus_hot_c,HOT_C_AT);
    }
    tables();
    return placed;
}
/* UI tick, every two seconds: if anything changed an SRAM copy, go back to
 * the DDR originals for good (LFO3 does the same with its kernel). The DDR
 * state starts over (its first block resets it). */
void chorus_check_code(void)
{
    static u32 ticks DATA=0;
    if(++ticks%60u)return;
    if(kernels_placed && !same(ST_SRAM_CHORUS_CODE,chorus_hot_code,HOT_K_BYTES)){kernels_placed=0;tables();}
    if(chorus_ch==SRAM_STATE && !same(HOT_C_AT,chorus_hot_c,HOT_C_BYTES)){
        chorus_e=ddr_entries;chorus_ch=&chorus_ddr_state;
    }
}
/* Offsets the emulator tests read (index 8: position); 0 for what moved. */
const u32 chorus24_layout[]={sizeof(struct chorus_state),0u,0u,0u,0u,OFF(hp),OFF(target),OFF(current),
    OFF(position),OFF(write_pos),OFF(initialized)};
/* Everything but the kernel table, the ring and the buffers; the DSP starts
 * asleep. */
void chorus_state_reset(struct chorus_state *s)
{
    u32 i,*w=(u32 *)s;
    for(i=0;i<OFF(k)/4;i++)w[i]=0;
    for(i=0;i<CHORUS_RING;i++)chorus_ring[i]=0;
    for(i=0;i<56;i++)chorus_in[i]=0;
    for(i=0;i<48;i++)chorus_up[i]=0;
    for(i=0;i<64;i++)chorus_wet[i]=0;
    s->previous_delay[0]=s->previous_delay[1]=(chorus24_lut[0]>>1)-(3<<22);
    s->idle=1;s->initialized=1;
}
void chorus_dsp_reset(void) {chorus_state_reset(chorus_ch);}
