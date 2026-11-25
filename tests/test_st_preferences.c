/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <syntakt/storage.h>
#include "../mods/core-st/storage.h"
typedef unsigned int u32;
extern int core_preferences_io(const struct st_storage_io *,u32,u32 *,u32,u32);
const struct st_storage_io core_hzfs_native_io={0};
static unsigned char card[2][512],legacy[512];static u32 writes,last;static int torn,fail_read;
extern u32 core_crc_update(u32,const unsigned char *,u32);
static u32 slot(u32 lba)
{
    u32 a=(CORE_MODS_SLOT_META(0)-512u)>>9,b=(CORE_MODS_SLOT_META(1)-512u)>>9;
    assert(lba==a || lba==b);return lba==b;
}
static int read_card(u32 lba,u32 n,void *p){assert(n==512);if(fail_read)return -1;memcpy(p,lba==CORE_MODS_LBA?legacy:card[slot(lba)],512);return 0;}
static void put(unsigned char *p,u32 v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static int write_card(u32 lba,u32 n,const void *p)
{assert(n==512);last=slot(lba);writes++;memcpy(card[last],p,torn?100:512);return 0;}
static u32 sectors(void){return 0x400000u;}
static const struct st_storage_io io={read_card,write_card,sectors,0,0};
int main(void)
{
    u32 rec[6]={5,8,14,1,1,0},bank[3]={1,27,27},got[8]={99},before;
    memset(card,255,sizeof card);
    assert(!core_preferences_io(&io,1,got,6,0) && got[0]==99 && !writes);
    assert(core_preferences_io(&io,1,rec,6,1)==1 && writes==1);
    assert(core_preferences_io(&io,1,got,6,0)==1 && !memcmp(rec,got,24));
    assert(core_preferences_io(&io,1,rec,6,1)==1 && writes==1);  /* no wear for unchanged values */
    assert(core_preferences_io(&io,2,bank,3,1)==1 && writes==2);
    assert(core_preferences_io(&io,1,got,6,0)==1 && !memcmp(rec,got,24));
    assert(core_preferences_io(&io,2,got,3,0)==1 && !memcmp(bank,got,12));
    put(legacy,0x454c4d4fu);put(legacy+4,0x44533031u);put(legacy+8,1);
    put(legacy+12,CORE_MODS_MAX);put(legacy+16,~CORE_MODS_MAX);
    put(legacy+20,core_crc_update(~0u,legacy,20)^~0u);before=writes;
    assert(core_preferences_io(&io,1,rec,6,1)==-1 && writes==before);
    memset(legacy,0,sizeof legacy);
    torn=1;rec[0]=2;
    assert(core_preferences_io(&io,1,rec,6,1)==-1);torn=0;
    assert(core_preferences_io(&io,1,got,6,0)==1 && got[0]==5); /* previous committed copy */
    assert(core_preferences_io(&io,1,rec,6,1)==1);
    card[last][508]^=1;
    assert(core_preferences_io(&io,1,got,6,0)==1 && got[0]==5);
    before=writes;fail_read=1;
    assert(core_preferences_io(&io,2,bank,3,1)==-1 && writes==before);fail_read=0;
    assert(core_preferences_io(&io,0,got,1,1)==-1 && writes==before);
    assert(core_preferences_io(&io,9,got,1,1)==-1 && writes==before);
    assert(core_preferences_io(&io,1,got,9,1)==-1 && writes==before);
    assert(!core_preferences_io(&io,2,got,2,0));
    assert(core_preferences_io(&io,1,rec,6,1)==1);
    assert(core_preferences_io(&io,2,got,3,0)==1 && !memcmp(bank,got,12));
    puts("PASS preferences: independent keys, fresh RAM reads, no-change writes, redundant CRC/sequence records, torn/corrupt write recovery, IO errors, bounds, bank metadata/payload never touched");
    return 0;
}
