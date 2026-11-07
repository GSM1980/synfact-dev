/* SPDX-License-Identifier: GPL-2.0-or-later
 * Read-only stock-code gate for the two audited modules with direct hooks.
 * Keep this before ANY bank initializer. The table is generated from their
 * own preimage declarations, and is checked for drift by the regressions.
 */
#include "boot-preimages.h"
typedef core_u32 u32;typedef core_u8 u8;
#include "boot-preimages.inc"
#ifdef CORE_ST_NATIVE_TEST
extern u8 core_boot_byte(u32);
#else
static u8 core_boot_byte(u32 at){return *(const volatile u8 *)at;}
#endif
static int name_is(const u8 *p,const char *name)
{
    u32 i=0;while(name[i]){if(i>=24 || p[i]!=(u8)name[i])return 0;i++;}
    return i<24 && !p[i];
}
static int check(const u8 *table,u32 count,u32 expected)
{
    u32 k,i,c=0xffffffffu,at,n;
    for(k=0;k<count;k++,table+=4){
        at=0x40000000u+((u32)table[0]<<16|(u32)table[1]<<8|table[2]);n=table[3];
        for(i=0;i<n;i++){
            u32 j;c^=core_boot_byte(at+i);
            for(j=0;j<8;j++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);
        }
    }
    return ~c==expected;
}
int core_boot_preimages_ok(const u8 *info)
{
    u32 i,lfo3=0,skip=0;
    if(!info)return 0;
    for(i=0;i<16 && info[40*i];i++){
        if(name_is(info+40*i,"lfo3-st"))lfo3=1;
        if(name_is(info+40*i,"modulation-skip-st"))skip=1;
    }
    return (!lfo3 || check(lfo3_preimages,LFO3_PREIMAGES,LFO3_PREIMAGE_CRC)) &&
           (!skip || check(modulation_skip_preimages,MODULATION_SKIP_PREIMAGES,MODULATION_SKIP_PREIMAGE_CRC));
}
