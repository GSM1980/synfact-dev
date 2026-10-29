/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native three-pane project sample manager. Owned views/captures only;
 * the task streams audition PCM and the MAIN mix event consumes it.
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
#endif
#ifndef FREE
#define FREE fw_free
#endif
#ifndef MMC_READ
#define MMC_READ fw_mmc_read
#endif
#ifndef SLOT_READ
#define SLOT_READ ((s32 (*)(u32,void *,void *,u32,u32))0x4011320au)
#endif
#ifndef MMCFS
#define MMCFS ((u8 *(*)(void))0x4016f0ceu)
#endif
#ifndef QSTR_ASSIGN
#define QSTR_ASSIGN ((void (*)(void *,const char *))0x401bde32u)
#endif
#define WORKING_PROJECT ((u8 *)0x40652514u)
#define SLOTS 128u
#define STOCK_SLOTS CORE_POOL_STOCK_SLOTS
#define HZ_FLAG 0x8000u
#define HZ_BLOCK_SECTORS 1024u
#define HZ_ENTRIES 0x140u
#define HZ_ENTRY 64u
/* Waveform glyph in native column format, expressed entirely as C data. */
#define SAMPLE_MENU_ICON_COLUMNS {0,0x7fc00000u,0,0x1f000000u,0,0x0e000000u,0,0x04000000u,0,0x04000000u,0,0}
#define SAMPLE_MENU_ICON_MASK {0xffe00000u,0xffe00000u,0xffe00000u,0xffe00000u,0xffe00000u,0xffe00000u,0xffe00000u,0xffe00000u,0xffe00000u,0xffe00000u,0xffe00000u,0xffe00000u}
static u16 st_pool_now[SLOTS];
static u32 be32(const void *data){const u8 *p=data;return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static u32 be16(const u8 *p){return (u32)p[0]<<8|p[1];}
static void put32(u8 *p,u32 v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
#define crc_update core_crc_update
static int heap_ok(const void *p){return (u32)p>=0x40000000u;}
static void name_copy(char *d,const u8 *s,u32 n){u32 i;for(i=0;i<n && s[i];i++)d[i]=s[i]>=32 && s[i]<127?s[i]:'?';d[i]=0;}
static int st_pool_read(const u8 *p,u16 *entries){int valid=core_pool_valid(p);core_pool_read(p,entries);return valid;}
#define st_pool_write core_pool_write
#define st_pool_live_ready core_pool_ready
s32 st_pool_live_sync(const u16 *before);
static const u8 *hz_find(const u8 *index,u32 length,u32 id)
{u32 i;const u8 *e;if(!index || length<ST_HZ_ENTRIES || !id || id>32767)return 0;
 for(i=0;i<be32(index+12);i++){e=index+ST_HZ_ENTRIES+64*i;if(be32(e)==id)return e;}return 0;}
static u8 *hz_load(u32 *length)
{struct st_hzfs fs;u8 *p;*length=0;if(core_hzfs_open(&fs,&core_hzfs_native_io)!=1)return 0;
 p=fs.copy[fs.best];*length=fs.length[fs.best];fs.copy[fs.best]=0;core_hzfs_close(&fs);return p;}
#ifndef UI_LIST_INIT
#define UI_LIST_INIT ((void (*)(void *,const char *,u32,s32,s32,s32,s32,u32,u32))0x400F521Cu)
#endif
#ifndef UI_ITEM_INIT
#define UI_ITEM_INIT ((void (*)(void *,void *,void *,void *,void *,s32,u32))0x400FA688u)
#endif
#ifndef UI_LIST_CLEAR
#define UI_LIST_CLEAR ((void (*)(void *))0x400F9FE0u)
#endif
#ifndef UI_LIST_SELECT
#define UI_LIST_SELECT ((void (*)(void *,u32,u32))0x400F9B88u)
#endif
#ifndef UI_THREE_INIT
#define UI_THREE_INIT ((void (*)(void *,const char *,u32,u32,u32,u32))0x400FABBEu)
#endif
#ifndef UI_THREE_FOCUS
#define UI_THREE_FOCUS ((void (*)(void *,void *))0x400FAB74u)
#endif
#ifndef UI_THREE_DTOR
#define UI_THREE_DTOR ((void (*)(void *))0x40184DFCu)
#endif
#ifndef UI_LIST_DTOR
#define UI_LIST_DTOR ((void (*)(void *))0x40184CFAu)
#endif
#ifndef UI_PUSH
#define UI_PUSH ((u32 (*)(void *,void *,u32))0x40100BC2u)
#endif
#ifndef UI_DROP
#define UI_DROP ((void (*)(void *))0x4016DDE8u)
#endif
#ifndef UI_CLOSE
#define UI_CLOSE ((void (*)(void *))0x400FFD02u)
#endif
#ifndef UI_DIRTY
#define UI_DIRTY ((void (*)(void *))0x400FFC5Eu)
#endif
#ifndef UI_OPAQUE
#define UI_OPAQUE ((void (*)(void *,u32))0x400FFAF4u)
#endif
#ifndef QSTR_INIT
#define QSTR_INIT ((void *(*)(void *,const char *,void *))0x401BF082u)
#endif
#ifndef UI_ROOT
#define UI_ROOT (*(u8 *volatile *)0x444E1334u)
#endif
#define PREVIEW_CHUNK 0x8000u
#define PREVIEW_FRAMES (2*PREVIEW_CHUNK)
#ifndef MAIN_BUS
#define MAIN_BUS ((s32 *)ST_PREVIEW_BUS)
#endif

enum { ROW_OPEN, ROW_VIEW, ROW_APPLY, ROW_FILE, ROW_SLOT, ROW_EMPTY };
struct row_capture { u32 kind, code; u8 *menu; };
struct fn_capture { void *data; u32 reserved; void *manager, *invoke; };
static u8 *ui_view DATA = 0;
static u8 *ui_manager DATA = 0;
static u8 *ui_index DATA = 0;
static u32 ui_index_len DATA = 0, ui_mode DATA = 0, ui_count DATA = 0;
static u16 *ui_codes DATA = 0;
static u8 *ui_selected DATA = 0;
static u8 ui_pool_selected[SLOTS] DATA = {0};
static u32 ui_table[45] DATA = {1};
static u32 ui_view_table[19] DATA = {1};
static char ui_status[40] DATA = "YES ADD  FUNC+NO REMOVE";
/* Publication to the audio IRQ is a single aligned volatile length store,
 * performed only after the buffer and cursor are ready. Stop disables it before
 * freeing/reusing PCM. No allocation or eMMC reads occur in the IRQ. */
static u8 *preview_pcm DATA = 0;
static u8 *preview_scratch DATA = 0;
static volatile u32 preview_length DATA = 0;
static volatile u32 preview_loaded DATA = 0;
static u32 preview_lba DATA = 0;
static u32 preview_crc DATA = 0, preview_expected_crc DATA = 0;
static u32 preview_stock DATA = 0;
static u32 ui_combo_keys DATA = 0;
static u32 ui_yes_down DATA = 0;
static u32 ui_loading DATA = 0, ui_progress_done DATA = 0, ui_progress_total DATA = 0;
static u32 ui_progress_count DATA = 0, ui_progress_percent DATA = 101;
static char ui_progress_label[40] DATA = {0};
static volatile u32 preview_pos DATA = 0;
u32 st_pool_ui_report[8] DATA = {0};

static void text_set(char *dst,const char *src,u32 max)
{
    u32 i; for(i=0;i<max && src[i];i++) dst[i]=src[i]; dst[i]=0;
}
static void number3(char *dst,u32 n)
{
    dst[0]='0'+(n/100)%10; dst[1]='0'+(n/10)%10; dst[2]='0'+n%10;
}
static void row_name(char *text,u32 code);
static void preview_stop(void);
static void ui_redraw(void)
{
    if(ui_view) UI_DIRTY(ui_view+0x38);
}
static void ui_message(const char *s)
{
    text_set(ui_status,s,39);
    if(ui_view) QSTR_ASSIGN(ui_view+0x1BC,s);
    ui_redraw();
}
static u32 code_slot(u32 code)
{
    u32 i; st_pool_read(WORKING_PROJECT,st_pool_now);
    for(i=0;i<SLOTS;i++) if(st_pool_now[i]==code && code) return i+1;
    return 0;
}

/* Live edits publish descriptors only after AUDIO confirms the PCM transfer. */
s32 st_pool_ui_add(u32 code)
{
    u32 i,j;u16 before[SLOTS];s32 result;
    if(!st_pool_live_ready()) { ui_message("AUDIO NOT READY");return -4; }
    if(!(code&HZ_FLAG) || !ui_index || !hz_find(ui_index,ui_index_len,code&~HZ_FLAG)) {
        ui_message("SAMPLE MISSING"); return -1;
    }
    if(code_slot(code)) { ui_message("ALREADY IN POOL"); return -2; }
    for(j=0;j<SLOTS;j++) before[j]=st_pool_now[j];
    for(i=STOCK_SLOTS;i<SLOTS;i++) if(!st_pool_now[i]) {
        st_pool_now[i]=(u16)code; st_pool_write(WORKING_PROJECT,st_pool_now);
        preview_stop();result=st_pool_live_sync(before);
        if(result!=1) { ui_message(result==-6?"AUDIO FULL":"LOAD FAILED");return result; }
        st_pool_ui_report[1]++; ui_message("ADDED - SAVE PROJECT"); return (s32)i+1;
    }
    ui_message("POOL FULL (65-128)"); return -3;
}
s32 st_pool_ui_remove(u32 slot)
{
    u32 j;u16 before[SLOTS];s32 result;
    if(slot>=SLOTS) return -1;
    if(slot<STOCK_SLOTS) { ui_message("STOCK SLOT PROTECTED");return -1; }
    if(!st_pool_live_ready()) { ui_message("AUDIO NOT READY");return -4; }
    st_pool_read(WORKING_PROJECT,st_pool_now);
    if(!st_pool_now[slot]) { ui_message("SLOT EMPTY"); return 0; }
    for(j=0;j<SLOTS;j++) before[j]=st_pool_now[j];
    st_pool_now[slot]=0; st_pool_write(WORKING_PROJECT,st_pool_now);
    preview_stop();result=st_pool_live_sync(before);
    if(result!=1) { ui_message("REMOVE FAILED");return result; }
    st_pool_ui_report[2]++; ui_message("REMOVED - SAVE PROJECT"); return 1;
}

static void preview_stop(void)
{
    preview_length=0;
    __asm__ volatile("" ::: "memory");
}
void st_pool_preview_stop(void) { preview_stop(); }

/* +0x1c is the native last opaque view, maintained by 40100ade.
 * Transparent hints above it must not disable audition. A newer opaque dialog
 * still owns input and stops preview. The native dispatcher visits overlays. */
static u32 ui_foreground(void)
{
    u8 *node;
    if(!ui_view || !ui_manager || ui_view[0x68]) return 0;
    node=*(u8 **)(ui_manager+0x1C);
    return node && node!=ui_manager+0x14 && *(u8 **)(node+8)==ui_view+0x38;
}

/* Paint through the same bitmap/view-stack path as the native App repaint.
 * Do not call the App maintenance loop: its idle counter is time based.
 * The native framebuffer cursor must be updated after each LCD buffer swap. */
void st_pool_ui_draw(u8 *iface,void *ctx)
{
    u32 width;void *font;
    ((void (*)(void *,void *))0x400FA86Au)(iface-0x38,ctx);
    if(!ui_loading) return;
    font=((void *(*)(void *))0x400F92B4u)(ui_view+0x94);
    ((void (*)(void *,s32,s32,s32,s32,u32))0x400F7DF2u)(ctx,1,1,126,19,0);
    ((void (*)(void *,void *,s32,s32,u32,const char *))0x400F89A4u)(ctx,font,3,12,0xFFFFFFFFu,ui_progress_label);
    ((void (*)(void *,s32,s32,s32,s32,u32))0x400F7DF2u)(ctx,2,2,125,7,1);
    ((void (*)(void *,s32,s32,s32,s32,u32))0x400F7DF2u)(ctx,3,3,124,6,0);
    width=ui_progress_total?120u*ui_progress_done/ui_progress_total:0;
    if(width>120) width=120;
    if(width) ((void (*)(void *,s32,s32,s32,s32,u32))0x400F7DF2u)(ctx,4,4,3+width,5,1);
}
static void progress_paint(void)
{
    u32 bitmap[8],pixels;
    if(!ui_foreground()) return;
    ui_redraw();pixels=*(u32 *)0x405D3A18u;
    if(!pixels) return; /* Native App has not yet initialised its LCD context. */
    ((void (*)(void *,u32,u32,u32,u32))0x400F70FAu)(bitmap,128,64,pixels,0x405D3618u);
    ((void (*)(void *,void *))0x401007CEu)(ui_manager,bitmap);
    *(u32 *)0x405D3A18u=((u32 (*)(void))0x40113756u)();
    ((void (*)(void *))0x400F702Cu)(bitmap);
}
static void st_pool_ui_progress_begin(u32 count)
{
    ui_progress_count=count;ui_loading=count && ui_foreground();
    ui_progress_done=0;ui_progress_total=0;ui_progress_percent=101;
    if(ui_loading) { UI_THREE_FOCUS(ui_view,ui_view);ui_message("LOADING SAMPLES"); }
}
static void st_pool_ui_progress_file(u32 code,u32 ordinal,u32 bytes)
{
    char name[40];
    ui_progress_done=0;ui_progress_total=bytes;ui_progress_percent=0;
    number3(ui_progress_label,ordinal);ui_progress_label[3]='/';
    number3(ui_progress_label+4,ui_progress_count);ui_progress_label[7]=' ';
    row_name(name,code);text_set(ui_progress_label+8,name,31);
    progress_paint();
}
static void st_pool_ui_progress_bytes(u32 bytes) __attribute__((unused));
static void st_pool_ui_progress_bytes(u32 bytes)
{
    u32 percent;
    ui_progress_done+=bytes;
    if(ui_progress_done>ui_progress_total) ui_progress_done=ui_progress_total;
    percent=ui_progress_total?100u*ui_progress_done/ui_progress_total:0;
    if(percent!=ui_progress_percent) { ui_progress_percent=percent;progress_paint(); }
}
static void st_pool_ui_progress_end(void) { ui_loading=0;ui_redraw(); }

/* Read a window in task context, including the SAMP header only in the first
 * CRC fragment. Validate the complete CRC before publishing the final window. */
static s32 preview_fetch(u32 total)
{
    u32 loaded=preview_loaded,frames=total-loaded,bytes,i,crc,at;
    if(frames>PREVIEW_CHUNK) frames=PREVIEW_CHUNK;
    bytes=2*frames;
    if(preview_stock) {
        if(SLOT_READ(preview_stock-1,preview_scratch,preview_scratch+64,2*loaded,bytes)<0) return -1;
    } else if(MMC_READ(preview_lba+(2*loaded)/512,(bytes+64+511)&~511u,preview_scratch)!=0) return -1;
    if(!loaded && (be32(preview_scratch)!=0x53414D50u || be32(preview_scratch+8)!=total)) return -1;
    crc=preview_stock?0:crc_update(preview_crc,preview_scratch+(loaded?64:0),bytes+(loaded?0:64));
    if(!preview_stock && loaded+frames==total && ~crc!=preview_expected_crc) return -2;
    at=(loaded&(PREVIEW_FRAMES-1))*2;
    for(i=0;i<bytes;i++) preview_pcm[at+i]=preview_scratch[64+i];
    preview_crc=crc;
    __asm__ volatile("" ::: "memory");
    preview_loaded=loaded+frames;
    return (s32)frames;
}

/* Prime two windows for immediate audition; subsequent IO/CRC stays in UI idle.
 * This never loads the complete long sample or changes project descriptors. */
s32 st_pool_ui_preview(u32 code)
{
    const u8 *e=0; u8 *fs; u32 frames=0,i; s32 result;
    preview_stop();
    preview_stock=0;
    if(code>=1 && code<=STOCK_SLOTS) {
        fs=MMCFS();if(fs) frames=be32(fs+0x12E3C+(code-1)*24);
        if(frames>0x3FFE0u) frames=0;
        preview_stock=code;
    } else if((code&HZ_FLAG) && ui_index && (e=hz_find(ui_index,ui_index_len,code&~HZ_FLAG))) {
        frames=be32(e+16);
    }
    if(!frames) {
        ui_message("PREVIEW: SAMPLE MISSING"); return -1;
    }
    if(!preview_pcm) {
        preview_pcm=MALLOC(2*PREVIEW_FRAMES);
        if(!heap_ok(preview_pcm)) { preview_pcm=0; ui_message("PREVIEW: NO MEMORY"); return -2; }
    }
    if(!preview_scratch) {
        preview_scratch=MALLOC(0x10200);
        if(!heap_ok(preview_scratch)) { preview_scratch=0;ui_message("PREVIEW: NO MEMORY");return -2; }
    }
    preview_pos=0;preview_loaded=0;preview_lba=e?be32(e+4)*HZ_BLOCK_SECTORS:0;
    preview_crc=0;preview_expected_crc=e?be32(e+20):0;
    for(i=0;i<2 && preview_loaded<frames;i++) {
        result=preview_fetch(frames);
        if(result<0) {
            ui_message(result==-2?"PREVIEW: CRC ERROR":"PREVIEW: READ ERROR");return -3;
        }
    }
    __asm__ volatile("" ::: "memory");
    preview_length=frames;
    st_pool_ui_report[3]++; st_pool_ui_report[4]=frames;
    ui_message("PREVIEWING");
    return (s32)frames;
}

/* UI task context only. A whole consumed window is free before it is refilled.
 * The IRQ sees preview_loaded only after the bytes have been published. If UI
 * stalls longer than the ring, it emits silence and resumes without stale PCM. */
void st_pool_preview_refill(void)
{
    u32 n=preview_length,loaded=preview_loaded,p=preview_pos; s32 result;
    if(!n || loaded>=n || loaded-p>PREVIEW_CHUNK) return;
    result=preview_fetch(n);
    if(result<0) {
        preview_stop();ui_message(result==-2?"PREVIEW: CRC ERROR":"PREVIEW: READ ERROR");return;
    }
}
void core_pool_tick(void *controller)
{
    (void)controller;
    if(ui_view) {
        if(ui_foreground()) st_pool_preview_refill();else { preview_stop();ui_yes_down=0; }
    }
}

/* Preview bus (mix.h: outside the FX track) is inverted Q31, 32 stereo
 * frames at 48 kHz. Mono signed BE16 at 1/32 Q31, x5/4 before the native
 * x8 stage. The 4008eeca gain made the old 1/4 scale clip at full PCM.
 * Effective peak is 10/32 (-10.1 dBFS) before the final output ramp.
 * Saturating subtraction still protects addition to a busy bus. */
static void preview_mix_frame(u32 frame,s32 x)
{
    st_preview_mix_frame(MAIN_BUS,frame,x);
}
void st_pool_preview_mix(void)
{
    u32 i,n=preview_length,p=preview_pos; s32 x;
    if(n && preview_pcm) {
        st_pool_ui_report[5]++;
        for(i=0;i<32 && p<n && p<preview_loaded;i++,p++) {
            x=(s32)(short)be16(preview_pcm+2*(p&(PREVIEW_FRAMES-1)))*2048;
            preview_mix_frame(i,x);
        }
        preview_pos=p;
        st_pool_ui_report[6]=p;
        if(p==n) preview_length=0;
    }
}

static void row_name(char *s,u32 code)
{
    const u8 *e;
    if(!code) { text_set(s,"EMPTY",32); return; }
    if(!ui_index && core_pool_cached_name(code,s))return;
    if(code<=64) {
        u8 *fs=MMCFS(); if(fs) { name_copy(s,fs+0x12E28+(code-1)*24,16); return; }
    }
    if((code&HZ_FLAG) && ui_index && (e=hz_find(ui_index,ui_index_len,code&~HZ_FLAG))) {
        name_copy(s,e+32,32); return;
    }
    text_set(s,"MISSING",32);
}
void *st_pool_item_label(void *ret,const struct fn_capture *fn)
{
    const struct row_capture *c=fn->data; char text[48]; u8 alloc=0; u32 s;
    if(!c) text_set(text,"NO MEMORY",47);
    else if(c->kind==ROW_EMPTY) text_set(text,"NO SAMPLES",47);
    else if(c->kind==ROW_OPEN) text_set(text,"SAMPLE MANAGER",47);
    else if(c->kind==ROW_VIEW) text_set(text,ui_mode?"VIEW POOL":"VIEW eMMC",47);
    else if(c->kind==ROW_APPLY) text_set(text,ui_mode?"ADD TO POOL":"REMOVE FROM POOL",47);
    else if(c->kind==ROW_SLOT) {
        number3(text,c->code+1); text[3]=' '; st_pool_read(WORKING_PROJECT,st_pool_now);
        row_name(text+4,st_pool_now[c->code]);
    } else {
        s=code_slot(ui_codes[c->code]);
        if(s) number3(text,s); else { text[0]='-';text[1]='-';text[2]='-'; }
        text[3]=' '; row_name(text+4,ui_codes[c->code]);
    }
    QSTR_INIT(ret,text,&alloc); return ret;
}
/* Native C++ uses A0 for the return address of a nontrivial QString value. */
extern void st_pool_label_invoker(void);
#ifndef CORE_ST_NATIVE_TEST
__asm__(".globl st_pool_label_invoker\n"
        "st_pool_label_invoker:\n"
        "move.l 4(%sp),-(%sp)\n"
        "move.l %a0,-(%sp)\n"
        "jsr st_pool_item_label\n"
        "addq.l #8,%sp\n"
        "rts\n");
#endif
static u32 row_manager(struct fn_capture *dst,const struct fn_capture *src,u32 op)
{
    struct row_capture *c;
    if(op==0) dst->data=0;
    else if(op==1) dst->data=src->data;
    else if(op==2) {
        dst->data=0;c=MALLOC(sizeof(*c)); if(!heap_ok(c)) return 0;
        *c=*(const struct row_capture *)src->data; dst->data=c;
    } else if(op==3) FREE(dst->data);
    return 0;
}
static void row_action(const struct fn_capture *fn);
void st_pool_ui_open(u8 *menu);
u32 st_pool_ui_event(const u8 *ev);
u32 st_pool_ui_key(u8 *iface,const u8 *ev)
{
    if(!ui_foreground() || iface!=ui_view+0x38) return 0;
    if(st_pool_ui_event(ev)) return 1;
    return ((u32 (*)(void *,const void *))0x400FAD00u)(iface-0x38,ev);
}
static u32 st_pool_row_selected(void *view,u32 row)
{
    if(view!=ui_view) return 0;
    if(ui_mode && ui_selected && row<ui_count) return ui_selected[row];
    if(!ui_mode && row<SLOTS) return ui_pool_selected[row];
    return 0;
}
static const u32 sample_menu_pixels[12]=SAMPLE_MENU_ICON_COLUMNS;
static const u32 sample_menu_mask[12]=SAMPLE_MENU_ICON_MASK;
void st_pool_menu_icon_draw(const struct fn_capture *fn,void *item,void *ctx,s32 x,s32 y)
{
    u32 bitmap[8];
    (void)fn;(void)item;
    /* Non-owning descriptor over original ROM-format columns; no pixel heap. */
    ((void (*)(void *,u32,u32,const void *,void *))0x400F70FAu)
        (bitmap,12,11,sample_menu_pixels,(void *)sample_menu_mask);
    ((void (*)(void *,void *,s32,s32,u32))0x400F8DACu)(ctx,bitmap,x,y,0);
    ((void (*)(void *))0x400F702Cu)(bitmap);
}
static void add_row(u8 *view,u32 kind,u32 code,u8 *menu)
{
    struct row_capture c={kind,code,menu};
    struct fn_capture label={&c,0,row_manager,st_pool_label_invoker};
    struct fn_capture action={&c,0,row_manager,row_action};
    struct fn_capture icon={&c,0,row_manager,(void *)st_pool_menu_icon_draw};
    struct fn_capture empty={0,0,0,0}; u8 *item=MALLOC(0x54);
    if(!heap_ok(item)) return;
    UI_ITEM_INIT(item,&label,&action,kind==ROW_OPEN?&icon:&empty,&empty,-1,8);
    ((void (*)(void *,void *))(*(u32 **)view)[2])(view,item);
}
static void rebuild_list(void)
{
    u32 i;
    UI_LIST_CLEAR(ui_view);
    if(ui_mode) {
        for(i=0;i<ui_count;i++) add_row(ui_view,ROW_FILE,i,0);
        /* Native selected-item maintenance reads row zero even for an empty
         * vector. Keep a real owned, non-actionable row instead of exposing
         * unused vector capacity as an item/control-block pair. */
        if(!ui_count) add_row(ui_view,ROW_EMPTY,0,0);
    }
    else for(i=0;i<SLOTS;i++) add_row(ui_view,ROW_SLOT,i,0);
    UI_LIST_SELECT(ui_view,0,0); ui_redraw();
}
static void apply_selection(void)
{
    u32 i,j,k,n=0,full=0,protected=0; char message[24];u16 before[SLOTS];s32 result=1;
    if(!st_pool_live_ready()) { ui_message("AUDIO NOT READY");return; }
    preview_stop();
    st_pool_read(WORKING_PROJECT,st_pool_now);
    for(i=0;i<SLOTS;i++) before[i]=st_pool_now[i];
    if(ui_mode) {
        for(i=0;i<ui_count;i++) if(ui_selected[i]) {
            for(j=0;j<SLOTS && st_pool_now[j]!=ui_codes[i];j++) {}
            if(j<SLOTS) { ui_selected[i]=0; continue; }
            for(k=STOCK_SLOTS;k<SLOTS && st_pool_now[k];k++) {}
            if(k==SLOTS) { full=1; continue; }
            st_pool_now[k]=ui_codes[i];ui_selected[i]=0;n++;
        }
    } else {
        for(i=0;i<SLOTS;i++) if(ui_pool_selected[i]) {
            if(i<STOCK_SLOTS)protected=1;
            if(i>=STOCK_SLOTS && st_pool_now[i]) { st_pool_now[i]=0;n++; }
            ui_pool_selected[i]=0;
        }
    }
    if(n) {
        st_pool_write(WORKING_PROJECT,st_pool_now);result=st_pool_live_sync(before);
        st_pool_read(WORKING_PROJECT,st_pool_now);n=0;
        for(i=0;i<SLOTS;i++) if(before[i]!=st_pool_now[i]) n++;
        st_pool_ui_report[ui_mode?1:2]+=n;
    }
    text_set(message,ui_mode?"ADDED 000":"REMOVED 000",23);
    number3(message+(ui_mode?6:8),n);
    UI_THREE_FOCUS(ui_view,ui_view);
    ui_message(result!=1?(result==-6?"AUDIO FULL":"AUDIO ERROR"):full?"POOL FULL":
        protected && !n?"STOCK SLOTS PROTECTED":message);
}
static void row_action(const struct fn_capture *fn)
{
    const struct row_capture *c=fn->data;
    if(!c || c->kind==ROW_EMPTY) return;
    if(c->kind==ROW_OPEN) st_pool_ui_open(c->menu);
    else if(c->kind==ROW_VIEW) {
        preview_stop();ui_mode=!ui_mode;rebuild_list();
        UI_THREE_FOCUS(ui_view,ui_view);ui_message("SAMPLE MANAGER");
    } else if(c->kind==ROW_APPLY) apply_selection();
    else if(c->kind==ROW_FILE && c->code<ui_count) {
        ui_selected[c->code]^=1;ui_redraw();
    } else if(c->kind==ROW_SLOT && c->code<SLOTS) {
        ui_pool_selected[c->code]^=1;ui_redraw();
    }
}
void st_pool_ui_dispose(u8 *view)
{
    preview_stop();
    if(ui_view==view) {
        ui_view=0;
        ui_manager=0;
        if(ui_index) FREE(ui_index);
        ui_index=0;
        if(ui_selected) FREE(ui_selected);
        ui_selected=0;
        if(ui_codes) FREE(ui_codes);
        ui_codes=0;
        if(preview_pcm) FREE(preview_pcm);
        preview_pcm=0;
        if(preview_scratch) FREE(preview_scratch);
        preview_scratch=0;
        ui_count=0;
    }
    UI_THREE_DTOR(view);
}
/* Refcount wrapper uses the native control-block destructor and free methods,
 * with our newly constructed UI list at +16. No dynamic cast is needed. */
void st_pool_ui_open(u8 *menu)
{
    u8 *root,*view; u32 i,j,n,code; void *pair[2]; const u8 *e;
    if(ui_view) return;
    root=MALLOC(16+0x550); if(!heap_ok(root)) return;
    *(u32 *)root=0x401C9670u; put32(root+4,1);put32(root+8,1);
    view=root+16; *(u8 **)(root+12)=view;
    UI_THREE_INIT(view,"SAMPLE MANAGER",128,1,1,1);
    /* Wider owned action pane fits REMOVE FROM POOL in the native small font.
     * Native three-pane draw reads the list viewport at +198/+1a0. */
    *(u32 *)(view+0x380+0x198)=49;
    *(u32 *)(view+0x380+0x1A0)=79;
    UI_OPAQUE(view+0x38,1);
    /* Both primary and secondary tables retain the complete two-word C++
     * offset/RTTI header: native dynamic_cast reads the primary header too. */
    for(i=0;i<45;i++) ui_table[i]=((const u32 *)0x40240DECu)[i];
    ui_table[2]=(u32)st_pool_ui_dispose;
    ui_table[2+0x64/4]=(u32)st_pool_row_selected;
    *(u32 **)view=ui_table+2;
    /* Copy only raw immutable vtable entries into our own table, retaining its
     * offset-to-top and RTTI header. The native list remains otherwise intact. */
    for(i=0;i<19;i++) ui_view_table[i]=((const u32 *)0x40240EA0u)[i];
    ui_view_table[2+2]=(u32)st_pool_ui_key;
    ui_view_table[2+4]=(u32)st_pool_ui_draw;
    /* Native idle/lifetime methods remain untouched. Streaming refill uses
     * the ordinary ev_tick subscription, like other periodic core UI work. */
    *(u32 **)(view+0x38)=ui_view_table+2;
    ui_view=view;ui_mode=1;ui_count=0;ui_combo_keys=0;ui_yes_down=0;
    if(st_pool_read(WORKING_PROJECT,st_pool_now)!=1) st_pool_write(WORKING_PROJECT,st_pool_now);
    ui_index=hz_load(&ui_index_len);
    if(ui_index) {
        n=be32(ui_index+12); ui_codes=MALLOC(2*(n?n:1));
        if(heap_ok(ui_codes)) {
            for(i=0;i<n;i++) {
                e=ui_index+HZ_ENTRIES+i*HZ_ENTRY;code=be32(e);
                if(!hz_find(ui_index,ui_index_len,code) || !be32(e+16)) continue;
                for(j=0;j<ui_count && ui_codes[j]!=(HZ_FLAG|code);j++) {}
                if(j==ui_count) ui_codes[ui_count++]=(u16)(HZ_FLAG|code);
            }
        } else ui_codes=0;
    }
    ui_selected=MALLOC(ui_count?ui_count:1);
    if(!heap_ok(ui_selected)) { ui_selected=0;ui_count=0; }
    else for(i=0;i<ui_count;i++) ui_selected[i]=0;
    for(i=0;i<SLOTS;i++) ui_pool_selected[i]=0;
    add_row(view+0x1C0,ROW_VIEW,0,0);
    add_row(view+0x380,ROW_APPLY,0,0);
    text_set(ui_status,"SAMPLE MANAGER",39);
    rebuild_list();
    pair[0]=view+0x38;pair[1]=root;
    ui_manager=menu?*(u8 **)(menu+0x64):0;
    if(ui_manager) UI_PUSH(ui_manager,pair,0);
    UI_DROP(pair+1);
    st_pool_ui_report[0]=ui_count;
}
void st_pool_ui_menu(u8 *menu)
{
    /* Pool belongs to the project, so it remains accessible on every track. */
    add_row(menu,ROW_OPEN,0,menu);
}
/* FUNC combos must reach the active pool before the app's pre-dispatch
 * listeners (4000839e) can act on the underlying SOUND view. Use the native
 * manager dispatcher directly so release callback cleanup remains native. */
u32 st_pool_ui_pre_event(void *app,const u8 *ev)
{
    u32 key=be32(ev+12),flags=be32(ev+16),bit;
    (void)app;
    if(!ui_foreground()) { ui_combo_keys=0;ui_yes_down=0;preview_stop();return 0; }
    if(key!=12 && key!=13) return 0;
    bit=key==12?1:2;
    if(flags&1) {
        if(!(flags&2)) return 0;
        ui_combo_keys|=bit;
    } else {
        if(!(ui_combo_keys&bit)) return 0;
        ui_combo_keys&=~bit;
    }
    ((u32 (*)(void *,const void *))0x401012D4u)(ui_manager,ev);
    st_pool_ui_report[7]++;
    return 1;
}
/* Native IView key callback: code +12, flags +16 (pressed bit0, FUNC bit1;
 * YES/NO edges bit4). Root dispatch, release callbacks and view cleanup remain
 * native: only our independently owned view's virtual key method is replaced. */
u32 st_pool_ui_event(const u8 *ev)
{
    u32 key,flags,row,code=0,count,step; u8 *active;
    struct row_capture c; struct fn_capture fn;
    if(!ui_foreground()) { ui_yes_down=0;preview_stop();return 0; }
    if(ui_loading) return 1;
    key=be32(ev+12);flags=be32(ev+16);
    if(key<12 || key>17) return 0;
    active=*(u8 **)(ui_view+0x540);
    if(!(flags&1)) {
        if(key==12) {
            ui_yes_down=0;preview_stop();
            if(ui_status[0]=='P' && ui_status[7]=='I') ui_message("SAMPLE MANAGER");
            return 1;
        }
        if(key==13 && !(flags&2)) return 0; /* native back/close on release */
        return 1;
    }
    if(key==12) { if(ui_yes_down) return 1;ui_yes_down=1; }
    row=*(u32 *)(ui_view+16);
    if(ui_mode && row<ui_count) code=ui_codes[row];
    else if(!ui_mode && row<SLOTS) { st_pool_read(WORKING_PROJECT,st_pool_now);code=st_pool_now[row]; }
    if(key==12 && (flags&2)) { st_pool_ui_preview(code);return 1; }
    if(key==13 && (flags&2)) return 1;
    if(key==13) { preview_stop();return 0; }
    if(key==16 || key==17) {
        preview_stop();
        UI_THREE_FOCUS(ui_view,active==ui_view?ui_view+(key==16?0x1C0:0x380):ui_view);
        ui_message("SAMPLE MANAGER");return 1;
    }
    if(key==14 || key==15) {
        count=active==ui_view?(ui_mode?ui_count:SLOTS):1;
        row=*(u32 *)(active+16);step=(flags&2)?8:1;preview_stop();
        if(!count) return 1;
        if(key==14) row=row>step?row-step:0;
        else row=row+step<count?row+step:count-1;
        UI_LIST_SELECT(active,row,0);ui_message("SAMPLE MANAGER");return 1;
    }
    if(key==12) {
        c.kind=active==ui_view?(ui_mode?ROW_FILE:ROW_SLOT):active==ui_view+0x1C0?ROW_VIEW:ROW_APPLY;
        c.code=row;c.menu=0;fn.data=&c;row_action(&fn);
    }
    return 1;
}

s32 st_pool_live_sync(const u16 *before)
{
    u32 i,n=0;int result;
    for(i=0;i<SLOTS;i++)if(st_pool_now[i] && st_pool_now[i]!=before[i])n++;
    st_pool_ui_progress_begin(n);result=core_pool_sync();st_pool_ui_progress_end();
    if(result!=1){core_pool_write(WORKING_PROJECT,before);core_pool_sync();}
    return result==-2?-6:result;
}
void core_pool_menu(void *menu){st_pool_ui_menu(menu);}
int core_pool_key(void *app,const u8 *ev){return st_pool_ui_pre_event(app,ev);}
void core_pool_ui_progress(u32 done,u32 total)
{
    u32 percent;
    if(!ui_loading || !ui_foreground())return;
    ui_progress_done=done;ui_progress_total=total;
    percent=total?100u*done/total:0;if(percent!=ui_progress_percent){ui_progress_percent=percent;progress_paint();}
}
void core_pool_ui_file(u32 code,u32 ordinal,u32 bytes)
{if(ui_loading)st_pool_ui_progress_file(code,ordinal,bytes);}
/* SAMP controls resolve project slots through the same core pool as the UI. */
char *core_pool_format(u32 id,u32 raw)
{
    const struct core_machine_def *m;static char formatted[37];u16 entries[SLOTS];u32 index;
    if(id<0x1f9 || id>=0x3f9)return 0;
    m=core_machine_find(64+(id-0x1f9)/32);index=(id-0x1f9)&31;
    if(!m || index>=m->params || m->parameter[index].alias!=0x1f2)return 0;
    core_pool_read(WORKING_PROJECT,entries);row_name(formatted,entries[(raw>>8)&127]);return formatted;
}
