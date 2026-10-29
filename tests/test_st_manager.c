/* SPDX-License-Identifier: GPL-2.0-or-later
 * Production manager C on a 32-bit native host. Native UI/IO are doubles;
 * no target instructions, firmware routines or MIDI ports are executed.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void fake_init(void *,const char *,unsigned int,unsigned int,unsigned int,unsigned int);
static void fake_item(void *,void *,void *,void *,void *,int,unsigned int);
static void fake_clear(void *),fake_select(void *,unsigned int,unsigned int),fake_focus(void *,void *);
static void fake_dirty(void *),fake_assign(void *,const char *),fake_drop(void *),fake_dtor(void *);
static void fake_opaque(void *,unsigned int);
static unsigned int fake_push(void *,void *,unsigned int);
static unsigned char *fake_mmcfs(void);
static void *fake_qstr_init(void *,const char *,void *);
#define MMCFS fake_mmcfs
#define QSTR_INIT fake_qstr_init
static int bus[64];
#define UI_THREE_INIT fake_init
#define UI_ITEM_INIT fake_item
#define UI_LIST_CLEAR fake_clear
#define UI_LIST_SELECT fake_select
#define UI_THREE_FOCUS fake_focus
#define UI_DIRTY fake_dirty
#define UI_THREE_DTOR fake_dtor
#define QSTR_ASSIGN fake_assign
#define UI_PUSH fake_push
#define UI_DROP fake_drop
#define UI_OPAQUE fake_opaque
#define MAIN_BUS bus
#define CORE_ST_NATIVE_TEST
#include "../mods/core-st/sample-manager.c"
void st_pool_label_invoker(void){}

static u16 record[128];static u8 index_data[512],pcm_file[64+200000];
static void *allocations[1024];static u32 allocated,freed,rows[3],last_kind[3],syncs,io_reads;
static u32 empty_index,io_failure,ready=1,in_irq;
static u8 *fake_view,*manager,*node,*menu;
static u8 stock_names[0x12e28+64*24];
static u8 *fake_mmcfs(void){return stock_names;}
static void *fake_qstr_init(void *out,const char *text,void *alloc)
{(void)alloc;strcpy(out,text);return out;}
void *fw_malloc(u32 n)
{void *p;assert(!in_irq && allocated<1024);p=VirtualAlloc((void *)(0x47000000u+allocated*0x100000u),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(p);allocations[allocated++]=p;return p;}
void fw_free(void *p)
{u32 i;assert(!in_irq);for(i=0;i<allocated && allocations[i]!=p;i++){}assert(i<allocated);allocations[i]=0;assert(VirtualFree(p,0,MEM_RELEASE));freed++;}
int core_pool_valid(const u8 *p){(void)p;return 1;}
void core_pool_read(const u8 *p,u16 *out){(void)p;memcpy(out,record,sizeof(record));}
void core_pool_write(u8 *p,const u16 *in){(void)p;memcpy(record,in,sizeof(record));}
int core_pool_ready(void){return ready;}
int core_pool_sync(void){syncs++;return 1;}
int core_pool_cached_name(u32 code,char *out){(void)code;(void)out;return 0;}
const struct core_machine_def *core_machine_find(u32 type){(void)type;return 0;}
const struct st_storage_io core_hzfs_native_io={0};
int core_hzfs_open(struct st_hzfs *fs,const struct st_storage_io *io)
{(void)io;memset(fs,0,sizeof(*fs));fs->copy[0]=fw_malloc(512);memcpy(fs->copy[0],index_data,512);if(empty_index)put32(fs->copy[0]+12,0);fs->length[0]=512;return 1;}
void core_hzfs_close(struct st_hzfs *fs){u32 i;for(i=0;i<2;i++)if(fs->copy[i])fw_free(fs->copy[i]);}
int fw_mmc_read(u32 lba,u32 bytes,void *out)
{u32 at=(lba-(0x602u<<10))*512,n;assert(!in_irq && lba>=(0x602u<<10));io_reads++;if(io_failure)return -1;memset(out,0,bytes);n=at<sizeof(pcm_file)?sizeof(pcm_file)-at:0;if(n>bytes)n=bytes;memcpy(out,pcm_file+at,n);return 0;}
static u32 pane(void *v){assert(v==fake_view || v==fake_view+0x1c0 || v==fake_view+0x380);return v==fake_view?0:v==fake_view+0x1c0?1:2;}
static void fake_add(void *view,void *item){u32 p=pane(view);rows[p]++;last_kind[p]=*(u32 *)item;}
static void fake_init(void *v,const char *s,u32 n,u32 a,u32 b,u32 c)
{u32 i;(void)a;(void)b;(void)c;assert(!strcmp(s,"SAMPLE MANAGER") && n==128);fake_view=v;for(i=0;i<3;i++)*(u32 **)(fake_view+i*0x1c0)=(u32 *)0x40240df4u;*(u8 **)(fake_view+0x540)=fake_view;}
static void fake_item(void *item,void *label,void *action,void *icon,void *empty,int n,u32 flags)
{struct fn_capture *f=label;struct row_capture *c=f->data;(void)action;(void)icon;(void)empty;assert(n==-1 && flags==8);*(u32 *)item=c->kind;}
static void fake_clear(void *v){rows[pane(v)]=0;}
static void fake_select(void *v,u32 row,u32 flags){(void)flags;*(u32 *)((u8 *)v+16)=row;}
static void fake_focus(void *v,void *at){assert(v==fake_view);*(void **)(fake_view+0x540)=at;}
static void fake_dirty(void *p){(void)p;}
static void fake_assign(void *p,const char *s){(void)p;(void)s;}
static void fake_drop(void *p){(void)p;}
static void fake_opaque(void *p,u32 v){(void)p;assert(v==1);}
static void fake_dtor(void *p){assert(p==fake_view);}
static u32 fake_push(void *m,void *pair,u32 flags)
{(void)flags;assert(m==manager && *(void **)pair==fake_view+0x38);*(u8 **)(manager+0x1c)=node;*(u8 **)(node+8)=fake_view+0x38;return 1;}
static void key(u32 id,u32 flags){u8 ev[20]={0};put32(ev+12,id);put32(ev+16,flags);st_pool_ui_event(ev);}
static void close_view(void){u8 *v=ui_view;st_pool_ui_dispose(v);fake_view=0;assert(!ui_view && !preview_length && !ui_codes && !ui_selected && !ui_index);}
int main(void)
{
    u32 i,reads_before,allocs_before;struct row_capture capture={ROW_FILE,3,0};struct fn_capture src={&capture,0,row_manager,0},dst={0};
    assert(VirtualAlloc((void *)0x40240000u,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)0x40240000u);
    ((u32 *)0x40240decu)[4]=(u32)fake_add;
    menu=fw_malloc(256);manager=fw_malloc(256);node=fw_malloc(32);*(u8 **)(menu+0x64)=manager;
    row_manager(&dst,&src,2);assert(dst.data!=src.data && ((struct row_capture *)dst.data)->code==3);row_manager(&dst,0,3);
    /* Empty library has a real owned row, with logical count still zero. */
    empty_index=1;st_pool_ui_open(menu);assert(ui_mode==1 && !ui_count && rows[0]==1 && last_kind[0]==ROW_EMPTY && rows[1]==1 && rows[2]==1);
    key(12,1);assert(!ui_count);key(12,16);close_view();
    for(i=0;i<64;i++)record[i]=i+1;
    /* Exercise the real row formatter, not just the pool array. */
    for(i=0;i<64;i++){
        char label[48],expected[48];struct row_capture row={ROW_SLOT,i,0};
        struct fn_capture fn={&row,0,row_manager,0};
        snprintf((char *)stock_names+0x12e28+24*i,17,"STOCK %u",i+1);
        snprintf(expected,sizeof(expected),"%03u STOCK %u",i+1,i+1);
        st_pool_item_label(label,&fn);assert(!strcmp(label,expected));
    }
    /* Real library, native pane focus, multi-selection and batch actions. */
    put32(index_data+12,1);put32(index_data+0x140,1);put32(index_data+0x144,0x602);put32(index_data+0x150,100000);memcpy(index_data+0x160,"PAD",4);
    put32(pcm_file,0x53414d50);put32(pcm_file+8,100000);for(i=64;i<sizeof(pcm_file);i+=2){pcm_file[i]=0x10;pcm_file[i+1]=0;}
    put32(index_data+0x154,~crc_update(0,pcm_file,sizeof(pcm_file)));
    empty_index=0;st_pool_ui_open(menu);assert(ui_count==1 && rows[0]==1 && last_kind[0]==ROW_FILE);
    key(12,1);key(12,16);assert(ui_selected[0]);key(17,1);assert(*(void **)(ui_view+0x540)==ui_view+0x380);
    key(12,1);key(12,16);assert(record[64]==0x8001 && record[0]==1 && syncs==1 && !ui_selected[0]);
    key(16,1);assert(*(void **)(ui_view+0x540)==ui_view+0x1c0);key(12,1);key(12,16);assert(!ui_mode && rows[0]==128);
    /* Removing stock rows is refused in both batch and single-slot APIs. */
    key(12,1);key(12,16);assert(ui_pool_selected[0]);key(17,1);key(12,1);key(12,16);assert(record[64]==0x8001 && syncs==1);
    assert(!strcmp(ui_status,"STOCK SLOTS PROTECTED"));
    for(i=0;i<64;i++)assert(record[i]==i+1);
    assert(st_pool_ui_remove(0)==-1 && record[0]==1 && syncs==1);
    UI_THREE_FOCUS(ui_view,ui_view);UI_LIST_SELECT(ui_view,64,0);
    key(12,1);key(12,16);assert(ui_pool_selected[64]);key(17,1);key(12,1);key(12,16);assert(!record[64] && syncs==2);
    /* Single and batch insertion skip even empty stock rows in legacy pools. */
    record[0]=0;record[65]=0x8002;
    assert(st_pool_ui_add(0x8001)==65 && record[64]==0x8001 && !record[0] && syncs==3);
    assert(st_pool_ui_remove(64)==1 && !record[64] && syncs==4);
    ui_mode=1;ui_selected[0]=1;apply_selection();assert(record[64]==0x8001 && !record[0] && syncs==5);
    /* Full upper half never spills into a free lower-half position. */
    for(i=64;i<128;i++)record[i]=0x8002;
    assert(st_pool_ui_add(0x8001)==-3 && syncs==5 && !record[0]);
    ui_selected[0]=1;apply_selection();assert(!strcmp(ui_status,"POOL FULL") && syncs==5 && !record[0]);
    /* Hold FUNC+YES auditions independently, IRQ consumes only published PCM. */
    assert(st_pool_ui_preview(0x8001)==100000 && preview_length==100000 && io_reads==2);
    reads_before=io_reads;allocs_before=allocated;in_irq=1;st_pool_preview_mix();in_irq=0;
    assert(preview_pos==32 && bus[0]<0 && io_reads==reads_before && allocated==allocs_before);
    assert(ui_view_table[2+11]==((u32 *)0x40240EA0u)[2+11]);
    preview_pos=PREVIEW_CHUNK;core_pool_tick(manager);assert(preview_loaded==3*PREVIEW_CHUNK);
    preview_pos=2*PREVIEW_CHUNK;core_pool_tick(manager);assert(preview_loaded==100000);
    *(u8 **)(manager+0x1c)=manager+0x14;core_pool_tick(manager);assert(!preview_length && !ui_yes_down);
    *(u8 **)(manager+0x1c)=node;assert(st_pool_ui_preview(0x8001)==100000);
    key(12,16);assert(!preview_length);io_failure=1;assert(st_pool_ui_preview(0x8001)<0 && !preview_length);io_failure=0;
    close_view();core_pool_tick(manager);for(i=0;i<allocated;i++)if(allocations[i])fw_free(allocations[i]);
    assert(freed==allocated);assert(VirtualFree((void *)0x40240000u,0,MEM_RELEASE));
    puts("PASS: native-manager C empty row, pane navigation, owned captures, batch pool edits, audition streaming and bounded IRQ mixing");return 0;
}
