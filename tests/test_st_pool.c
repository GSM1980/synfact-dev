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
int core_parameter_provider(unsigned int q,unsigned int *a,void *r,unsigned int *v)   /* machines.c (host API 7), none here */
{(void)q;(void)a;(void)r;(void)v;return 0;}
#include "../mods/core-st/sample-wave.c"
static u32 receiver_table[256],receiver_stock[128],receiver_bell,receiver_calls;
static unsigned char receiver_channels[128];
#define CHANNELS receiver_channels
#define TABLE receiver_table
#define STOCK_TABLE receiver_stock
#define WIN ((volatile u32 *)0x10000800u)
#define DOORBELL receiver_bell
#include "../mods/core-st/audio-pool.c"
static void receiver_sleep(u32 delay)
{assert(delay==500);receiver_calls++;audio_pool_receive();}
static unsigned char disk[4][0x80000],sample[64+2200];
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
    const u8 *page;const struct st_machine_wave *wave;void *project_ram,*blob_ram;
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
    /* The visible stock wave gets a whole-sample overview immediately, then
     * the bounded idle scan refines peaks missed by the sparse pass. */
    state=fw_malloc(sizeof(*state));assert(state);memset(state,0,sizeof(*state));
    state->code[0]=1;state->frames[0]=state->stock_frames[0]=5000;
    state->stock_pointer[0]=STOCK_AUDIO+0x1000;
    memset(blob+0x1000,0,10000);
    blob[0x1000+2*4997]=0x04;blob[0x1000+2*4997+1]=0xd2;
    blob[0x1000+2*4999]=0x4e;blob[0x1000+2*4999+1]=0x20;
    core_pool_wave_idle(0,64);wave=core_pool_wave(0);
    assert(wave && wave->ready && wave->frames==5000 && wave->hi[95]==1234);
    state->code[1]=2;state->frames[1]=state->stock_frames[1]=4;
    state->stock_pointer[1]=STOCK_AUDIO+0x3000;
    memset(blob+0x3000,0,8);
    core_pool_wave_idle(1,64); /* interrupt the first sample's exact scan */
    for(i=0;i<80;i++)core_pool_wave_idle(0,64);
    assert(wave->ready==2 && wave->hi[95]==20000);
    blob[0x1000+2*4997]=blob[0x1000+2*4997+1]=0;
    blob[0x1000+2*4999]=blob[0x1000+2*4999+1]=0;
    core_pool_wave_begin(1,5000,1);core_pool_wave_idle(0,64);
    assert(wave->ready==1 && wave->hi[95]==0); /* same code, new PCM */
    state->frames[0]=state->stock_frames[0]=4;
    core_pool_wave_idle(0,64);wave=core_pool_wave(0);
    assert(wave && wave->ready && wave->frames==4);
    reset_state();
    /* Two long samples loaded at boot both keep their waves. Before core 1.8
     * the second evicted the first: no slot's frames are published until the
     * boot load ends, so the cache took the first wave for unused. */
    assert(core_hzfs_put(&core_hzfs_native_io,"two",sizeof(sample),0,sample_source,0,&id)==1 && id==2);
    memset(PROJECT,0,512);core_pool_read(PROJECT,entries);entries[64]=0x8001;entries[65]=0x8002;
    core_pool_write(PROJECT,entries);core_pool_prepare();assert(state && state->files==2);
    for(i=0;i<core_pool_boot_pages;i++)core_pool_boot_page(i,&wire);
    {const struct st_machine_wave *a=core_pool_wave(64),*b=core_pool_wave(65);
     assert(a && b && a!=b && a->ready==2 && b->ready==2 && a->code==0x8001 && b->code==0x8002);}
    reset_state();
    /* Core 1.8: a stereo entry (flag ST_HZ_STEREO, interleaved L, R; frames
     * still counts samples) boots as frame pairs with channel count 2 in the
     * image's channel table; mono slots keep 1. */
    for(i=0;i<2;i++)if(rd(disk[i])==0x485a4653u){
        u8 *e=disk[i]+ST_HZ_ENTRIES;u32 n=rd(disk[i]+12),k,len=rd(disk[i]+36);
        for(k=0;k<n;k++,e+=64)if(rd(e)==1)wr(e+28,ST_HZ_STEREO);
        wr(disk[i]+32,0);wr(disk[i]+32,~core_crc_update(0,disk[i],len));
    }
    memset(PROJECT,0,512);core_pool_read(PROJECT,entries);entries[64]=0x8001;core_pool_write(PROJECT,entries);
    core_pool_prepare();assert(state && state->files==1 && state->file[0].channels==2);
    core_pool_boot_page(0,&wire);core_pool_boot_page(1,&wire);core_pool_boot_page(2,&wire);
    assert(rd(state->image+0xc00+64*8)==550 && rd(state->image+0xc04+64*8)==CORE_POOL_PCM);
    assert(state->image[0xb80+64]==2 && state->image[0xb80]==1 && state->channels[64]==2);
    wave=core_pool_wave(64);assert(wave && wave->frames==550 && wave->ready==2 && wave->channels==2);
    assert(core_pool_sample_channels(64)==2 && core_pool_sample_channels(0)==1 && core_pool_sample_channels(65)==1);
    /* L (even bytes 64+4k..) and R keep their own peaks: the pattern is i&255,
     * so in bin 0 L's samples start 0x4041 and R's 0x4243. */
    {short l=(short)(sample[64]<<8|sample[65]),r=(short)(sample[66]<<8|sample[67]);
     assert(wave->lo[0]<=l && wave->hi[0]>=l && wave->lo_r[0]<=r && wave->hi_r[0]>=r);
     assert(wave->lo[0]!=wave->lo_r[0] || wave->hi[0]!=wave->hi_r[0]);}
    reset_state();
    if(waves)fw_free(waves);if(saved_record)fw_free(saved_record);assert(!live);
    assert(VirtualFree(project_ram,0,MEM_RELEASE));assert(VirtualFree(blob_ram,0,MEM_RELEASE));
    assert(VirtualFree((void *)0x10000000u,0,MEM_RELEASE));assert(VirtualFree((void *)0x43bd0000u,0,MEM_RELEASE));
    puts("PASS: project pool codecs/migration, snapshot/validation, stock preservation, boot page mapping, full long-sample CRC, shared transfer and a stereo entry");return 0;
}
