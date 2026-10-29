/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native 32-bit host test of the real C route and storage code. No ColdFire
 * emulation: IO/constructors are replaced by explicit bounded test doubles.
 * The production assembly veneers/native stream implementation are not run.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define CORE_ST_NATIVE_TEST
#include "../mods/core-st/files.c"
static u8 disk[512];
static u32 reads,writes,read_fail,write_fail;
static const char *last_error;
volatile u32 core_machine_status=2,core_machine_active,core_smz_failure=89;
u32 core_machine_file_bytes=7952,core_machine_file_magic=0x53414645,core_machine_count,core_machine_pages;
volatile u32 core_machine_availability_seen,core_machine_last_track,core_machine_menu_limit;
u32 core_machine_paints;
volatile u32 core_machine_available_mask,core_machine_name_seen,core_machine_category_seen;
volatile u32 core_machine_vector_mask,core_machine_vector_count,core_machine_vector_calls;
int core_machine_present(u32 type){return type>=66 && type<=69;}
u32 fw_mmc_ready[]={1},fw_mmc_sectors[]={CORE_MODS_LBA+1},fw_empty_string[]={0};
void core_fn_manager(void){}
void core_mods_validate(void){}
int fw_mmc_read(u32 lba,u32 n,void *out)
{assert(lba==CORE_MODS_LBA && n==512);reads++;if(read_fail)return -1;memcpy(out,disk,512);return 0;}
int fw_mmc_write(u32 lba,u32 n,const void *in)
{assert(lba==CORE_MODS_LBA && n==512);writes++;if(write_fail)return -1;memcpy(disk,in,512);return 0;}
void fw_info_copy(void *out,const void *in){memcpy(out,in,sizeof(struct info));}
void fw_string_ctor(void *out,const char *s,void *allocator)
{(void)allocator;*(void **)out=fw_empty_string;last_error=s;}
void fw_string_assign(void *out,const char *s){*(void **)out=fw_empty_string;last_error=s;}
static void put(u8 *p,u32 n){p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static struct info route(void)
{
    u32 out[33];const char *path="/mods";struct info result;
    assert(core_mods_route(0,&path,out)==1);assert(*(u8 *)out==1);
    memcpy(&result,(u8 *)out+8,sizeof(result));return result;
}
static void close_ok(u32 bytes)
{
    u8 header[31]={0};u32 status[5]={0};struct info info=route();
    put(header+13,8);put(header+25,bytes);*(u8 *)status=1;
    core_mods_written(&info.written,header);
    core_mods_finish(&info.written,header,status);assert(*(u8 *)status==1);
}
int main(void)
{
    {const char *path="/mod-status";u32 out[33];struct info info;
     assert(core_mods_route(0,&path,out)==1);memcpy(&info,(u8 *)out+8,sizeof(info));
     assert(info.kind==1 && info.permissions==2 && info.bytes==128 && info.id==1);
     assert(((u32 *)info.address)[0]==0x454c5331 && ((u32 *)info.address)[2]==2 && ((u32 *)info.address)[5]==89);
     assert(!reads && !writes);}
    struct info info;u32 out[33],before,n;const char *path;
    const char *others[]={"","/","/mod","/mods/","/mods/x","/mods2","/samples/1","/projects/1"};
    unsigned i;u8 header[31]={0};
    assert(sizeof(struct info)==124);assert(writes==0);
    for(i=0;i<sizeof(others)/sizeof(*others);i++){
        path=others[i];before=reads;memset(out,0xa5,sizeof(out));
        assert(core_mods_route(0,&path,out)==0 && reads==before);
        assert(out[0]==0xa5a5a5a5u);
    }
    info=route();assert(info.permissions==4 && info.bytes==0 && writes==0);
    assert(info.address==0x02400000 && info.type==8 && info.kind==2);
    assert(core_mods_span(CORE_MODS_BASE,0));assert(core_mods_span(CORE_MODS_BASE,CORE_MODS_MAX));
    assert(!core_mods_span(CORE_MODS_BASE-1,1));assert(!core_mods_span(CORE_MODS_BASE+1,1));
    for(i=0;i<2;i++){
        info.bytes=i?0xffffffffu:CORE_MODS_MAX+1;before=writes;
        assert(core_mods_write(0,&info,out)==1 && *(u8 *)out==0 && writes==before);
    }
    info.bytes=CORE_MODS_MAX;assert(core_mods_write(0,&info,out)==0);
    assert(store.pending && store.bytes==CORE_MODS_MAX && route().permissions==4);
    assert(core_mods_stat_io(&io,&n)==0);close_ok(CORE_MODS_MAX);
    assert(core_mods_stat_io(&io,&n)==1 && n==CORE_MODS_MAX);
    assert(get32(disk+12)==CORE_MODS_MAX && get32(disk+16)==~CORE_MODS_MAX);
    memset(&store,0,sizeof(store));info=route();
    assert(info.permissions==6 && info.bytes==CORE_MODS_MAX); /* persisted after reset */
    /* Rejected oversize does not invalidate the previous file. */
    info.bytes=CORE_MODS_MAX+1;before=writes;
    assert(core_mods_write(0,&info,out)==1 && writes==before);
    assert(core_mods_stat_io(&io,&n)==1 && n==CORE_MODS_MAX);
    /* Accepted replacement, then interruption: never publish a partial file. */
    info.bytes=17;assert(core_mods_write(0,&info,out)==0);
    memset(&store,0,sizeof(store));assert(route().permissions==4);
    info=route();info.bytes=17;assert(core_mods_write(0,&info,out)==0);close_ok(17);
    assert(core_mods_stat_io(&io,&n)==1 && n==17);
    disk[12]^=1;assert(core_mods_stat_io(&io,&n)==0 && n==0); /* corrupt record */
    info=route();info.bytes=0;assert(core_mods_write(0,&info,out)==0);close_ok(0);
    assert(route().permissions==6 && route().bytes==0); /* empty file is valid */
    /* Other native descriptors are declined and never cause metadata IO. */
    info=route();info.written.manager=0;before=writes;
    assert(core_mods_write(0,&info,out)==0 && writes==before);
    /* Short/unknown/not-ready cards are rejected before block IO. */
    info=route();info.bytes=17;
    for(i=0;i<3;i++){
        before=reads+writes;fw_mmc_sectors[0]=i?0xffffffffu:CORE_MODS_LBA;
        if(i==2)fw_mmc_ready[0]=0;
        path="/mods";assert(core_mods_route(0,&path,out)==1 && *(u8 *)out==0);
        assert(core_mods_write(0,&info,out)==1 && *(u8 *)out==0);
        assert(reads+writes==before);
    }
    fw_mmc_ready[0]=1;fw_mmc_sectors[0]=CORE_MODS_LBA+1;
    read_fail=1;assert(core_mods_stat_io(&io,&n)==-1);read_fail=0;
    write_fail=1;assert(core_mods_write(0,&info,out)==1 && !store.pending);write_fail=0;
    assert(core_mods_write(0,&info,out)==0);write_fail=1;
    put(header+13,8);put(header+25,17);*(u8 *)out=1;
    core_mods_written(&info.written,header);core_mods_finish(&info.written,header,out);
    assert(*(u8 *)out==0 && last_error && !store.commit_ok);write_fail=0;
    before=writes;core_mods_check_header(out,header);assert(*(u8 *)out==1 && writes==before);
    put(header+25,CORE_MODS_MAX+1);core_mods_check_header(out,header);assert(*(u8 *)out==0);
    put(header+25,17);put(header+13,1);core_mods_check_header(out,header);assert(*(u8 *)out==0);
    puts("PASS: /mods route, exact/oversize bounds, persistence, abort, invalid metadata, short cards and IO failures");
    return 0;
}
