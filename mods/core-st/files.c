/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native Data RPC /mods, fresh elekloader source using the MAIN 1.41
 * descriptor/stream contracts documented in Synfact's emmc-storage study.
 */
#include "file-info.h"
#include "smz.h"
extern u32 fw_mmc_ready[],fw_mmc_sectors[],fw_empty_string[];
extern int fw_mmc_read(u32,u32,void *),fw_mmc_write(u32,u32,const void *);
extern void fw_info_copy(void *,const void *),fw_string_ctor(void *,const char *,void *);
extern void fw_string_assign(void *,const char *),core_fn_manager(void);
extern void core_mods_validate(void);
static struct core_mods_store store;
static struct {struct core_mods_slot slot;u32 pending,commit_ok;} staged;
extern volatile u32 core_machine_status;
extern volatile u32 core_machine_trial,core_machine_trial_slot,core_machine_trial_sequence;
extern volatile u32 core_machine_rollback;
extern u32 core_machine_file_bytes,core_machine_file_magic,core_machine_count,core_machine_pages;
extern volatile u32 core_machine_active,core_smz_failure;
extern volatile u32 core_machine_availability_seen,core_machine_last_track,core_machine_menu_limit;
extern u32 core_machine_paints;
extern volatile u32 core_machine_available_mask,core_machine_name_seen,core_machine_category_seen;
extern volatile u32 core_machine_vector_mask,core_machine_vector_count,core_machine_vector_calls;
extern int core_machine_present(u32);
static u32 loader_status[32];
static u32 confirm_ok;
static u32 sectors(void){return fw_mmc_ready[0]?fw_mmc_sectors[0]:0;}
static const struct core_mods_io io={fw_mmc_read,fw_mmc_write,sectors};
static u32 get32(const u8 *p)
{return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static void zero(void *p,u32 n){u8 *q=p;while(n--)*q++=0;}
static void error(void *out,const char *message,u32 bytes)
{
    u8 allocator=0;zero(out,bytes);fw_string_ctor((u8 *)out+4,message,&allocator);
}
static void capture(struct fn *fn,void *invoke)
{fn->data=fn->reserved=0;fn->manager=core_fn_manager;fn->invoke=invoke;}
struct stage_reader {u32 base,sector,valid;u8 cache[4096] __attribute__((aligned(16)));};
static int stage_read(void *context,u32 at,void *out,u32 n)
{
    struct stage_reader *s=context;u8 *q=out;u32 base,i,count;
    while(n){
        base=at&~4095u;
        if(!s->valid || s->sector!=base){
            if(fw_mmc_read((s->base+base)>>9,4096,s->cache))return -1;
            s->sector=base;s->valid=1;
        }
        i=at-base;count=4096-i;if(count>n)count=n;
        while(count--){*q++=s->cache[i++];at++;n--;}
    }
    return 0;
}
static void core_mods_stage_written(const struct fn *fn,const u8 *header)
{
    struct stage_reader disk;struct core_smz_reader reader;struct core_smz_view view;
    u8 first[20];(void)fn;staged.commit_ok=0;
    if(!staged.pending || !header || get32(header+13)!=8 ||
       get32(header+25)!=staged.slot.bytes)return;
    staged.pending=0;
    disk.base=staged.slot.base;disk.valid=0;
    reader.context=&disk;reader.read=stage_read;reader.bytes=staged.slot.bytes;
    if(!core_smz_validate(&reader,&view) || view.contract!=CORE_SMZ_CONTRACT ||
       stage_read(&disk,0,first,sizeof(first)))return;
    staged.slot.stamp=get32(first+16);
    staged.commit_ok=core_mods_record_io(&io,&staged.slot);
}
static void core_mods_confirm_written(const struct fn *fn,const u8 *header)
{
    u8 b[512] __attribute__((aligned(16)));(void)fn;confirm_ok=0;
    if(!header || get32(header+13)!=8 || get32(header+25)!=16 ||
       !core_machine_trial || core_machine_status!=0 ||
       fw_mmc_read(CORE_MODS_CONFIRM>>9,512,b))return;
    if(get32(b)!=0x4d434631u || get32(b+4)!=core_machine_trial_sequence ||
       get32(b+8)!=core_machine_trial_slot ||
       get32(b+12)!=(core_machine_trial_sequence^core_machine_trial_slot^0xa55a5aa5u))return;
    confirm_ok=core_mods_transition_io(&io,core_machine_trial_slot,
                                       CORE_MODS_TRYING,CORE_MODS_GOOD);
    if(confirm_ok)core_machine_trial=0;
}
/* The native BackupFileWriter parses the transport header and footer.
 * It passes the complete original 31-byte header after its successful close. */
void core_mods_written(const struct fn *fn,const u8 *header)
{
    (void)fn;store.commit_ok=0;
    if(header && get32(header+13)==8)
        core_mods_commit_io(&store,&io,get32(header+25));
}
/* Called by the veneer below with the nontrivial BoolResult return slot. */
void core_mods_check_header(void *out,const u8 *header)
{
    u8 *r=out;
    if(!header || get32(header+13)!=8 || get32(header+25)>CORE_MODS_MAX) {
        error(out,"Invalid /mods Data object or size",12);return;
    }
    zero(out,12);r[0]=1;*(void **)(r+4)=fw_empty_string;r[8]=1;
}
#ifndef CORE_ST_NATIVE_TEST
__asm__(".globl core_mods_validate\n"
        "core_mods_validate:\n"
        "move.l %a2,-(%sp)\n"
        "move.l %a0,%a2\n"
        "move.l 12(%sp),-(%sp)\n"
        "move.l %a2,-(%sp)\n"
        "jsr core_mods_check_header\n"
        "addq.l #8,%sp\n"
        "move.l %a2,%d0\n"
        "move.l (%sp)+,%a2\n"
        "rts\n");
#endif
int core_mods_route(void *router,const char *const *path,void *out)
{
    struct info info;u32 n=0,i;int valid;u8 *r=out;(void)router;
    if(path && *path){
        const char *p=*path,*q="/mod-status";
        while(*p && *p==*q){p++;q++;}
        if(!*p && !*q){
            zero(loader_status,sizeof(loader_status));
            loader_status[0]=0x454c5331;loader_status[1]=2;loader_status[2]=core_machine_status;
            loader_status[3]=core_machine_file_bytes;loader_status[4]=core_machine_file_magic;
            loader_status[5]=core_smz_failure;loader_status[6]=core_machine_active;
            loader_status[7]=core_machine_count;loader_status[8]=core_machine_pages;
            loader_status[9]=fw_mmc_ready[0];loader_status[10]=fw_mmc_sectors[0];
            for(i=64;i<80;i++)if(core_machine_present(i))loader_status[11]|=1u<<(i-64);
            loader_status[12]=core_machine_availability_seen;loader_status[13]=core_machine_last_track;
            loader_status[14]=core_machine_menu_limit;loader_status[15]=core_machine_paints;
            loader_status[16]=core_machine_available_mask;loader_status[17]=core_machine_vector_mask;
            loader_status[18]=core_machine_vector_count;loader_status[19]=core_machine_vector_calls;
            loader_status[20]=core_machine_name_seen;loader_status[21]=core_machine_category_seen;
            loader_status[22]=CORE_SMZ_CONTRACT;
            loader_status[23]=core_machine_trial;
            loader_status[24]=core_machine_trial_slot;
            loader_status[25]=core_machine_trial_sequence;
            loader_status[26]=core_machine_rollback;
            zero(&info,sizeof(info));info.type=8;info.kind=1;info.permissions=2;
            info.address=(u32)loader_status;info.bytes=sizeof(loader_status);info.id=1;info.tag=0xffffffffu;
            zero(out,8);r[0]=1;*(void **)(r+4)=fw_empty_string;fw_info_copy(r+8,&info);return 1;
        }
    }
    if(!path || (!core_mods_path(*path) && !core_mods_stage_path(*path) &&
                 !core_mods_confirm_path(*path)))return 0;
    zero(&info,sizeof(info));info.type=8;info.kind=2;info.id=1;info.tag=0xffffffffu;
    if(core_mods_confirm_path(*path)){
        info.permissions=6;info.address=CORE_MODS_CONFIRM;info.bytes=16;
        capture(&info.written,core_mods_confirm_written);
        capture(&info.validate,core_mods_validate);
    }else if(core_mods_stage_path(*path)){
        struct core_mods_slot target,existing;
        if(!core_mods_stage_io(&io,&target)){
            error(out,"/mods-stage eMMC is unavailable",0x84);return 1;}
        valid=core_mods_slot_io(&io,target.slot,&existing);
        if(valid<0){error(out,"/mods-stage eMMC is unavailable",0x84);return 1;}
        info.permissions=valid==1?6:4;info.address=target.base;
        info.bytes=valid==1?existing.bytes:0;
        capture(&info.written,core_mods_stage_written);
        capture(&info.validate,core_mods_validate);
    }else{
        struct core_mods_slot primary,backup;
        valid=core_mods_select_io(&io,&primary,&backup);
        if(valid<0){error(out,"/mods eMMC is unavailable",0x84);return 1;}
        if(valid==1){n=primary.bytes;info.address=primary.base;}
        else {valid=core_mods_stat_io(&io,&n);info.address=CORE_MODS_BASE;}
        if(valid<0){error(out,"/mods eMMC is unavailable",0x84);return 1;}
        info.permissions=2;info.bytes=valid==1?n:0;
    }
    /* The native constructor clones only our inline captures through our
     * manager. No cached/borrowed std::function object is ever memcpy'd. */
    zero(out,8);r[0]=1;*(void **)(r+4)=fw_empty_string;
    fw_info_copy(r+8,&info);return 1;
}
int core_mods_write(void *router,const struct info *info,void *out)
{
    (void)router;
    if(info->written.manager!=(void *)core_fn_manager)return 0;
    if(info->written.invoke==(void *)core_mods_confirm_written){
        if(info->type!=8 || info->kind!=2 || info->address!=CORE_MODS_CONFIRM ||
           info->bytes!=16 || !core_machine_trial || core_machine_status!=0){
            error(out,"No matching mods trial to confirm",16);return 1;}
        confirm_ok=0;return 0;
    }
    if(info->written.invoke!=(void *)core_mods_stage_written)return 0;
    if(info->type!=8 || info->kind!=2 || info->bytes>CORE_MODS_SLOT_CAP) {
        error(out,"/mods-stage bank exceeds 8 MiB or has an invalid destination",16);return 1;
    }
    /* The native writer passes the destination selected by route. Recompute
     * it before any body IO: a stale descriptor may never target a GOOD slot. */
    if(!core_mods_stage_io(&io,&staged.slot) || info->address!=staged.slot.base ||
       !info->bytes || !core_mods_invalidate_slot_io(&io,staged.slot.slot)){
        error(out,"Failed to prepare /mods-stage storage",16);return 1;}
    staged.slot.bytes=info->bytes;staged.pending=1;staged.commit_ok=0;
    return 0; /* Continue into the stock bounded SafeMmcStreamWriter. */
}
void core_mods_finish(const struct fn *fn,const u8 *header,void *status)
{
    (void)header;
    if(fn->manager!=(void *)core_fn_manager)return;
    if(fn->invoke!=(void *)core_mods_stage_written &&
       fn->invoke!=(void *)core_mods_confirm_written)return;
    if(!(fn->invoke==(void *)core_mods_stage_written?staged.commit_ok:confirm_ok)) {
        *(u8 *)status=0;
        fw_string_assign((u8 *)status+4,"Failed to validate or commit mods update");
    }
}
