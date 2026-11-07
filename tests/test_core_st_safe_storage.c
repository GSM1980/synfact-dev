/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fault-injected v2 metadata selection. No device IO or bank code executes. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/core-st/storage.h"
static unsigned char sectors[3][512];
static int fail_write=-1,fail_read=-1;
static int index_of(unsigned int lba)
{
    if(lba==CORE_MODS_SLOT_META(0)>>9)return 0;
    if(lba==CORE_MODS_SLOT_META(1)>>9)return 1;
    if(lba==CORE_MODS_LBA)return 2;
    assert(0);return -1;
}
static int read_sector(unsigned int lba,unsigned int n,void *out)
{
    int i=index_of(lba);assert(n==512);
    if(i==fail_read)return -1;
    memcpy(out,sectors[i],512);return 0;
}
static int write_sector(unsigned int lba,unsigned int n,const void *in)
{
    int i=index_of(lba);assert(n==512);
    if(i==fail_write)return -1;
    memcpy(sectors[i],in,512);return 0;
}
static unsigned int card_sectors(void){return CORE_MODS_LBA+1;}
static const struct core_mods_io io={read_sector,write_sector,card_sectors};
int main(void)
{
    struct core_mods_slot a,b,primary,fallback;struct core_mods_store legacy={0};
    unsigned int n;
    assert(core_mods_select_io(&io,&primary,&fallback)==0);
    assert(core_mods_stage_io(&io,&a) && a.slot==0 && a.sequence==1);
    a.bytes=4096;a.stamp=0x12345678;
    fail_write=0;assert(!core_mods_record_io(&io,&a));fail_write=-1;
    assert(core_mods_select_io(&io,&primary,&fallback)==0);
    assert(core_mods_record_io(&io,&a));
    assert(core_mods_select_io(&io,&primary,&fallback)==1 && primary.slot==0 &&
           primary.state==CORE_MODS_CANDIDATE && !fallback.bytes);
    assert(core_mods_transition_io(&io,0,CORE_MODS_CANDIDATE,CORE_MODS_TRYING));
    assert(core_mods_select_io(&io,&primary,&fallback)==0); /* crash before confirmation */
    assert(core_mods_transition_io(&io,0,CORE_MODS_TRYING,CORE_MODS_GOOD));
    assert(core_mods_select_io(&io,&primary,&fallback)==1 && primary.slot==0);
    assert(core_mods_stage_io(&io,&b) && b.slot==1 && b.sequence==2);
    b.bytes=8192;b.stamp=0xabcdef01;
    fail_write=1;assert(!core_mods_record_io(&io,&b));fail_write=-1;
    assert(core_mods_select_io(&io,&primary,&fallback)==1 && primary.slot==0);
    assert(core_mods_record_io(&io,&b));
    assert(core_mods_select_io(&io,&primary,&fallback)==1 && primary.slot==1 &&
           fallback.slot==0 && fallback.bytes==4096);
    assert(core_mods_transition_io(&io,1,CORE_MODS_CANDIDATE,CORE_MODS_TRYING));
    assert(core_mods_select_io(&io,&primary,&fallback)==1 && primary.slot==0);
    assert(core_mods_transition_io(&io,1,CORE_MODS_TRYING,CORE_MODS_GOOD));
    assert(core_mods_select_io(&io,&primary,&fallback)==1 && primary.slot==1);
    assert(core_mods_stage_io(&io,&a) && a.slot==0 && a.sequence==3);
    assert(core_mods_invalidate_slot_io(&io,0));
    assert(core_mods_select_io(&io,&primary,&fallback)==1 && primary.slot==1);
    sectors[1][16]^=1;assert(core_mods_select_io(&io,&primary,&fallback)==0);
    fail_read=1;assert(core_mods_stage_io(&io,&a)==0);fail_read=-1;
    memset(sectors,0,sizeof(sectors));
    assert(core_mods_begin_io(&legacy,&io,256));
    assert(core_mods_commit_io(&legacy,&io,256));
    assert(core_mods_stat_io(&io,&n)==1 && n==256);
    assert(core_mods_stage_io(&io,&a) && a.slot==1); /* preserve legacy slot 0 */
    puts("PASS: v2 stage, trial, confirmation, rollback, corruption, failed writes and legacy migration");
    return 0;
}
