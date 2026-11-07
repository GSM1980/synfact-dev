/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native 32-bit host checks of the production parser and boot orchestration.
 * Target callbacks/assembly/LCD/eMMC are NOT executed. No instruction emulator.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void sample_notify(unsigned int);
extern volatile unsigned int core_smz_failure;
#define CORE_SAMPLE_NOTIFY sample_notify
#include "../mods/core-st/machine-boot.c"
static u32 notifications[256],notification_count;
static void sample_notify(u32 percent){assert(notification_count<256);notifications[notification_count++]=percent;}
static u8 *disk;static u32 bytes,stat_result=1,reads,allocs,fail_alloc,fail_load,bind_ok=1,paints,expected_count=1;
static int select_result,fallback_enabled,preflight_ok=1;static u32 slot_state,transitions,preflights;static u8 *candidate;
static u8 pixels_a[1024],pixels_b[1024];
u32 fw_mmc_ready[]={1},fw_mmc_sectors[]={CORE_MODS_LBA+1};
u32 fw_lcd_front[1],fw_lcd_back[1],fw_font5[1];
const u32 core_machine_audio_abi[]={CORE_SMZ_ABI};
int core_pool_available(void){return 1;}
volatile u32 core_machine_active;
const struct st_machine_ui *core_machine_ui_definitions;
int core_modules_bind(const struct st_smz_module *m,u32 n){assert(m && n);return 1;}
int core_mods_stat_io(const struct core_mods_io *io,u32 *size)
{(void)io;*size=bytes;return stat_result;}
int core_mods_select_io(const struct core_mods_io *io,struct core_mods_slot *primary,struct core_mods_slot *backup)
{(void)io;backup->bytes=0;if(select_result==1){primary->base=CORE_MODS_BASE;primary->bytes=bytes;primary->slot=0;primary->sequence=7;primary->state=slot_state;
 if(fallback_enabled){backup->base=CORE_MODS_BASE+CORE_MODS_SLOT_BYTES;backup->bytes=bytes;backup->slot=1;backup->state=CORE_MODS_GOOD;}}
 return select_result;}
