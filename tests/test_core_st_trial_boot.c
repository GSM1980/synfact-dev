/* SPDX-License-Identifier: GPL-2.0-or-later
 * The safe-boot cycle end to end: the production boot loader (machine-boot.c)
 * linked with the production slot storage (storage.c) and parser (smz.c),
 * over a simulated eMMC. No storage function is replaced by a double: core
 * 1.7's boot storage had no write function, and its first trial boot called
 * address 0 on the device (every boot, since the record never changed). The
 * test that missed it replaced core_mods_transition_io.
 *
 * Usage: test_core_st_trial_boot.exe <bank.smz4>
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void sample_notify(unsigned int);
#define CORE_SAMPLE_NOTIFY sample_notify
#include "../mods/core-st/machine-boot.c"
static void sample_notify(u32 percent){(void)percent;}
/* The simulated card: the 16 MiB bank reservation and the legacy record. */
#define DISK_BYTES (CORE_MODS_MAX+512u)
static u8 *card;static u32 writes,boot_writes,expected_count;
u32 fw_mmc_ready[]={1},fw_mmc_sectors[]={CORE_MODS_LBA+1};
static u8 *at(u32 lba,u32 n)
{
    u32 a=lba<<9;
    assert(a>=CORE_MODS_BASE && a-CORE_MODS_BASE<=DISK_BYTES && n<=DISK_BYTES-(a-CORE_MODS_BASE));
    return card+(a-CORE_MODS_BASE);
}
int fw_mmc_read(u32 lba,u32 n,void *out){memcpy(out,at(lba,n),n);return 0;}
int fw_mmc_write(u32 lba,u32 n,const void *in)
{
    /* The boot writes nothing but a trial slot's 512-byte record. */
    assert(n==512 && ((lba<<9)==CORE_MODS_SLOT_META(0) || (lba<<9)==CORE_MODS_SLOT_META(1)));
    writes++;memcpy(at(lba,n),in,n);return 0;
}
const u32 core_machine_audio_abi[]={CORE_SMZ_ABI};
int core_pool_available(void){return 1;}
volatile u32 core_machine_active;
const struct st_machine_ui *core_machine_ui_definitions;
int core_modules_bind(const struct st_smz_module *m,u32 n){assert(m && n);return 1;}
int core_boot_preimages_ok(const core_u8 *info){assert(info);return 1;}
void core_native_initialize(void){}
static u32 allocations;
void *fw_malloc(u32 n)
{allocations++;return VirtualAlloc((void *)(0x46000000u+0x1000000u*allocations),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}
void fw_free(void *p){assert(VirtualFree(p,0,MEM_RELEASE));}
void core_machine_flush(void *p,u32 n){assert(p && n);}
int core_machine_bind(const struct core_machine_def *d,u32 n)
{assert(d && n==expected_count);core_machine_active=1;return 1;}
/* The device's own accessors, as files.c and machine-boot.c take them. */
static u32 card_sectors(void){return CORE_MODS_LBA+1;}
static const struct core_mods_io device={fw_mmc_read,fw_mmc_write,card_sectors};
/* What /mods-stage does (files.c): the inactive slot, the body, its record. */
static struct core_mods_slot stage(const u8 *bank,u32 bytes)
{
    struct core_mods_slot s;
    assert(core_mods_stage_io(&device,&s) && core_mods_invalidate_slot_io(&device,s.slot));
    memcpy(card+(s.base-CORE_MODS_BASE),bank,bytes);
    s.bytes=bytes;s.stamp=0x5ca1ab1e;assert(core_mods_record_io(&device,&s));
    return s;
}
static u32 state_of(u32 slot)
{struct core_mods_slot s;return core_mods_slot_io(&device,slot,&s)==1?s.state:0;}
/* A power cycle: the resident bank and its machines are gone. */
static void boot(void)
{
    if(retained)fw_free(retained);
    retained=0;core_machine_active=0;writes=0;core_machine_load();boot_writes=writes;
}
int main(int argc,char **argv)
{
    FILE *f;u8 *bank,*bad;u32 bytes,cases=0;struct core_mods_slot first,second,third;
    struct core_smz_reader r;struct core_smz_view v;
    assert(argc==2);f=fopen(argv[1],"rb");assert(f);
    fseek(f,0,SEEK_END);bytes=(u32)ftell(f);rewind(f);
    bank=malloc(bytes);bad=malloc(bytes);assert(bank && bad && fread(bank,1,bytes,f)==bytes);fclose(f);
    r.context=bank;r.read=core_smz_memory_read;r.bytes=bytes;
    assert(core_smz_validate(&r,&v) && v.contract==CORE_SMZ_CONTRACT);expected_count=v.count;
    memcpy(bad,bank,bytes);bad[bytes/2]^=0x5a;      /* fails its checksum */
    card=calloc(1,DISK_BYTES);assert(card);
    /* No bank: stock machines, nothing written. */
    boot();assert(core_machine_status==1 && !core_machine_active && !boot_writes);cases++;
    /* A staged bank boots as a trial: its record says TRYING before any
     * bank code runs. This is the step core 1.7 crashed on. */
    first=stage(bank,bytes);assert(state_of(first.slot)==CORE_MODS_CANDIDATE);
    boot();assert(core_machine_status==0 && core_machine_active && core_machine_trial &&
                  core_machine_trial_slot==first.slot && boot_writes==1 &&
                  state_of(first.slot)==CORE_MODS_TRYING);cases++;
    /* It crashed before confirmation: the next boot does not run it again. */
    boot();assert(!core_machine_active && !core_machine_trial && core_machine_rollback &&
                  !boot_writes && state_of(first.slot)==CORE_MODS_TRYING);cases++;
    /* Another try, confirmed by the health tick (files.c): GOOD, and then an
     * ordinary boot with no write. */
    second=stage(bank,bytes);
    boot();assert(core_machine_trial && boot_writes==1 && state_of(second.slot)==CORE_MODS_TRYING);
    assert(core_mods_transition_io(&device,second.slot,CORE_MODS_TRYING,CORE_MODS_GOOD));
    boot();assert(core_machine_status==0 && core_machine_active && !core_machine_trial &&
                  !core_machine_rollback && !boot_writes && state_of(second.slot)==CORE_MODS_GOOD);cases++;
    /* A corrupt candidate in the other slot is spent and the GOOD bank loads. */
    third=stage(bad,bytes);assert(third.slot!=second.slot);
    boot();assert(core_machine_status==0 && core_machine_active && !core_machine_trial &&
                  core_machine_rollback && boot_writes==1 && state_of(third.slot)==CORE_MODS_TRYING &&
                  state_of(second.slot)==CORE_MODS_GOOD);cases++;
    boot();assert(core_machine_active && !boot_writes && state_of(second.slot)==CORE_MODS_GOOD);cases++;
    /* A good candidate replacing it, crashed: the GOOD bank comes back. */
    third=stage(bank,bytes);assert(third.slot!=second.slot);
    boot();assert(core_machine_trial && core_machine_trial_slot==third.slot && boot_writes==1);
    boot();assert(core_machine_active && !core_machine_trial && core_machine_rollback && !boot_writes &&
                  state_of(second.slot)==CORE_MODS_GOOD);cases++;
    if(retained)fw_free(retained);
    free(card);free(bank);free(bad);
    printf("PASS: %u safe-boot cycles on the production loader and slot storage: trial marked TRYING, "
           "crash rollback, confirmation, corrupt candidate, GOOD restored; boot writes only trial records\n",cases);
    return 0;
}
