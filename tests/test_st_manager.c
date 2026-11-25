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
static void *fake_font(void *);
static void fake_text(void *,void *,int,int,unsigned int,const char *);
static void fake_fill(void *,int,int,int,int,unsigned int),fake_draw(void *,void *);
#define UI_FONT fake_font
#define UI_TEXT fake_text
#define UI_FILL fake_fill
#define UI_NATIVE_DRAW fake_draw
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
#include "../mods/sample-manager-st/ui.c"
#include "../mods/core-st/sample-manager.c"
void st_pool_label_invoker(void){}

static u16 record[128];static u8 index_data[512],pcm_file[64+200000];
static void *allocations[1024];static u32 allocated,freed,rows[3],last_kind[3],syncs,io_reads;
static u32 empty_index,io_failure,ready=1,in_irq,sync_failure;
static u8 *fake_view,*manager,*node,*menu;
static u8 stock_names[0x12e28+64*24];
static u32 test_font[4]={3,0,0,0},texts;static char painted[4][44];static s32 paint_x[4],paint_y[4];
static void *fake_font(void *p){(void)p;return test_font;}
static void fake_text(void *ctx,void *font,s32 x,s32 y,u32 max,const char *s)
{(void)ctx;(void)font;(void)max;assert(texts<4);strcpy(painted[texts],s);paint_x[texts]=x;paint_y[texts++]=y;}
static void fake_fill(void *ctx,s32 a,s32 b,s32 c,s32 d,u32 color)
{(void)ctx;(void)a;(void)b;(void)c;(void)d;(void)color;}
static void fake_draw(void *view,void *ctx){(void)ctx;assert(view==ui_view);}
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
int core_pool_sync(void){syncs++;if(sync_failure){sync_failure=0;return -2;}return 1;}
int core_pool_cached_name(u32 code,char *out){(void)code;(void)out;return 0;}
void core_pool_memory(struct st_pool_memory *out)
{u32 i;memset(out,0,sizeof(*out));out->total=CORE_POOL_END-CORE_POOL_PCM;
 for(i=0;i<128;i++)if(record[i]){out->bytes[i]=record[i]&HZ_FLAG?200000:1000;
 if(i>=64 && record[i]&HZ_FLAG)out->used+=200704;}}
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
{u32 i;(void)a;(void)b;(void)c;assert(!strcmp(s,"SAMPLE MANAGER") && n==128);fake_view=v;for(i=0;i<3;i++)*(u32 **)(fake_view+i*0x1c0)=(u32 *)0x40241114u;*(u8 **)(fake_view+0x540)=fake_view;*(u32 *)(fake_view+0x1a0)=124;}
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
static void pre(u32 id,u32 flags){u8 ev[20]={0};put32(ev+12,id);put32(ev+16,flags);assert(!st_pool_ui_pre_event(0,ev));}
static void close_view(void){u8 *v=ui_view;st_pool_ui_dispose(v);fake_view=0;assert(!ui_view && !preview_length && !ui_codes && !ui_selected && !ui_index);}
int main(void)
{
    u32 i,reads_before,allocs_before;struct row_capture capture={ROW_FILE,3,0};struct fn_capture src={&capture,0,row_manager,0},dst={0};
    assert(VirtualAlloc((void *)0x40240000u,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)0x40240000u);
    ((u32 *)0x4024110cu)[4]=(u32)fake_add;
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
    assert(ui_memory.total==66711552u && !ui_memory.used);
    {char label[48],size[16];struct row_capture c={ROW_FILE,0,0};struct fn_capture fn={&c};
     assert(row_size(&c,size) && !strcmp(size,"0.19MB"));
     memset(ui_index+0x160,'W',32);st_pool_item_label(label,&fn);
     assert(strstr(label,"...") && text_width(test_font,label)+3+text_width(test_font,size)<=row_width());
     /* Core 2.8: a canonical wavetable reads WT before its name; a wrong count does not. */
     memcpy(ui_index+0x160,"PAD\0",4);put32(ui_index+0x15c,ST_HZ_WAVETABLE);put32(ui_index+0x150,2*ST_WT_FRAMES);
     st_pool_item_label(label,&fn);assert(!strcmp(label,"--- WT PAD"));
     put32(ui_index+0x150,2*ST_WT_FRAMES-2);st_pool_item_label(label,&fn);assert(!strcmp(label,"--- PAD"));
     put32(ui_index+0x15c,ST_HZ_STEREO|ST_HZ_WAVETABLE);put32(ui_index+0x150,2*ST_WT_FRAMES);
     st_pool_item_label(label,&fn);assert(!strcmp(label,"--- ST PAD"));
     put32(ui_index+0x15c,0);put32(ui_index+0x150,100000);
     texts=0;st_pool_row_size_draw(&fn,0,0,8,42);
     assert(texts==1 && !strcmp(painted[0],"0.19MB") && paint_x[0]+text_width(test_font,size)==8+row_width());
     texts=0;st_pool_ui_draw(ui_view+0x38,0);assert(texts==3 && paint_x[0]==2);
     assert(!strcmp(painted[0],"SAMPLE MANAGER") && !strcmp(painted[1],"Project MEM"));
     assert(!strcmp(painted[2],"(0.00/63.62) MB") && paint_x[2]+text_width(test_font,painted[2])==126);
     assert(paint_y[1]==58 && paint_y[2]==50);
     ui_message("AUDIO FULL");texts=0;st_pool_ui_draw(ui_view+0x38,0);
     assert(texts==4 && !strcmp(painted[3],"AUDIO FULL") && paint_x[3]==2 && paint_y[3]==50);
     ui_message("SAMPLE MANAGER");}
    {char text[16];memory_mb(text,41943);assert(!strcmp(text,"0.04"));
     memory_mb(text,1488978);assert(!strcmp(text,"1.42"));
     memory_mb(text,66711552);assert(!strcmp(text,"63.62"));
     memory_mb(text,1048575);assert(!strcmp(text,"1.00"));}
    key(12,1);key(12,16);assert(ui_selected[0]);key(17,1);assert(*(void **)(ui_view+0x540)==ui_view+0x380);
    key(12,1);key(12,16);assert(record[64]==0x8001 && record[0]==1 && syncs==1 && !ui_selected[0]);
    assert(ui_memory.used==200704 && ui_memory.bytes[64]==200000);
    key(16,1);assert(*(void **)(ui_view+0x540)==ui_view+0x1c0);key(12,1);key(12,16);assert(!ui_mode && rows[0]==128);
    /* Removing stock rows is refused in both batch and single-slot APIs. */
    key(12,1);key(12,16);assert(ui_pool_selected[0]);key(17,1);key(12,1);key(12,16);assert(record[64]==0x8001 && syncs==1);
    assert(!strcmp(ui_status,"STOCK SLOTS PROTECTED"));
    for(i=0;i<64;i++)assert(record[i]==i+1);
    assert(st_pool_ui_remove(0)==-1 && record[0]==1 && syncs==1);
    UI_THREE_FOCUS(ui_view,ui_view);UI_LIST_SELECT(ui_view,64,0);
    key(12,1);key(12,16);assert(ui_pool_selected[64]);key(17,1);key(12,1);key(12,16);assert(!record[64] && syncs==2);
    assert(!ui_memory.used && !ui_memory.bytes[64]);
    /* FUNC+UP/DOWN steps eight rows until a FUNC+YES audition of a sample,
     * then one row while FUNC stays held; FUNC repeats keep the fine step. */
    UI_THREE_FOCUS(ui_view,ui_view);UI_LIST_SELECT(ui_view,0,0);
    key(15,3);assert(*(u32 *)(ui_view+16)==8);key(15,3);assert(*(u32 *)(ui_view+16)==16);
    key(12,3);assert(ui_fine_step && ui_yes_down);key(12,0x12);assert(!ui_yes_down);
    key(15,3);assert(*(u32 *)(ui_view+16)==17);key(12,3);key(12,0x12);pre(1,9);
    key(15,3);assert(*(u32 *)(ui_view+16)==18);key(14,3);assert(*(u32 *)(ui_view+16)==17);
    pre(1,0x10);assert(!ui_fine_step);key(15,3);assert(*(u32 *)(ui_view+16)==25);
    /* A fresh FUNC press, or a key pressed without FUNC, also ends it. */
    key(12,3);key(12,0x12);pre(1,1);assert(!ui_fine_step);key(14,3);assert(*(u32 *)(ui_view+16)==17);
    key(12,3);key(12,0x12);key(15,1);assert(!ui_fine_step && *(u32 *)(ui_view+16)==18);
    key(15,3);assert(*(u32 *)(ui_view+16)==26);
    /* An empty slot is not an audition: the step stays at eight. */
    pre(1,0x10);UI_LIST_SELECT(ui_view,64,0);key(12,3);key(12,0x12);assert(!ui_fine_step);
    key(14,3);assert(*(u32 *)(ui_view+16)==56);
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
    assert(ui_view_table[2+11]==((u32 *)0x402411c0u)[2+11]);
    preview_pos=PREVIEW_CHUNK;manager_tick(manager);assert(preview_loaded==3*PREVIEW_CHUNK);
    preview_pos=2*PREVIEW_CHUNK;manager_tick(manager);assert(preview_loaded==100000);
    *(u8 **)(manager+0x1c)=manager+0x14;manager_tick(manager);assert(!preview_length && !ui_yes_down);
    *(u8 **)(manager+0x1c)=node;assert(st_pool_ui_preview(0x8001)==100000);
    key(12,16);assert(!preview_length);io_failure=1;assert(st_pool_ui_preview(0x8001)<0 && !preview_length);io_failure=0;
    close_view();
    /* Empty and nested folders navigate without allocating PCM or changing
     * the numeric project pool. Root rows are [Drums], then parent/[Acoustic]. */
    put32(index_data+4,2);put32(index_data+40,2);put32(index_data+44,3);
    put32(index_data+0x180,2); /* membership after one sample */
    put32(index_data+0x184,1);memcpy(index_data+0x18c,"Drums",6);
    put32(index_data+0x1b4,2);put32(index_data+0x1b8,1);memcpy(index_data+0x1bc,"Acoustic",9);
    st_pool_ui_open(menu);assert(ui_count==1 && ui_codes[0]==1);
    {char label[48],size[16];struct row_capture c={ROW_FILE,0,0};struct fn_capture fn={&c};
     st_pool_item_label(label,&fn);assert(!strcmp(label,"[Drums]") && !row_size(&c,size));}
    {struct row_capture c={ROW_FILE,0,0};struct fn_capture fn={&c};u16 before[128];
     memcpy(before,record,sizeof before);row_action(&fn);assert(ui_folder==1 && ui_count==2 && ui_codes[0]==UI_PARENT_FOLDER && ui_codes[1]==2);
     c.code=1;row_action(&fn);assert(ui_folder==2 && ui_count==2 && ui_codes[1]==0x8001);
     c.code=0;row_action(&fn);assert(ui_folder==1);row_action(&fn);assert(ui_folder==0);
     assert(!memcmp(before,record,sizeof before));}
    close_view();
    /* Both single and batch out-of-memory errors restore the entire record
     * and refresh the meter from the final pool, without counting additions. */
    memset(record+64,0,64*sizeof(*record));st_pool_ui_open(menu);
    {u16 before[128];u32 added=st_pool_ui_report[1],calls=syncs;memcpy(before,record,sizeof before);
     sync_failure=1;assert(st_pool_ui_add(0x8001)==-6 && !strcmp(ui_status,"AUDIO FULL"));
     assert(!memcmp(before,record,sizeof before) && !ui_memory.used && st_pool_ui_report[1]==added);
     assert(syncs==calls+1);calls=syncs;
     ui_folder=2;library_list();ui_selected[1]=1;sync_failure=1;apply_selection();
     assert(!strcmp(ui_status,"AUDIO FULL") && !memcmp(before,record,sizeof before));
     assert(!ui_memory.used && st_pool_ui_report[1]==added && syncs==calls+1);}
    close_view();manager_tick(manager);for(i=0;i<allocated;i++)if(allocations[i])fw_free(allocations[i]);
    assert(freed==allocated);assert(VirtualFree((void *)0x40240000u,0,MEM_RELEASE));
    puts("PASS: native-manager C empty row, pane navigation, owned captures, batch pool edits, audition streaming and bounded IRQ mixing");return 0;
}