int core_mods_slot_io(const struct core_mods_io *io,u32 slot,struct core_mods_slot *out)
{(void)io;(void)slot;(void)out;return 0;}
int core_mods_transition_io(const struct core_mods_io *io,u32 slot,u32 from,u32 to)
{(void)io;assert(slot==0 && from==CORE_MODS_CANDIDATE && to==CORE_MODS_TRYING);transitions++;slot_state=to;return 1;}
int core_boot_preimages_ok(const core_u8 *info)
{assert(info);preflights++;return preflight_ok;}
void core_native_initialize(void){}
int fw_mmc_read(u32 lba,u32 n,void *out)
{
    u32 at=(lba<<9)-CORE_MODS_BASE;u8 *source;reads++;
    assert(at<CORE_MODS_MAX && n<=CORE_MODS_MAX-at);
    if(fail_load && allocs>=2)return -1;
    source=at<CORE_MODS_SLOT_BYTES?(candidate?candidate:disk):disk;
    at%=CORE_MODS_SLOT_BYTES;
    memset(out,0,n);if(at<bytes){u32 copy=bytes-at;if(copy>n)copy=n;memcpy(out,source+at,copy);}
    return 0;
}
void *fw_malloc(u32 n)
{allocs++;return allocs==fail_alloc?0:VirtualAlloc((void *)(0x46000000u+0x1000000u*allocs),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}
void fw_free(void *p){assert(VirtualFree(p,0,MEM_RELEASE));}
void core_machine_flush(void *p,u32 n){assert(p && n);}
int core_machine_bind(const struct core_machine_def *d,u32 n)
{assert(d && n==expected_count);core_machine_active=bind_ok;return bind_ok;}
void fw_bitmap_ctor(void *b,u32 w,u32 h,void *p,void *stride)
{(void)b;(void)stride;assert(w==128 && h==64 && p==(void *)fw_lcd_front[0]);}
void fw_bitmap_dtor(void *b){(void)b;}
void fw_fillrect(void *b,int x0,int y0,int x1,int y1,u32 color)
{(void)b;(void)color;assert(x0<=x1 && y0<=y1 && x0>=0 && x1<128 && y0>=0 && y1<64);}
void fw_textf(void *b,void *font,int x,int y,u32 color,const char *format,...)
{
    va_list args;(void)b;(void)font;(void)x;(void)y;(void)color;
    (void)args;(void)format;paints++;assert(0 && "SMZ loading must stay silent");
}
void *fw_lcd_swap(void)
{u32 t=fw_lcd_front[0];fw_lcd_front[0]=fw_lcd_back[0];fw_lcd_back[0]=t;return (void *)fw_lcd_front[0];}
static void reset(void)
{
    if(retained)fw_free(retained);retained=0;core_machine_active=core_machine_pages=0;
    allocs=reads=paints=fail_alloc=fail_load=transitions=preflights=0;bind_ok=stat_result=preflight_ok=1;select_result=fallback_enabled=0;slot_state=0;candidate=0;
    memset(pixels_a,0x55,1024);memset(pixels_b,0xaa,1024);
    fw_lcd_front[0]=(u32)pixels_a;fw_lcd_back[0]=(u32)pixels_b;
}
static void unchanged(void)
{
    u32 i;assert(fw_lcd_front[0]==(u32)pixels_a && fw_lcd_back[0]==(u32)pixels_b);
    for(i=0;i<1024;i++){assert(pixels_a[i]==0x55);assert(pixels_b[i]==0xaa);}
}
static void write32(u8 *p,u32 n){p[0]=n>>24;p[1]=n>>16;p[2]=n>>8;p[3]=n;}
static void checksums(u8 *file,const struct core_smz_view *v)
{
    u32 i,sum=0;
    for(i=80;i<v->image+v->bytes;i+=4)sum+=core_smz_be(file+i);
    write32(file+68,sum);write32(file+16,core_smz_adler(1,file+20,bytes-20));
}
int main(int argc,char **argv)
{
    FILE *f;struct core_smz_reader r;struct core_smz_view v;u8 *bad;u32 at;
    assert(argc==2);f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);bytes=(u32)ftell(f);rewind(f);
    for(at=0;at<6;at++)core_sample_progress(at,6);
    assert(notification_count==7 && notifications[0]==0 && notifications[1]==16 && notifications[6]==100);
    notification_count=0;core_sample_progress(0,0);assert(!notification_count);
    disk=malloc(bytes);assert(fread(disk,1,bytes,f)==bytes);fclose(f);
    r.context=disk;r.read=core_smz_memory_read;r.bytes=bytes;
    if(!core_smz_validate(&r,&v)){fprintf(stderr,"Parser rejected bank at smz.c:%u\n",core_smz_failure);return 1;}
    assert(v.contract==CORE_SMZ_CONTRACT);expected_count=v.count;
    bad=malloc(bytes);r.context=bad;memcpy(bad,disk,bytes);
    bad[8]^=1;assert(!core_smz_validate(&r,&v));memcpy(bad,disk,bytes);
    bad[bytes-1]^=1;assert(!core_smz_validate(&r,&v));memcpy(bad,disk,bytes);
    r.context=disk;assert(core_smz_validate(&r,&v));r.context=bad;
    write32(bad+64,CORE_SMZ_CONTRACT+1);checksums(bad,&v);
    assert(!core_smz_validate(&r,&v));memcpy(bad,disk,bytes);
    /* A valid checksum never authorizes bad slots, relocation offsets,
     * cross-CPU entry points or the four not-yet-backed control positions. */
    at=v.image+v.descriptors+4;write32(bad+at,32);checksums(bad,&v);assert(!core_smz_validate(&r,&v));
    memcpy(bad,disk,bytes);write32(bad+80,v.bytes-2);checksums(bad,&v);assert(!core_smz_validate(&r,&v));
    memcpy(bad,disk,bytes);write32(bad+v.audio+20,CORE_SMZ_AUDIO-2);checksums(bad,&v);assert(!core_smz_validate(&r,&v));
    memcpy(bad,disk,bytes);write32(bad+24,0x40000538);checksums(bad,&v);assert(!core_smz_validate(&r,&v));
    /* Every native category is valid with freshly computed checksums. Reject
     * zero, the first unsupported category and values that truncate to a valid
     * byte: the file stores a full word, not an unchecked renderer byte. */
    for(at=1;at<=14;at++){
        memcpy(bad,disk,bytes);write32(bad+v.image+v.descriptors+32,at);
        checksums(bad,&v);assert(core_smz_validate(&r,&v));
    }
    {const u32 invalid[]={0,15,0x100,0x105,0xffffffffu};u32 i;
     for(i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){
        memcpy(bad,disk,bytes);write32(bad+v.image+v.descriptors+32,invalid[i]);
        checksums(bad,&v);assert(!core_smz_validate(&r,&v));
     }}
    reset();stat_result=0;core_machine_load();assert(core_machine_status==1 && !reads && !paints && !allocs);unchanged();
    reset();disk[8]^=1;core_machine_load();disk[8]^=1;assert(core_machine_status==2 && !paints && !allocs);unchanged();
    reset();fail_alloc=1;core_machine_load();assert(core_machine_status==4 && !paints);unchanged();
    reset();fail_alloc=2;core_machine_load();assert(core_machine_status==4 && !paints);unchanged();
    reset();fail_load=1;core_machine_load();assert(core_machine_status==3 && !core_machine_active && !core_machine_pages);unchanged();
    reset();preflight_ok=0;core_machine_load();
    assert(core_machine_status==11 && preflights==1 && !core_machine_active && !core_machine_pages && !retained);unchanged();
    reset();bind_ok=0;core_machine_load();assert(core_machine_status==5 && !core_machine_active && !core_machine_pages && retained);unchanged();
    reset();r.context=disk;assert(core_smz_validate(&r,&v));
    write32(disk+64,CORE_SMZ_CONTRACT+1);checksums(disk,&v);
    assert(core_smz_validate(&r,&v)==0);
    core_machine_load();assert(core_machine_status==10 && !core_machine_active && !allocs);unchanged();
    write32(disk+64,CORE_SMZ_CONTRACT);checksums(disk,&v);assert(core_smz_validate(&r,&v));
    reset();select_result=1;slot_state=CORE_MODS_CANDIDATE;core_machine_load();
    assert(core_machine_status==0 && core_machine_trial==1 && core_machine_trial_sequence==7 &&
           transitions==1 && slot_state==CORE_MODS_TRYING && core_machine_active);unchanged();
    reset();select_result=fallback_enabled=1;slot_state=CORE_MODS_CANDIDATE;
    candidate=bad;memcpy(bad,disk,bytes);write32(bad+64,CORE_SMZ_CONTRACT+1);
    r.context=disk;assert(core_smz_validate(&r,&v));checksums(bad,&v);
    core_machine_load();assert(core_machine_status==0 && core_machine_active && !core_machine_trial &&
                               core_machine_rollback && transitions==1 && slot_state==CORE_MODS_TRYING);unchanged();
    reset();core_machine_load();assert(core_machine_status==0 && core_machine_active && core_machine_pages && !paints && !core_machine_paints);unchanged();
    at=allocs;core_machine_load();assert(allocs==at);reset();free(bad);free(disk);
    puts("PASS: SMZ validation, absent/invalid skips, memory/IO/init failures, silent load, native sample progress, activation and once-only load");return 0;
}
