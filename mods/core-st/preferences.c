/* SPDX-License-Identifier: GPL-2.0-or-later
 * Global module preferences in the two already-reserved bank-slot spare
 * sectors. Payload ends at SLOT_CAP; independent bank metadata follows this
 * spare sector. Neither payload capacity nor bank metadata is changed.
 * Task-only, redundant sequence+CRC records, verify before reporting success. */
#include <syntakt/storage.h>
#include "storage.h"
#include "cold.h"
typedef unsigned int u32;typedef unsigned char u8;
extern u32 core_crc_update(u32,const u8 *,u32);
static u32 mutex[4];
#define MAGIC 0x53595052u
#define PREF_LBA(s) ((CORE_MODS_SLOT_META(s)-512u)>>9)
_Static_assert(CORE_MODS_BASE+CORE_MODS_SLOT_CAP==CORE_MODS_SLOT_META(0)-512u,"preferences outside bank payload");
static u32 get(const u8 *p){return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static void put(u8 *p,u32 v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static int valid(const u8 *p)
{
    u32 i;
    if(get(p)!=MAGIC || get(p+4)!=1 || get(p+12)!=8 ||
       get(p+508)!=(core_crc_update(~0u,p,508)^~0u))return 0;
    for(i=0;i<8;i++)if(get(p+16+36*i)>8)return 0;
    return 1;
}
int core_preferences_io(const struct st_storage_io *io,u32 key,u32 *values,u32 count,u32 write)
{
    u8 b[2][512] __attribute__((aligned(16)));u32 ok[2],i,best=0,seq=0,target,at;
    if(!io || !io->read || !io->sectors || key<1 || key>8 || !values || !count || count>8)return -1;
    if(io->sectors()==~0u || io->sectors()<=CORE_MODS_LBA)return -1;
    /* A historical opaque /mods file may occupy the whole legacy window.
     * Preserve it if its committed length overlaps either spare sector. */
    if(io->read(CORE_MODS_LBA,512,b[0]))return -1;
    if(get(b[0])==0x454c4d4fu && get(b[0]+4)==0x44533031u && get(b[0]+8)==1 &&
       get(b[0]+16)==~get(b[0]+12) && get(b[0]+20)==(core_crc_update(~0u,b[0],20)^~0u) &&
       get(b[0]+12)>CORE_MODS_SLOT_CAP)return -1;
    for(i=0;i<2;i++){
        if(io->read(PREF_LBA(i),512,b[i]))return -1;
        ok[i]=valid(b[i]);
    }
    if(ok[1] && (!ok[0] || (int)(get(b[1]+8)-get(b[0]+8))>0))best=1;
    at=16+36*(key-1);
    if(!write){
        if(!ok[best] || get(b[best]+at)!=count)return 0;
        for(i=0;i<count;i++)values[i]=get(b[best]+at+4+4*i);
        return 1;
    }
    if(!io->write)return -1;
    if(ok[best]){
        seq=get(b[best]+8);
        if(get(b[best]+at)==count){
            for(i=0;i<count;i++)if(get(b[best]+at+4+4*i)!=values[i])break;
            if(i==count)return 1;
        }
    }else for(i=0;i<512;i++)b[best][i]=0;
    put(b[best],MAGIC);put(b[best]+4,1);put(b[best]+8,seq+1);put(b[best]+12,8);
    put(b[best]+at,count);
    for(i=0;i<8;i++)put(b[best]+at+4+4*i,i<count?values[i]:0);
    put(b[best]+508,core_crc_update(~0u,b[best],508)^~0u);
    target=best^1;
    if(io->write(PREF_LBA(target),512,b[best]) || io->read(PREF_LBA(target),512,b[target]) ||
       !valid(b[target]))return -1;
    for(i=0;i<512;i++)if(b[best][i]!=b[target][i])return -1;
    return 1;
}
CORE_HOT int core_preferences(u32 key,u32 *values,u32 count,u32 write)
{
    int r;
#ifdef __mcoldfire__
    u32 sr;__asm__ volatile("move.w %%sr,%0":"=d"(sr));if(sr&0x700u)return -1;
#endif
    ((void (*)(void *))0x40001980u)(mutex);
    r=core_preferences_io(&core_hzfs_native_io,key,values,count,write);
    ((void (*)(void *))0x40001aa2u)(mutex);
    return r;
}
