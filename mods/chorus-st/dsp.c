/* SPDX-License-Identifier: GPL-2.0-or-later
 * The chorus's memory: its hot state, delay ring and kernels, and where they
 * run. The per-block code itself is hot.c (copied to SRAM) and kernels.s. */
#include "chorus.h"
static struct chorus_hot ddr_hot DATA={0};
struct chorus_hot *chorus_ch DATA=&ddr_hot;
s32 chorus_ring[257] DATA={0};
struct chorus_buffers chorus_buf DATA={{0},{0},{0}};
static const struct chorus_kernels ddr_kernels={chorus24_mix,chorus24_decimate,chorus24_hpf,
    chorus24_delay,chorus24_upsample,chorus24_return_stable};
struct chorus_kernels chorus_k DATA={chorus24_mix,chorus24_decimate,chorus24_hpf,
    chorus24_delay,chorus24_upsample,chorus24_return_stable};
static const struct chorus_entries ddr_entries={chorus_hot_sends,chorus_hot_before_delay,
    chorus_hot_before_reverb,chorus_hot_master_return};
struct chorus_entries chorus_e DATA={chorus_hot_sends,chorus_hot_before_delay,
    chorus_hot_before_reverb,chorus_hot_master_return};
extern const unsigned char chorus_hot_code[],chorus_hot_code_end[];
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
#define HOT_C chorus_hot_c
#define HOT_C_BYTES ((u32)(chorus_hot_c_end-chorus_hot_c))
#define HOT_C_AT (CHORUS_SRAM_AT+sizeof(struct chorus_hot))
/* Module init, task context, before any block. Each SRAM window
 * (<syntakt/sram.h>) still all zero as the boot leaves it takes its part:
 * the first the hot state and, after it, the per-block code (hot.c); the
 * second the kernels. A checked copy, then the instruction caches are
 * invalidated (as LFO3). Returns bit 0: state, 1: kernels, 2: per-block code. */
u32 chorus_place(void)
{
    u32 placed=0,n=(u32)(chorus_hot_code_end-chorus_hot_code);
    chorus_ch=&ddr_hot;chorus_k=ddr_kernels;chorus_e=ddr_entries;
    if(zero(CHORUS_SRAM_AT,CHORUS_SRAM_END)){
        chorus_ch=(struct chorus_hot *)CHORUS_SRAM_AT;placed|=1;
        if(HOT_C_AT+HOT_C_BYTES<=CHORUS_SRAM_END && copy(HOT_C_AT,HOT_C,HOT_C_BYTES))placed|=4;
    }
    if(ST_SRAM_CHORUS_CODE+n<=ST_SRAM_CHORUS_CODE_END && zero(ST_SRAM_CHORUS_CODE,ST_SRAM_CHORUS_CODE_END) &&
       copy(ST_SRAM_CHORUS_CODE,chorus_hot_code,n))placed|=2;
    if(placed&6)chorus_sync();
    if(placed&2){
        chorus_k.mix=moved(chorus24_mix,chorus_hot_code,ST_SRAM_CHORUS_CODE);
        chorus_k.decimate=moved(chorus24_decimate,chorus_hot_code,ST_SRAM_CHORUS_CODE);
        chorus_k.delay=moved(chorus24_delay,chorus_hot_code,ST_SRAM_CHORUS_CODE);
        chorus_k.upsample=moved(chorus24_upsample,chorus_hot_code,ST_SRAM_CHORUS_CODE);
        chorus_k.return_stable=moved(chorus24_return_stable,chorus_hot_code,ST_SRAM_CHORUS_CODE);
    }
    if(placed&4){
        chorus_e.sends=moved(chorus_hot_sends,HOT_C,HOT_C_AT);
        chorus_e.before_delay=moved(chorus_hot_before_delay,HOT_C,HOT_C_AT);
        chorus_e.before_reverb=moved(chorus_hot_before_reverb,HOT_C,HOT_C_AT);
        chorus_e.master_return=moved(chorus_hot_master_return,HOT_C,HOT_C_AT);
    }
    return placed;
}
/* UI tick, every two seconds: if anything changed an SRAM copy, go back to
 * the DDR originals for good (LFO3 does the same with its kernel). */
void chorus_check_code(void)
{
    static u32 ticks DATA=0;
    if(++ticks%60u)return;
    if(chorus_k.mix!=chorus24_mix &&
       !same(ST_SRAM_CHORUS_CODE,chorus_hot_code,(u32)(chorus_hot_code_end-chorus_hot_code)))chorus_k=ddr_kernels;
    if(chorus_e.sends!=chorus_hot_sends && !same(HOT_C_AT,HOT_C,HOT_C_BYTES))chorus_e=ddr_entries;
}
/* Offsets the emulator tests read (index 8: position); 0 for what moved. */
const u32 chorus24_layout[]={sizeof(struct chorus_hot),0u,0u,0u,0u,
    __builtin_offsetof(struct chorus_hot,hp),__builtin_offsetof(struct chorus_hot,target),
    __builtin_offsetof(struct chorus_hot,current),__builtin_offsetof(struct chorus_hot,position),
    __builtin_offsetof(struct chorus_hot,write_pos),__builtin_offsetof(struct chorus_hot,initialized)};
/* A return whose gain moves: bus += g*wet (or -= for the inverted FX bus),
 * the gain stepping before every frame; V56's EMAC steps, in C. Rare
 * (DEL/REV/VOL moving): out of hot.c's SRAM span. */
void chorus_ramp_return(s32 *bus,const s32 *w,s32 g,s32 step,u32 subtract)
{
    u32 i;
#pragma GCC unroll 1
    for(i=0;i<32;i++) {
        s32 l,r;
        g+=step;
        l=ch_mul(g,w[0]);r=ch_mul(g,w[1]);
        if(subtract) {l=-l;r=-r;}
        bus[0]=ch_add_sat(l,bus[0]);bus[1]=ch_add_sat(r,bus[1]);
        bus+=2;w+=2;
    }
}
void chorus_dsp_reset(void)
{
    u32 i,ch;
    s32 initial=(chorus24_lut[0]>>1)-(3<<22);
    u32 *words=(u32 *)CH;
    for(i=0;i<sizeof(*CH)/4;i++) words[i]=0;
    for(i=0;i<257;i++) chorus_ring[i]=0;
    for(i=0;i<sizeof(chorus_buf)/4;i++) ((u32 *)&chorus_buf)[i]=0;
    for(ch=0;ch<2;ch++) CH->previous_delay[ch]=initial;
    CH->idle=1;CH->initialized=1;
}
