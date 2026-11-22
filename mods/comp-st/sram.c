/* SPDX-License-Identifier: GPL-2.0-or-later
 * Only the documented, formerly unused end of LFO3's first window.
 * LFO3 checks its narrowed window, so module init order does not matter.
 * Checked detector/curve copy and hot state; Chorus keeps its reservation.
 */
#include "comp.h"
#include <syntakt/sram.h>
#define GUARD0 0x434f4d50u
#define GUARD1 0xbcb0b2afu
#define WORDS (sizeof(struct comp_runtime)/4)
extern const u8 comp_detector_start[],comp_detector_end[];
extern void comp_peak(void),comp_hp(void),comp_curve(void),comp_scale(void),comp_sync(void);
void (*comp_peak_entry)(void) DATA=comp_peak;
void (*comp_hp_entry)(void) DATA=comp_hp;
void (*comp_curve_entry)(void) DATA=comp_curve;
void (*comp_scale_entry)(void) DATA=comp_scale;
static u32 code_placed DATA=0,code_ticks DATA=0;
_Static_assert(sizeof(struct comp_runtime)+8==ST_SRAM_COMP_END-ST_SRAM_COMP_STATE,"COMP SRAM boundary");
static void *moved(void (*f)(void))
{
    u32 a=(u32)f,b=(u32)comp_detector_start;
    __asm__("" : "+d"(a),"+d"(b));return (void *)(a-b+ST_SRAM_COMP_CODE);
}
static u32 place_code(void)
{
    volatile u8 *to=(volatile u8 *)ST_SRAM_COMP_CODE;
    u32 i,n=(u32)comp_detector_end-(u32)comp_detector_start,sr;
    if(code_placed)return 2;
    if(n>ST_SRAM_COMP_STATE-ST_SRAM_COMP_CODE)return 0;
    for(i=0;i<ST_SRAM_COMP_STATE-ST_SRAM_COMP_CODE;i++)if(to[i])return 0;
    for(i=0;i<n;i++)to[i]=comp_detector_start[i];
    for(i=0;i<n;i++)if(to[i]!=comp_detector_start[i])return 0;
    sr=comp_lock();comp_sync();
    comp_peak_entry=moved(comp_peak);comp_hp_entry=moved(comp_hp);comp_curve_entry=moved(comp_curve);comp_scale_entry=moved(comp_scale);
    code_placed=1;comp_unlock(sr);return 2;
}
static u32 place_state(void)
{
    volatile u32 *dst=(volatile u32 *)ST_SRAM_COMP_STATE;
    const u32 *src=(const u32 *)&comp_ddr_runtime;u32 i,sr;
    if(comp_hot!=&comp_ddr_runtime)return 1;
    for(i=0;i<WORDS+2;i++)if(dst[i])return 0;
    sr=comp_lock();
    for(i=0;i<WORDS;i++)dst[i]=src[i];
    for(i=0;i<WORDS;i++)if(dst[i]!=src[i]){comp_unlock(sr);return 0;}
    dst[WORDS]=GUARD0;dst[WORDS+1]=GUARD1;
    __asm__ volatile("" ::: "memory");
    comp_hot=(struct comp_runtime *)ST_SRAM_COMP_STATE;
    comp_unlock(sr);return 1;
}
u32 comp_place(void)
{u32 code=place_code();return code|place_state();}
void comp_check_state(void)
{
    const volatile u32 *src=(const volatile u32 *)ST_SRAM_COMP_STATE;
    u32 *dst=(u32 *)&comp_ddr_runtime;u32 i,sr;
    if(code_placed && ++code_ticks>=60){
        const volatile u8 *code=(const volatile u8 *)ST_SRAM_COMP_CODE;
        u32 n=(u32)comp_detector_end-(u32)comp_detector_start;
        code_ticks=0;
        for(i=0;i<n && code[i]==comp_detector_start[i];i++);
        if(i!=n){
            sr=comp_lock();comp_peak_entry=comp_peak;comp_hp_entry=comp_hp;comp_curve_entry=comp_curve;comp_scale_entry=comp_scale;
            code_placed=0;comp_unlock(sr);
        }
    }
    if(comp_hot==&comp_ddr_runtime || (src[WORDS]==GUARD0 && src[WORDS+1]==GUARD1))return;
    /* A later conflict never makes audio depend on a reinstalled copy.
     * Preserve envelope/Kit/config history and permanently fall back to DDR. */
    sr=comp_lock();for(i=0;i<WORDS;i++)dst[i]=src[i];
    __asm__ volatile("" ::: "memory");comp_hot=&comp_ddr_runtime;comp_unlock(sr);
}
