/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native 32-bit host test of the real Data RPC route and safe bank lifecycle.
 * ColdFire veneers and the native stream are replaced by bounded doubles. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CORE_ST_NATIVE_TEST
#include "../mods/core-st/files.c"
static u8 mock_sector[3][512],payload[4096];
static u32 reads,writes,read_fail,write_fail;
static int audio_ready,pool_ready,pool_available=1;
static const char *last_error;
volatile u32 core_machine_status=2,core_machine_active,core_smz_failure=89;
volatile u32 core_machine_trial,core_machine_trial_slot,core_machine_trial_sequence;
volatile u32 core_machine_rollback;
u32 core_machine_file_bytes=7952,core_machine_file_magic=0x53414645,core_machine_count,core_machine_pages;
volatile u32 core_machine_availability_seen,core_machine_last_track,core_machine_menu_limit;
u32 core_machine_paints;
volatile u32 core_machine_available_mask,core_machine_name_seen,core_machine_category_seen;
volatile u32 core_machine_vector_mask,core_machine_vector_count,core_machine_vector_calls;
int core_machine_present(u32 type){return type>=66 && type<=69;}
int core_mods_audio_ready(void){return audio_ready;}
int core_pool_available(void){return pool_available;}
int core_pool_ready(void){return pool_ready;}
u32 fw_mmc_ready[]={1},fw_mmc_sectors[]={CORE_MODS_LBA+1},fw_empty_string[]={0};
int core_smz_validate(const struct core_smz_reader *r,struct core_smz_view *v)
{(void)r;v->contract=CORE_SMZ_CONTRACT;return 1;}
void core_fn_manager(void){}
void core_mods_validate(void){}
static int sector_index(u32 lba)
{
    if(lba==CORE_MODS_SLOT_META(0)>>9)return 0;
    if(lba==CORE_MODS_SLOT_META(1)>>9)return 1;
    if(lba==CORE_MODS_LBA)return 2;
    assert(0);return -1;
}
int fw_mmc_read(u32 lba,u32 n,void *out)
{
    reads++;if(read_fail)return -1;
    if(lba==CORE_MODS_BASE>>9 && n==4096){memcpy(out,payload,n);return 0;}
    assert(n==512);memcpy(out,mock_sector[sector_index(lba)],n);return 0;
}
int fw_mmc_write(u32 lba,u32 n,const void *in)
{
    writes++;if(write_fail)return -1;
    assert(n==512);memcpy(mock_sector[sector_index(lba)],in,n);return 0;
}
void fw_info_copy(void *out,const void *in){memcpy(out,in,sizeof(struct info));}
void fw_string_ctor(void *out,const char *s,void *allocator)
{(void)allocator;*(void **)out=fw_empty_string;last_error=s;}
void fw_string_assign(void *out,const char *s){*(void **)out=fw_empty_string;last_error=s;}
static void put(u8 *p,u32 n){p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static struct info route(const char *path)
{
    u32 out[33];struct info result;
    assert(core_mods_route(0,&path,out)==1 && *(u8 *)out==1);
    memcpy(&result,(u8 *)out+8,sizeof(result));return result;
}
static void close_stage(struct info info,u32 bytes)
{
    u8 header[31]={0};u32 status[5]={0};
    put(header+13,8);put(header+25,bytes);*(u8 *)status=1;
    ((void (*)(const struct fn *,const u8 *))info.written.invoke)(&info.written,header);
    core_mods_finish(&info.written,header,status);assert(*(u8 *)status==1);
}
int main(void)
{
    struct info info;struct core_mods_slot bank;u32 out[33],before;const char *path;
    const char *others[]={"","/","/mod","/mods/","/mods/x","/mods2","/samples/1"};
    unsigned i;u8 header[31]={0};
    path="/mod-status";info=route(path);
    assert(info.kind==1 && info.permissions==2 && info.bytes==128);
    assert(((u32 *)info.address)[22]==CORE_SMZ_CONTRACT && !writes);
    for(i=0;i<sizeof(others)/sizeof(*others);i++){
        path=others[i];before=reads;memset(out,0xa5,sizeof(out));
        assert(core_mods_route(0,&path,out)==0 && reads==before);
    }
    info=route("/mods");assert(info.permissions==2 && info.bytes==0);
    info=route("/mods-stage");assert(info.address==CORE_MODS_BASE && info.bytes==0);
    /* Rejected size and foreign capture do not touch metadata. */
    before=writes;info.bytes=CORE_MODS_SLOT_CAP+1;
    assert(core_mods_write(0,&info,out)==1 && writes==before);
    info.bytes=4096;info.written.manager=0;
    assert(core_mods_write(0,&info,out)==0 && writes==before);
    info=route("/mods-stage");info.bytes=4096;
    assert(core_mods_write(0,&info,out)==0 && staged.pending);
    put(payload+16,0x12345678);close_stage(info,4096);
    assert(core_mods_slot_io(&io,0,&bank)==1 && bank.state==CORE_MODS_CANDIDATE);
    assert(route("/mods-stage").bytes==4096 && route("/mods").bytes==4096);
    assert(core_mods_transition_io(&io,0,CORE_MODS_CANDIDATE,CORE_MODS_TRYING));
    core_machine_trial=1;core_machine_trial_slot=0;core_machine_trial_sequence=bank.sequence;
    core_machine_status=5;audio_ready=1;pool_ready=1;
    for(i=0;i<150;i++)core_mods_auto_confirm_tick();
    assert(core_mods_slot_io(&io,0,&bank)==1 && bank.state==CORE_MODS_TRYING);
    core_machine_status=0;
    audio_ready=0;pool_ready=0;
    for(i=0;i<150;i++)core_mods_auto_confirm_tick();
    assert(core_mods_slot_io(&io,0,&bank)==1 && bank.state==CORE_MODS_TRYING);
    audio_ready=1;
    for(i=0;i<150;i++)core_mods_auto_confirm_tick();
    assert(core_mods_slot_io(&io,0,&bank)==1 && bank.state==CORE_MODS_TRYING);
    pool_ready=1;
    for(i=0;i<149;i++)core_mods_auto_confirm_tick();
    assert(core_mods_slot_io(&io,0,&bank)==1 && bank.state==CORE_MODS_TRYING);
    core_mods_auto_confirm_tick();
    assert(!core_machine_trial && core_mods_slot_io(&io,0,&bank)==1 && bank.state==CORE_MODS_GOOD);
    before=writes;core_mods_auto_confirm_tick();assert(writes==before);
    info=route("/mods-stage");assert(info.address==CORE_MODS_BASE+CORE_MODS_SLOT_BYTES);
    info.bytes=4096;assert(core_mods_write(0,&info,out)==0);
    /* Interrupted replacement invalidates only the inactive slot. */
    staged.pending=0;assert(core_mods_slot_io(&io,0,&bank)==1 && bank.state==CORE_MODS_GOOD);
    assert(route("/mods").address==CORE_MODS_BASE);
    /* A metadata write failure cannot silently confirm the trial. */
    assert(core_mods_stage_io(&io,&bank) && bank.slot==1);
    bank.bytes=4096;assert(core_mods_record_io(&io,&bank));
    assert(core_mods_transition_io(&io,1,CORE_MODS_CANDIDATE,CORE_MODS_TRYING));
    core_machine_trial=1;core_machine_trial_slot=1;trial_ready_ticks=0;
    pool_available=0;pool_ready=0; /* a module-only bank needs AUDIO, no pool */
    write_fail=1;before=writes;
    for(i=0;i<150;i++)core_mods_auto_confirm_tick();
    write_fail=0;
    assert(writes==before+1);
    assert(core_machine_trial && core_mods_slot_io(&io,1,&bank)==1 && bank.state==CORE_MODS_TRYING);
    core_mods_auto_confirm_tick();assert(core_machine_trial); /* no write storm */
    assert(route("/mods").address==CORE_MODS_BASE); /* reboot would pick old GOOD */
    core_machine_trial=0;
    fw_mmc_ready[0]=0;before=reads+writes;
    path="/mods-stage";assert(core_mods_route(0,&path,out)==1 && *(u8 *)out==0);
    assert(reads+writes==before);
    fw_mmc_ready[0]=1;
    read_fail=1;path="/mods";assert(core_mods_route(0,&path,out)==1 && *(u8 *)out==0);read_fail=0;
    put(header+13,8);put(header+25,CORE_MODS_MAX+1);
    before=writes;core_mods_check_header(out,header);assert(*(u8 *)out==0 && writes==before);
    put(header+25,16);put(header+13,1);core_mods_check_header(out,header);assert(*(u8 *)out==0);
    write_fail=1;info=route("/mods-stage");info.bytes=4096;
    assert(core_mods_write(0,&info,out)==1 && last_error);write_fail=0;
    puts("PASS: safe /mods routes, staging, automatic confirmation, rollback, bounds and IO failures");
    return 0;
}
