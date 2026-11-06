/* SPDX-License-Identifier: GPL-2.0-or-later
 * Full Q8 chorus settings, stored inline in the Kit's linear FX record.
 * The power-off path (400bcf02) dumps the project RAM image raw and the
 * boot path restores it without the FX codecs, so the record must hold the
 * values themselves; a handle into file-owned RAM did not survive a reboot.
 * Seven 1.41-unused FX words carry one value each (0x8000|Q8, bit 15 clear =
 * default); word 0 is the layout marker (0x4348 in the earliest chorus).
 * Each value is one aligned word, so
 * the IRQ never sees a partially updated setting. Copies, undo and the
 * MRAM dump copy plain values.
 */
typedef unsigned int u32;
typedef int s32;
typedef unsigned short u16;
typedef unsigned char u8;
#include "ids.h"
#define FIRST CHORUS_ID
#define MARK 0x4332u
#define VALID 0x8000u
static const u16 defaults[7]={0x7f00,0x2000,0,0x7f00,0,0,0x6400};
/* Working words 1a/24/27/29/2a/37/46: unused by 1.41 (docs/EMMC_STORAGE_1.41
 * section 6). Native publication 400a1d18 only copies/converts each into its
 * own entry; 0x38 is avoided because it starts a native copy segment. 0x1a/0x37
 * serialize only into raw words the chorus save codec overwrites, 0x46 is not
 * serialized, 24/27/29/2a round-trip and are rewritten by chorus_params_load.
 */
static const u8 slot[7]={0x1a,0x24,0x27,0x29,0x2a,0x37,0x46};
#define DATA __attribute__((section(".data")))
/* Also hot.c's, when the Kit's record or one of its words changed. */
void chorus_decode(const u16 *fx,u16 *out)
{
    const volatile u16 *source=fx;
    u32 i,word;
    u32 marked=source && source[0]==MARK;
    for (i=0;i<7;i++) {
        word=marked?source[slot[i]]:0;
        out[i]=(word&VALID) && (word&0x7fffu)<=0x7f00u?(u16)(word&0x7fffu):defaults[i];
    }
}
static void store(u16 *fx,const u16 *values)
{
    volatile u16 *dest=fx;
    u32 i;
    if (dest[0]!=MARK) {
        /* Values first, marker last: until then readers see defaults. */
        for (i=0;i<7;i++) dest[slot[i]]=(u16)(VALID|values[i]);
        __asm__ volatile ("" ::: "memory");
        dest[0]=MARK;
        return;
    }
    for (i=0;i<7;i++) dest[slot[i]]=(u16)(VALID|values[i]);
}
static u16 *model_fx(void *model)
{
    /* The native global-parameter model is a compact GlobalParams object,
     * not a Sound with a 0x62-byte header. Its bytes must not hold our tags.
     * Native 400a342c publishes the current Kit at 800030b8 and its linear
     * 71-word FX record at kit+0xdce. The latter is what the FX codecs own.
     */
    u8 *kit=*(u8 *volatile *)0x800030b8u;
    (void)model;
    return kit?(u16 *)(kit+0xdce):*(u16 *volatile *)0x800030bcu;
}
s32 chorus_param_get(void *model,u32 id)
{
    u16 values[7];
    if (id<FIRST || id>=FIRST+7) return 0;
    chorus_decode(model_fx(model),values);
    return values[id-FIRST];
}
s32 chorus_param_set(void *model,u32 id,s32 raw)
{
    u16 values[7],*fx=model_fx(model);
    if (!fx || id<FIRST || id>=FIRST+7) return -1;
    if (raw<0) raw=0;
    if (raw>0x7f00) raw=0x7f00;
    chorus_decode(fx,values);
    if (values[id-FIRST]==(u16)raw && fx[0]==MARK) return 0;
    values[id-FIRST]=(u16)raw;
    store(fx,values);
    {
        /* A playing FX lock may use a copy of the Kit record. Commit the
         * same values there for immediate audible edits, while retaining
         * the Kit copy for subsequent ticks, saving and the MRAM dump.
         */
        u16 *playing=*(u16 *volatile *)0x800030bcu;
        if(playing && playing!=fx) store(playing,values);
    }
    {
        u8 *ui=*(u8 *volatile *)0x444e1334u;
        if (ui) ui[96]=1;
    }
    return 0;
}
/* FX v2 has 69 serialized words, three unused words (4,6c,8c),
 * then eight explicitly zeroed extension bytes (94..9b). The seven
 * settings fit exactly, with one tag bit per word. Geometry stays 9c.
 * The serialized layout of every earlier Synfact chorus.
 */
static const u8 serialized[7]={4,0x6c,0x8c,0x94,0x96,0x98,0x9a};
static const u8 tag[7]={1,0,1,0,0,1,1};
void chorus_params_save(u8 *raw,const u16 *fx)
{
    u16 v[7];
    u32 i;
    chorus_decode(fx,v);
    for (i=0;i<7;i++) *(u16 *)(raw+serialized[i])=(u16)(v[i]|((u32)tag[i]<<15));
}
void chorus_params_load(u16 *fx,const u8 *raw)
{
    volatile u16 *dest=fx;
    u16 v[7];u32 i;
    for (i=0;i<7;i++) {
        u32 word=*(const u16 *)(raw+serialized[i]);
        if ((word>>15)!=tag[i] || (word&32767u)>0x7f00u) break;
        v[i]=(u16)(word&32767u);
    }
    if (i==7) {
        dest[0]=0;
        store(fx,v);
        return;
    }
    /* Legacy/invalid: defaults, and no native leftovers in the value words. */
    dest[0]=0;
    for (i=0;i<7;i++) dest[slot[i]]=0;
}
