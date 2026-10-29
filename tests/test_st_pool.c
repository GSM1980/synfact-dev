/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real MAIN pool C with sparse sector IO and explicit native RAM buffers.
 * No firmware routines or target instructions are executed.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void receiver_sleep(unsigned int);
#define CORE_POOL_SLEEP receiver_sleep
#include "../mods/core-st/sample-pool.c"
int (*core_parameter_provider)(unsigned int,unsigned int *,void *,unsigned int *);   /* machines.c (host API 7), none here */
#include "../mods/core-st/sample-wave.c"
static u32 receiver_table[256],receiver_stock[128],receiver_bell,receiver_calls;
#define TABLE receiver_table
#define STOCK_TABLE receiver_stock
#define WIN ((volatile u32 *)0x10000800u)
#define DOORBELL receiver_bell
#include "../mods/core-st/audio-pool.c"
static void receiver_sleep(u32 delay)
{assert(delay==500);receiver_calls++;audio_pool_receive();}
static unsigned char disk[3][0x80000],sample[64+2200];
static unsigned char project_sector[512];
static u32 io_fail,live,id,allocations;
const unsigned char core_pool_image[4096]={0x45,0x4c,0x50,0x31,0,0,0x10,0};
u32 fw_mmc_ready[]={1},fw_mmc_sectors[]={0x3a4000},core_machine_pages;
void *fw_malloc(u32 n)
{
    void *p=VirtualAlloc((void *)(0x47000000u+0x100000u*allocations++),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(p)live++;return p;
}
void fw_free(void *p){assert(VirtualFree(p,0,MEM_RELEASE));live--;}
int fw_mmc_read(u32 lba,u32 n,void *p)
{
    if(lba==0x80000u && n==512){memcpy(p,project_sector,512);return 0;}
    u32 at=(lba-(0x600u<<10))*512;assert(lba>=(0x600u<<10) && at+n<=sizeof(disk));
    if(io_fail)return -1;memcpy(p,(u8 *)disk+at,n);return 0;
}
static int write_sector(u32 lba,u32 n,const void *p)
{u32 at=(lba-(0x600u<<10))*512;assert(at+n<=sizeof(disk));memcpy((u8 *)disk+at,p,n);return 0;}
static u32 sectors(void){return fw_mmc_sectors[0];}
static void *allocate_mem(u32 n){return malloc(n);}
const struct st_storage_io core_hzfs_native_io={fw_mmc_read,write_sector,sectors,allocate_mem,free};
void core_pool_ui_progress(u32 d,u32 t){(void)d;(void)t;assert(0);}
void core_pool_ui_file(u32 code,u32 ordinal,u32 n){(void)code;(void)ordinal;(void)n;assert(0);}
static int sample_source(void *ctx,u32 at,u32 n,void *dst)
{(void)ctx;memcpy(dst,sample+at,n);return 0;}
static void reset_state(void)
{if(state)fw_free(state);state=0;core_pool_boot_pages=0;core_pool_busy=0;assert(live==(saved_record!=0)+(waves!=0));}
int main(void)
{
    u16 entries[128];u8 object[516],source_project[512],copy_project[512],*blob;u32 i,pos,wire;
    const u8 *page;void *project_ram,*blob_ram;
    project_ram=VirtualAlloc((void *)0x40650000u,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    blob_ram=VirtualAlloc((void *)0x4d840000u,0x30000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(project_ram==(void *)0x40650000u && blob_ram==(void *)0x4d840000u);
    assert(VirtualAlloc((void *)0x10000000u,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)0x10000000u);
    assert(VirtualAlloc((void *)0x43bd0000u,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)0x43bd0000u);
    memset(source_project,0xa5,512);memset(copy_project,0,512);
    core_pool_read(source_project,entries);for(i=0;i<128;i++)assert(entries[i]==(i<64?i+1:0));
    core_pool_write(source_project,entries);assert(core_pool_valid(source_project));
    entries[64]=0x8001;core_pool_write(source_project,entries);assert(core_pool_valid(source_project));
    core_pool_before_validate(source_project);memset(source_project+0xf0,0,0x110);
    core_pool_after_validate(source_project,1);core_pool_read(source_project,entries);assert(entries[64]==0x8001);
    core_pool_before_validate(source_project);memset(source_project+0xf0,0,0x110);
    core_pool_after_validate(source_project,0);assert(!core_pool_valid(source_project));
    core_pool_write(source_project,entries);memset(object,0,sizeof(object));
    core_pool_after_snapshot(object,source_project);assert(!memcmp(object+4+0xf0,source_project+0xf0,0x110));
    core_pool_empty(copy_project);assert(core_pool_valid(copy_project));core_pool_read(copy_project,entries);
    for(i=0;i<128;i++)assert(entries[i]==(i<64?i+1:0));
    /* A successful native load/conversion with no pool creates stock defaults. */
    memset(copy_project,0,sizeof(copy_project));core_pool_before_validate(copy_project);
    core_pool_after_validate(copy_project,1);assert(core_pool_valid(copy_project));
    core_pool_read(copy_project,entries);for(i=0;i<128;i++)assert(entries[i]==(i<64?i+1:0));
    /* Valid legacy pools retain arbitrary previous assignments, without migration. */
    entries[0]=0x8002;entries[1]=0;core_pool_write(copy_project,entries);
    core_pool_before_validate(copy_project);memset(copy_project+0xf0,0,0x110);
    core_pool_after_validate(copy_project,1);core_pool_read(copy_project,entries);
    assert(entries[0]==0x8002 && !entries[1]);
    /* Successful native project load, no record: create the default in working RAM. */
    memset(project_sector,0,512);wr(project_sector,0xbeefbaceu);
    core_pool_after_load(0,1);assert(core_pool_valid(PROJECT));core_pool_read(PROJECT,entries);
    for(i=0;i<128;i++)assert(entries[i]==(i<64?i+1:0));
    /* Existing empty/legacy records also survive the actual load path unchanged. */
    entries[0]=0x8002;entries[1]=0;core_pool_write(project_sector,entries);
    core_pool_after_load(0,1);core_pool_read(PROJECT,entries);assert(entries[0]==0x8002 && !entries[1]);
    /* NEW PROJECT must overwrite a VALID prior pool, unlike LOAD PROJECT. */
    for(i=0;i<64;i++)entries[i]=64-i;
    entries[64]=0x8001;entries[127]=0x8002;core_pool_write(PROJECT,entries);
    core_pool_created();assert(core_pool_valid(PROJECT));core_pool_read(PROJECT,entries);
    for(i=0;i<128;i++)assert(entries[i]==(i<64?i+1:0));
    /* The resulting project snapshot, not the old record, is saved/reloaded. */
    core_pool_after_snapshot(object,PROJECT);assert(!memcmp(object+4+0xf0,PROJECT+0xf0,0x110));
    memcpy(project_sector,object+4,512);wr(project_sector,0xbeefbaceu);
    core_pool_after_load(0,1);core_pool_read(PROJECT,entries);
    for(i=0;i<128;i++)assert(entries[i]==(i<64?i+1:0));
    source_project[0x101]^=1;assert(!core_pool_valid(source_project));
    /* Create one >1-page sample through the real HZFS writer. */
    assert(core_hzfs_ensure(&core_hzfs_native_io)==1);
    wr(sample,0x53414d50);wr(sample+8,1100);
    for(i=64;i<sizeof(sample);i++)sample[i]=(u8)i;
    assert(core_hzfs_put(&core_hzfs_native_io,"long",sizeof(sample),0,sample_source,0,&id)==1 && id==1);
    blob=(u8 *)STOCK_BLOB;pos=0;
    for(i=0;i<64;i++){wr(blob+pos,4);pos+=4;memset(blob+pos,(int)i,8);pos+=8;}
    *(u32 *)0x4d84f2c1u=pos;
    memset(PROJECT,0,512);core_pool_read(PROJECT,entries);entries[64]=0x8001;core_pool_write(PROJECT,entries);
    core_pool_prepare();assert(core_pool_boot_pages==4 && state && state->files==1);
    page=core_pool_boot_page(0,&wire);assert(wire==0x4164 && !memcmp(page,sample+64,2048));
    assert(core_pool_boot_page(0,&wire)==page); /* sender retry */
    page=core_pool_boot_page(1,&wire);assert(wire==0x4165 && !memcmp(page,sample+64+2048,152));
    for(i=152;i<2048;i++)assert(!page[i]);
    page=core_pool_boot_page(2,&wire);assert(wire==0x4162 && rd(page)==IMAGE_MAGIC);
    assert(rd(page+8)==adler(state->image+12,4096-12));
    page=core_pool_boot_page(3,&wire);assert(wire==0x4163);
    assert(rd(state->image+0xc00+64*8)==1100 && rd(state->image+0xc04+64*8)==CORE_POOL_PCM);
    assert(state->frames[0]==4 && state->pointer[0]==STOCK_AUDIO+4);
    assert(core_pool_report[2]==1);
    /* Actual MAIN sync and AUDIO receiver, with a native-host mailbox driver:
     * replacing a valid reversed pool must publish all 64 identity descriptors
     * and retire the prior long sample immediately, without reboot/reload. */
    for(i=0;i<64;i++){
        entries[i]=64-i;state->code[i]=64-i;
        state->frames[i]=state->stock_frames[63-i];state->pointer[i]=state->stock_pointer[63-i];
        receiver_stock[2*i]=state->stock_frames[i];receiver_stock[2*i+1]=state->stock_pointer[i];
    }
    entries[64]=0x8001;core_pool_write(PROJECT,entries);
    for(i=0;i<128;i++){receiver_table[2*i]=state->frames[i];receiver_table[2*i+1]=state->pointer[i];}
    *(volatile u32 *)0x43bdc824u=1;core_pool_created();assert(receiver_calls);
    core_pool_read(PROJECT,entries);
    for(i=0;i<128;i++){
        assert(entries[i]==(i<64?i+1:0));assert(state->code[i]==entries[i]);
        if(i<64){assert(receiver_table[2*i]==state->stock_frames[i]);assert(receiver_table[2*i+1]==state->stock_pointer[i]);}
        else assert(!receiver_table[2*i] && !state->frames[i]);
    }
    reset_state();
    /* IO corruption never publishes the failed PCM descriptor. */
    core_pool_prepare();disk[2][100]^=1;
    core_pool_boot_page(0,&wire);core_pool_boot_page(1,&wire);core_pool_boot_page(2,&wire);
    assert(!rd(state->image+0xc00+64*8));disk[2][100]^=1;reset_state();
    /* A missing catalogue still preserves every native stock source. */
    io_fail=1;core_pool_prepare();io_fail=0;assert(core_pool_boot_pages==2);
    core_pool_boot_page(0,&wire);assert(state->frames[0]==4 && !state->frames[64]);reset_state();
    if(waves)fw_free(waves);if(saved_record)fw_free(saved_record);assert(!live);
    assert(VirtualFree(project_ram,0,MEM_RELEASE));assert(VirtualFree(blob_ram,0,MEM_RELEASE));
    assert(VirtualFree((void *)0x10000000u,0,MEM_RELEASE));assert(VirtualFree((void *)0x43bd0000u,0,MEM_RELEASE));
    puts("PASS: project pool codecs/migration, snapshot/validation, stock preservation, boot page mapping, full long-sample CRC and shared transfer");return 0;
}
