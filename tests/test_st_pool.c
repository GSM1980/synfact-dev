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
static u32 stock_lengths[64],stock_reads,stock_peaks,stock_io_fail;
static u32 fail_allocations,fail_boot;
static void *owned[64];
const unsigned char core_pool_image[4096]={0x45,0x4c,0x50,0x31,0,0,0x10,0};
/* 2.1.1: only the header and code are resident (pool_audio.py); this one has a header only. */
const u32 core_pool_image_bytes=64;
u32 fw_mmc_ready[]={1},fw_mmc_sectors[]={0x3a4000},core_machine_pages;
void *fw_malloc(u32 n)
{
    if(fail_allocations || (fail_boot && n==5120))return 0;
    u32 i;void *p;for(i=0;i<64 && owned[i];i++){}assert(i<64);
    p=VirtualAlloc((void *)(0x47000000u+0x100000u*i),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(p){owned[i]=p;live++;allocations++;}return p;
}
void fw_free(void *p){u32 i;for(i=0;i<64 && owned[i]!=p;i++){}assert(i<64);
 assert(VirtualFree(p,0,MEM_RELEASE));owned[i]=0;live--;}
void *fw_memcpy(void *d,const void *s,u32 n){return memcpy(d,s,n);}
void *fw_memset(void *d,int v,u32 n){return memset(d,v,n);}
int fw_mmc_read(u32 lba,u32 n,void *p)
{
    if(lba==0x80000u && n==512){memcpy(p,project_sector,512);return 0;}
    if(lba>=8*1024u && lba<72*1024u){
        u32 slot=(lba>>10)-8,at=(lba&1023u)*512,k,frame;u8 *q=p,header[64]={0};
        stock_reads++;if(stock_io_fail)return -1;wr(header,0x53414d50u);wr(header+8,stock_lengths[slot]);
        for(k=0;k<n;k++){
            if(at+k<64)q[k]=header[at+k];
            else {frame=(at+k-64)/2;u32 v=slot*257;
                if(stock_peaks && !slot)v=frame==4997?1234:frame==4999?20000:0;
                q[k]=(u8)(v>>((at+k)&1?0:8));}
        }return 0;
    }
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
{core_pool_boot_release();if(state)fw_free(state);state=0;stock_last=0xffffffffu;
 if(waves)fw_free(waves);waves=0;work=128;
 core_pool_stock_bytes=256;core_pool_boot_pages=0;core_pool_busy=0;assert(live==(saved_record!=0)+(waves!=0));}
int main(void)
{
    u16 entries[128];u8 object[516],source_project[512],copy_project[512];u32 i,wire;
    const u8 *page;const struct st_machine_wave *wave;void *project_ram;
    project_ram=VirtualAlloc((void *)0x40650000u,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(project_ram==(void *)0x40650000u);
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
    for(i=0;i<64;i++)stock_lengths[i]=4;
    memset(PROJECT,0,512);core_pool_read(PROJECT,entries);entries[64]=0x8001;core_pool_write(PROJECT,entries);
    core_pool_prepare();assert(core_pool_boot_pages==4 && state && state->files==1);
    page=core_pool_boot_page(0,&wire);assert(wire==0x8003 && !memcmp(page,sample+64,2048));
    assert(core_pool_boot_page(0,&wire)==page); /* sender retry */
    page=core_pool_boot_page(1,&wire);assert(wire==0x8004 && !memcmp(page,sample+64+2048,152));
    for(i=152;i<2048;i++)assert(!page[i]);
    page=core_pool_boot_page(2,&wire);assert(wire==0x8001 && rd(page)==IMAGE_MAGIC);
    assert(rd(page+8)==adler(state->image+12,4096-12));
    page=core_pool_boot_page(3,&wire);assert(wire==0x8002);
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
    *(volatile u32 *)0x43bdd824u=1;core_pool_created();assert(receiver_calls);
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
    /* Boot does no envelope work. Lazy UI slices reread disk after release;
     * exact cached peaks then require no further reads. */
    stock_lengths[0]=5000;stock_peaks=1;core_pool_prepare();
    for(i=0;i<(core_pool_stock_bytes+2047)/2048;i++){
        const void *p=core_pool_stock_page(i);u32 before=stock_reads;
        assert(core_pool_stock_page(i)==p && stock_reads==before);
    }
    assert(!waves && !core_pool_wave(0));
    {u32 before=stock_reads,frames;const u8 *pcm;
     core_pool_boot_release();assert(!boot);
     assert(core_pool_sample_source(0,&pcm,&frames)==1 && !pcm && frames==5000);
     for(i=0;i<3;i++)core_pool_wave_idle(0,2048);
     wave=core_pool_wave(0);assert(stock_reads>before && wave && wave->ready==2 && wave->hi[95]==20000);
     before=stock_reads;core_pool_wave_idle(0,2048);assert(stock_reads==before);
     core_pool_stock_changed(0x400000);core_pool_wave_idle(0,2048);assert(stock_reads==before && wave->ready==2);}
    reset_state();
    stock_lengths[0]=4;stock_peaks=0;
    /* A write during a partial lazy scan must never produce a waveform of
     * bytes different from the PCM that AUDIO loaded. */
    stock_lengths[0]=5000;core_pool_prepare();core_pool_boot_release();core_pool_wave_idle(0,64);
    wave=core_pool_wave(0);assert(wave && !wave->ready);
    {u32 before=stock_reads;core_pool_stock_changed(0x400000);core_pool_wave_idle(0,2048);
     assert(stock_reads==before && !wave->ready);}
    reset_state();stock_lengths[0]=4;
    /* Long samples also defer envelopes and independently cache lazy reads. */
    assert(core_hzfs_put(&core_hzfs_native_io,"two",sizeof(sample),0,sample_source,0,&id)==1 && id==2);
    memset(PROJECT,0,512);core_pool_read(PROJECT,entries);entries[64]=0x8001;entries[65]=0x8002;
    core_pool_write(PROJECT,entries);core_pool_prepare();assert(state && state->files==2);
    for(i=0;i<core_pool_boot_pages;i++)core_pool_boot_page(i,&wire);
    assert(!waves);core_pool_boot_release();core_pool_wave_idle(64,2048);core_pool_wave_idle(65,2048);
    {const struct st_machine_wave *a=core_pool_wave(64),*b=core_pool_wave(65);
     assert(a && b && a!=b && a->ready==2 && b->ready==2 && a->code==0x8001 && b->code==0x8002);}
    reset_state();
    /* The boot CRC still validates AUDIO. If disk contents later change,
     * a lazy HZFS scan must reject its new CRC rather than cache that wave. */
    core_pool_prepare();for(i=0;i<core_pool_boot_pages;i++)core_pool_boot_page(i,&wire);
    core_pool_boot_release();disk[2][100]^=1;core_pool_wave_idle(64,2048);
    wave=core_pool_wave(64);assert(wave && !wave->ready);disk[2][100]^=1;
    core_pool_wave_idle(64,2048);assert(wave->ready==2);reset_state();
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
    assert(!waves);core_pool_boot_release();core_pool_wave_idle(64,2048);
    wave=core_pool_wave(64);assert(wave && wave->frames==550 && wave->ready==2 && wave->channels==2);
    assert(core_pool_sample_channels(64)==2 && core_pool_sample_channels(0)==1 && core_pool_sample_channels(65)==1);
    /* L (even bytes 64+4k..) and R keep their own peaks: the pattern is i&255,
     * so in bin 0 L's samples start 0x4041 and R's 0x4243. */
    {short l=(short)(sample[64]<<8|sample[65]),r=(short)(sample[66]<<8|sample[67]);
     assert(wave->lo[0]<=l && wave->hi[0]>=l && wave->lo_r[0]<=r && wave->hi_r[0]>=r);
     assert(wave->lo[0]!=wave->lo_r[0] || wave->hi[0]!=wave->hi_r[0]);}
    reset_state();
    /* Full legal stock capacity and a mixture of empty, short and rejected
     * oversize headers. Compare every byte, including page boundary splits. */
    for(u32 variant=0;variant<2;variant++){
        u32 total=0,at=0,count;u8 *expected;
        for(i=0;i<64;i++){
            stock_lengths[i]=variant?(i%4==0?0:i%4==1?1:i%4==2?2047:240001):240000;
            total+=4+2*(stock_lengths[i]<=240000?stock_lengths[i]:0);
        }
        expected=malloc(total);assert(expected);
        for(i=0;i<64;i++){
            u32 n=stock_lengths[i]<=240000?stock_lengths[i]:0;wr(expected+at,n);at+=4;
            for(u32 k=0;k<n;k++){expected[at++]=(u8)i;expected[at++]=(u8)i;}
        }
        memset(PROJECT,0,512);core_pool_prepare();assert(core_pool_stock_bytes==total);
        count=(total+2047)/2048;
        for(i=0;i<count;i++){
            u32 n=total-i*2048;if(n>2048)n=2048;
            page=core_pool_stock_page(i);assert(!memcmp(page,expected+i*2048,n));
            for(u32 k=n;k<2048;k++)assert(!page[k]);
        }
        for(i=0;i<core_pool_boot_pages;i++)core_pool_boot_page(i,&wire);
        assert(!waves);core_pool_boot_release();
        if(!variant)for(i=0;i<64;i++){
            for(u32 k=0;k<118;k++)core_pool_wave_idle(i,2048);
            wave=core_pool_wave(i);assert(wave && wave->ready==2 && wave->frames==240000);
            for(u32 k=0;k<96;k++)assert(wave->lo[k]==(short)(i*257) && wave->hi[k]==(short)(i*257));
        }
        core_pool_boot_release();assert(!boot);core_pool_boot_release();free(expected);reset_state();
    }
    /* Missing disk and a failed mid-stream read retain a safe empty/silent
     * result and never publish an exact waveform for failed PCM. */
    stock_io_fail=1;core_pool_prepare();assert(core_pool_stock_bytes==256);
    page=core_pool_stock_page(0);for(i=0;i<2048;i++)assert(!page[i]);reset_state();stock_io_fail=0;
    for(i=0;i<64;i++)stock_lengths[i]=4;
    core_pool_prepare();stock_io_fail=1;core_pool_stock_page(0);stock_io_fail=0;
    core_pool_boot_release();{u32 before=stock_reads;core_pool_wave_idle(0,2048);assert(stock_reads==before && !waves);}reset_state();
    fail_allocations=1;core_pool_prepare();assert(!state && !boot && core_pool_stock_bytes==256);
    page=core_pool_stock_page(0);for(i=0;i<2048;i++)assert(!page[i]);fail_allocations=0;
    fail_boot=1;core_pool_prepare();assert(!state && !boot);fail_boot=0;reset_state();
    if(waves)fw_free(waves);if(saved_record)fw_free(saved_record);assert(!live);
    assert(VirtualFree(project_ram,0,MEM_RELEASE));
    assert(VirtualFree((void *)0x10000000u,0,MEM_RELEASE));assert(VirtualFree((void *)0x43bd0000u,0,MEM_RELEASE));
    puts("PASS: project pool codecs/migration, snapshot/validation, stock preservation, boot page mapping, full long-sample CRC, shared transfer and a stereo entry");return 0;
}
