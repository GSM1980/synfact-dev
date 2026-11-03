/* SPDX-License-Identifier: GPL-2.0-or-later
 * Optional machine bank: silent preflight/load, private initialization, then
 * native project/sample startup. Never writes eMMC or the boot firmware.
 */
#include "machines.h"
typedef st_u32 u32;typedef core_u8 u8;
extern int fw_mmc_read(u32,u32,void *);
extern u32 fw_mmc_ready[],fw_mmc_sectors[];
extern void *fw_malloc(u32);extern void fw_free(void *);
extern void core_machine_flush(void *,u32);
extern const u32 core_machine_audio_abi[];
extern int core_pool_available(void);
struct st_smz_module;
extern int core_modules_bind(const struct st_smz_module *,u32);
extern void core_native_initialize(void);
static u32 sectors(void){return fw_mmc_ready[0]?fw_mmc_sectors[0]:0;}
static const struct core_mods_io storage={fw_mmc_read,0,sectors};
volatile u32 core_machine_status=9;
u32 core_machine_file_bytes,core_machine_file_magic;
u32 core_machine_paints; /* Reserved diagnostic: silent loading never draws. */
u32 core_machine_pages;
const u8 *core_machine_audio;
static void *retained;
/* Host API 12: the loaded bank's mod-info directory, null when none is published. */
const unsigned char *core_bank_info;
struct disk_reader {u32 sector,valid;u8 cache[4096] __attribute__((aligned(16)));};
static int disk_read(void *context,u32 at,void *out,u32 n)
{
    struct disk_reader *s=context;u8 *q=out;u32 i,base,count;
    while(n){
        base=at&~4095u;
        if(!s->valid || s->sector!=base){
            if(fw_mmc_read((CORE_MODS_BASE+base)>>9,4096,s->cache))return -1;
            s->sector=base;s->valid=1;
        }
        i=at-base;count=4096-i;if(count>n)count=n;
        while(count--){*q++=s->cache[i++];at++;n--;}
    }
    return 0;
}
/* The first native UI message changes the title; subsequent messages update
 * the original bar. Report percentage changes even for batches <128 pages. */
#ifndef CORE_SAMPLE_NOTIFY
#define CORE_SAMPLE_NOTIFY ((void (*)(u32))0x4008297cu)
#endif
void core_sample_progress(u32 page,u32 total)
{
    static u32 previous=101;u32 percent;
    if(!total)return;
    percent=100u*(page+1)/total;
    if(!page){previous=101;CORE_SAMPLE_NOTIFY(0);}
    if(percent!=previous){previous=percent;CORE_SAMPLE_NOTIFY(percent);}
}
void core_machine_load(void)
{
    struct disk_reader disk;struct core_smz_reader reader;
    struct core_smz_view view,again;
    u32 size,at,n,allocated;u8 *file;void *guard;
    /* This is a once-per-boot operation. A repeated native state load must
     * never reinitialize live machines or leak the resident bank. */
    if(retained)return;
    core_native_initialize();
    core_machine_active=0;core_machine_pages=0;core_machine_audio=0;core_machine_ui_definitions=0;
    core_machine_status=1;
    {int found=core_mods_stat_io(&storage,&size);
     if(found!=1){if(found<0)core_machine_status=8;return;}}
    core_machine_file_bytes=size;core_machine_status=6;
    if(core_machine_audio_abi[0]!=CORE_SMZ_ABI)return;
    disk.valid=0;reader.context=&disk;reader.read=disk_read;reader.bytes=size;
    core_machine_status=2;
    {u8 magic[4];if(!disk_read(&disk,0,magic,4))core_machine_file_magic=core_smz_be(magic);}
    /* Loading is silent throughout. Missing/incompatible or corrupt files
     * never run machine code. The native sample progress remains separate. */
    if(!core_smz_validate(&reader,&view))return;
    {u8 feature[4];
     if(disk_read(&disk,24,feature,4))return;
     if(core_smz_be(feature)&1u && !core_pool_available()){core_machine_status=7;return;}}
    allocated=(size+511)&~511u;core_machine_status=4;
    /* Keep the native sample builder's unchecked 512 KiB scratch allocation
     * available. The file cannot consume this reservation. */
    guard=fw_malloc(0x80000u);if((u32)guard<0x40000000u)return;
    file=fw_malloc(allocated);
    if((u32)file<0x40000000u){fw_free(guard);return;}
    core_machine_status=3;
    for(at=0;at<size;at+=n){
        n=size-at;if(n>0x10000u)n=0x10000u;
        if(fw_mmc_read((CORE_MODS_BASE+at)>>9,(n+511)&~511u,file+at))goto reject;
    }
    /* Recheck the actual RAM bytes before changing ANY pointer or publishing
     * anything, even when the preflight copy was valid. */
    reader.context=file;reader.read=core_smz_memory_read;
    core_machine_status=2;if(!core_smz_validate(&reader,&again))goto reject;
    core_smz_relocate(file,&again);
    core_machine_flush(file+again.image,again.bytes);
    if(again.ui!=CORE_SMZ_NONE)core_machine_ui_definitions=(void *)(file+again.image+again.ui);
    core_machine_status=5;
    if(!core_machine_bind((void *)(file+again.image+again.descriptors),again.count)){
        /* An initializer may have retained a private pointer. Keep its own
         * block alive until reboot, but publish no machine or AUDIO batch. */
        retained=file;fw_free(guard);return;
    }
    if(again.nmods && !core_modules_bind((void *)(file+again.image+again.modules),again.nmods)){
        core_machine_active=0;retained=file;fw_free(guard);return;}
    retained=file;core_bank_info=again.info==CORE_SMZ_NONE?0:file+again.image+again.info;
    core_machine_audio=file+again.audio;
    core_machine_pages=again.count?(again.audio_bytes+2047)>>11:0;
    core_machine_status=0;fw_free(guard);return;
reject:
    fw_free(file);fw_free(guard);
}
