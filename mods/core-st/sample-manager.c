/* SPDX-License-Identifier: GPL-2.0-or-later
 * Core-owned pool/preview infrastructure. Optional SMZ owns native views only.
 * IO and CRC are task-only; the existing mix hook consumes published PCM.
 */
#include "sample-pool.h"
#include "machines.h"
#include "crc.h"
#include <syntakt/mix.h>
typedef unsigned int u32;typedef int s32;typedef unsigned short u16;typedef unsigned char u8;
#define DATA
extern void *fw_malloc(u32);extern void fw_free(void *);
extern int fw_mmc_read(u32,u32,void *);
#ifndef MALLOC
#define MALLOC fw_malloc
#define FREE fw_free
#define MMC_READ fw_mmc_read
#endif
#ifndef SLOT_READ
#define SLOT_READ ((s32 (*)(u32,void *,void *,u32,u32))0x4011320au)
#endif
#ifndef MMCFS
#define MMCFS ((u8 *(*)(void))0x4016f0ceu)
#endif
#ifndef MAIN_BUS
#define MAIN_BUS ((s32 *)ST_PREVIEW_BUS)
#endif
#define WORKING_PROJECT ((u8 *)0x40652514u)
#define SLOTS 128u
#define STOCK_SLOTS CORE_POOL_STOCK_SLOTS
#define HZ_FLAG 0x8000u
#define HZ_BLOCK_SECTORS 1024u
#define PREVIEW_CHUNK 0x8000u
#define PREVIEW_FRAMES (2*PREVIEW_CHUNK)
#define crc_update core_crc_update
static u32 service_be32(const void *v){const u8 *p=v;return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static u32 service_be16(const u8 *p){return (u32)p[0]<<8|p[1];}
static int service_heap_ok(const void *p){return (u32)p>=0x40000000u;}
static u8 *preview_pcm DATA = 0;
static u8 *preview_scratch DATA = 0;
static volatile u32 preview_length DATA = 0;
static volatile u32 preview_loaded DATA = 0;
static u32 preview_lba DATA = 0;
static u32 preview_crc DATA = 0, preview_expected_crc DATA = 0;
static u32 preview_stock DATA = 0;
static volatile u32 preview_pos;
static void service_preview_stop(void){preview_length=0;__asm__ volatile("" ::: "memory");}
void st_pool_preview_stop(void){service_preview_stop();}
static s32 preview_fetch(u32 total)
{
    u32 loaded=preview_loaded,frames=total-loaded,bytes,i,crc,at;
    if(frames>PREVIEW_CHUNK) frames=PREVIEW_CHUNK;
    bytes=2*frames;
    if(preview_stock) {
        if(SLOT_READ(preview_stock-1,preview_scratch,preview_scratch+64,2*loaded,bytes)<0) return -1;
    } else if(MMC_READ(preview_lba+(2*loaded)/512,(bytes+64+511)&~511u,preview_scratch)!=0) return -1;
    if(!loaded && (service_be32(preview_scratch)!=0x53414D50u || service_be32(preview_scratch+8)!=total)) return -1;
    crc=preview_stock?0:crc_update(preview_crc,preview_scratch+(loaded?64:0),bytes+(loaded?0:64));
    if(!preview_stock && loaded+frames==total && ~crc!=preview_expected_crc) return -2;
    at=(loaded&(PREVIEW_FRAMES-1))*2;
    for(i=0;i<bytes;i++) preview_pcm[at+i]=preview_scratch[64+i];
    preview_crc=crc;
    __asm__ volatile("" ::: "memory");
    preview_loaded=loaded+frames;
    return (s32)frames;
}

s32 manager_preview(u32 code)
{
    const u8 *e=0;u8 saved[64];struct st_hzfs hz; u8 *fs; u32 frames=0,i; s32 result;
    service_preview_stop();
    preview_stock=0;
    if(code>=1 && code<=STOCK_SLOTS) {
        fs=MMCFS();if(fs) frames=service_be32(fs+0x12E3C+(code-1)*24);
        if(frames>0x3FFE0u) frames=0;
        preview_stock=code;
    } else if((code&HZ_FLAG) && core_hzfs_open(&hz,&core_hzfs_native_io)==1) {
        u32 n=service_be32(hz.copy[hz.best]+12),k,j;
        for(k=0;k<n;k++){
            const u8 *row=hz.copy[hz.best]+ST_HZ_ENTRIES+64*k;
            if(service_be32(row)==(code&~HZ_FLAG)){for(j=0;j<64;j++)saved[j]=row[j];e=saved;frames=service_be32(e+16);break;}
        }
        core_hzfs_close(&hz);
    }
    if(!frames) {
         return -1;
    }
    if(!preview_pcm) {
        preview_pcm=MALLOC(2*PREVIEW_FRAMES);
        if(!service_heap_ok(preview_pcm)) { preview_pcm=0;  return -2; }
    }
    if(!preview_scratch) {
        preview_scratch=MALLOC(0x10200);
        if(!service_heap_ok(preview_scratch)) { preview_scratch=0;return -2; }
    }
    preview_pos=0;preview_loaded=0;preview_lba=e?service_be32(e+4)*HZ_BLOCK_SECTORS:0;
    preview_crc=0;preview_expected_crc=e?service_be32(e+20):0;
    for(i=0;i<2 && preview_loaded<frames;i++) {
        result=preview_fetch(frames);
        if(result<0) {
            return -3;
        }
    }
    __asm__ volatile("" ::: "memory");
    preview_length=frames;


    return (s32)frames;
}

void manager_refill(void)
{
    u32 n=preview_length,loaded=preview_loaded,p=preview_pos; s32 result;
    if(!n || loaded>=n || loaded-p>PREVIEW_CHUNK) return;
    result=preview_fetch(n);
    if(result<0) {
        service_preview_stop();return;
    }
}
static void preview_mix_frame(u32 frame,s32 x)
{
    st_preview_mix_frame(MAIN_BUS,frame,x);
}
void st_pool_preview_mix(void)
{
    u32 i,n=preview_length,p=preview_pos; s32 x;
    if(n && preview_pcm) {

        for(i=0;i<32 && p<n && p<preview_loaded;i++,p++) {
            x=(s32)(short)service_be16(preview_pcm+2*(p&(PREVIEW_FRAMES-1)))*2048;
            preview_mix_frame(i,x);
        }
        preview_pos=p;

        if(p==n) preview_length=0;
    }
}


void manager_preview_release(void)
{
    service_preview_stop();
    if(preview_pcm)FREE(preview_pcm);if(preview_scratch)FREE(preview_scratch);
    preview_pcm=preview_scratch=0;
}
static const struct st_pool_ui_callbacks *pool_ui;
static int bind_pool_ui(const struct st_pool_ui_callbacks *ui)
{if(!ui || ui->version!=1 || pool_ui)return 0;pool_ui=ui;return 1;}
void core_pool_ui_reset(void){manager_preview_release();pool_ui=0;}
void core_pool_menu(void *menu){if(pool_ui && pool_ui->menu)pool_ui->menu(menu);}
void core_pool_ui_progress(u32 done,u32 total){if(pool_ui && pool_ui->progress)pool_ui->progress(done,total);}
void core_pool_ui_file(u32 code,u32 ordinal,u32 bytes){if(pool_ui && pool_ui->file)pool_ui->file(code,ordinal,bytes);}
s32 st_pool_live_sync(const u16 *before)
{
    u16 now[SLOTS];u32 i,n=0;int result;
    core_pool_read(WORKING_PROJECT,now);
    for(i=0;i<SLOTS;i++)if(now[i] && now[i]!=before[i])n++;
    if(pool_ui && pool_ui->begin)pool_ui->begin(n);
    result=core_pool_sync();
    if(result!=1){core_pool_write(WORKING_PROJECT,before);core_pool_sync();}
    if(pool_ui && pool_ui->end)pool_ui->end();
    return result==-2?-6:result;
}
const struct st_sample_pool_api core_sample_pool_api={1,core_pool_valid,core_pool_sync,core_pool_cached_name,
    manager_preview,manager_refill,manager_preview_release,bind_pool_ui};
char *core_pool_format(u32 id,u32 raw)
{
    const struct core_machine_def *m;static char formatted[37];u16 entries[SLOTS];u32 index,i,code;u8 *fs;
    if(id<0x1f9 || id>=0x3f9)return 0;
    m=core_machine_find(64+(id-0x1f9)/32);index=(id-0x1f9)&31;
    if(!m || index>=m->params || m->parameter[index].alias!=0x1f2)return 0;
    core_pool_read(WORKING_PROJECT,entries);code=entries[(raw>>8)&127];
    if(core_pool_cached_name(code,formatted))return formatted;
    if(code && code<=64 && (fs=MMCFS())){
        for(i=0;i<16 && fs[0x12e28+(code-1)*24+i];i++)formatted[i]=(char)fs[0x12e28+(code-1)*24+i];formatted[i]=0;
    }else {const char *text=code?"MISSING":"EMPTY";for(i=0;text[i];i++)formatted[i]=text[i];formatted[i]=0;}
    return formatted;
}
