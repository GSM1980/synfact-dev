/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native Data RPC /mods, fresh elekloader source using the MAIN 1.41
 * descriptor/stream contracts documented in Synfact's emmc-storage study.
 */
#include "file-info.h"
extern u32 fw_mmc_ready[],fw_mmc_sectors[],fw_empty_string[];
extern int fw_mmc_read(u32,u32,void *),fw_mmc_write(u32,u32,const void *);
extern void fw_info_copy(void *,const void *),fw_string_ctor(void *,const char *,void *);
extern void fw_string_assign(void *,const char *),core_fn_manager(void);
extern void core_mods_validate(void);
static struct core_mods_store store;
extern volatile u32 core_machine_status;
extern u32 core_machine_file_bytes,core_machine_file_magic,core_machine_count,core_machine_pages;
extern volatile u32 core_machine_active,core_smz_failure;
extern volatile u32 core_machine_availability_seen,core_machine_last_track,core_machine_menu_limit;
extern u32 core_machine_paints;
extern volatile u32 core_machine_available_mask,core_machine_name_seen,core_machine_category_seen;
extern volatile u32 core_machine_vector_mask,core_machine_vector_count,core_machine_vector_calls;
extern int core_machine_present(u32);
static u32 loader_status[32];
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
            zero(&info,sizeof(info));info.type=8;info.kind=1;info.permissions=2;
            info.address=(u32)loader_status;info.bytes=sizeof(loader_status);info.id=1;info.tag=0xffffffffu;
            zero(out,8);r[0]=1;*(void **)(r+4)=fw_empty_string;fw_info_copy(r+8,&info);return 1;
        }
    }
    if(!path || !core_mods_path(*path))return 0;
    valid=core_mods_stat_io(&io,&n);
    if(valid<0){error(out,"/mods eMMC is unavailable",0x84);return 1;}
    zero(&info,sizeof(info));info.type=8;info.kind=2;
    info.permissions=valid==1?6:4;info.address=CORE_MODS_BASE;
    info.bytes=valid==1?n:0;info.id=1;info.tag=0xffffffffu;
    capture(&info.written,core_mods_written);capture(&info.validate,core_mods_validate);
    /* The native constructor clones only our inline captures through our
     * manager. No cached/borrowed std::function object is ever memcpy'd. */
    zero(out,8);r[0]=1;*(void **)(r+4)=fw_empty_string;
    fw_info_copy(r+8,&info);return 1;
}
int core_mods_write(void *router,const struct info *info,void *out)
{
    (void)router;
    if(info->written.manager!=(void *)core_fn_manager ||
       info->written.invoke!=(void *)core_mods_written)return 0;
    if(!core_mods_span(info->address,info->bytes) || info->type!=8 || info->kind!=2) {
        error(out,"/mods file exceeds 16 MiB or has an invalid destination",16);return 1;
    }
    /* This event is the WRITE factory, after the header's length replaces
     * info.bytes and before any native stream or body-sector write exists.
     * A shared prepare callback would also run on READ: do not use it here. */
    if(!core_mods_begin_io(&store,&io,info->bytes)) {
        error(out,"Failed to prepare /mods storage",16);return 1;
    }
    return 0; /* Continue into the stock bounded SafeMmcStreamWriter. */
}
void core_mods_finish(const struct fn *fn,const u8 *header,void *status)
{
    (void)header;
    if(fn->manager!=(void *)core_fn_manager || fn->invoke!=(void *)core_mods_written)return;
    if(!store.commit_ok) {
        *(u8 *)status=0;
        fw_string_assign((u8 *)status+4,"Failed to commit /mods metadata");
    }
}
