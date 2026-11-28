/* SPDX-License-Identifier: GPL-2.0-or-later
 * Optional SMZ modules join the ordinary event lists once, without a proxy
 * dispatcher or a per-event scan of modules. Only core-owned list references
 * are published; stock firmware and the file contain no runtime patch plan.
 */
#include <syntakt/modules.h>
#include <syntakt/native.h>
#include "smz.h"
typedef st_u32 u32;
extern void *fw_malloc(u32);extern void fw_free(void *);
extern const struct st_machine_ui_api core_machine_ui_api;
extern void core_machine_input_reset(void);
extern void core_audio_idle_reset(void);
extern void core_event_publish(const u32 *);
#define VIEW_ENTRY 0x80000000u
#define EVENT(name) extern const u32 ev_##name[];extern const unsigned char core_##name##_table[];
EVENT(tick) EVENT(draw) EVENT(key) EVENT(enc) EVENT(settings)
EVENT(render_in) EVENT(render_out) EVENT(fs_route) EVENT(fs_write) EVENT(fs_written) EVENT(main_mix)
EVENT(track_send) EVENT(delay_in) EVENT(reverb_in) EVENT(fx_cv)
EVENT(pattern_menu) EVENT(reload_menu)
extern const u32 ev_native[];
#define E(name) {ev_##name,core_##name##_table+2}
static struct {const u32 *list;const unsigned char *operand;} events[]={
    E(tick),E(draw),E(key),E(enc),E(settings),E(render_in),E(render_out),
    E(fs_route),E(fs_write),E(fs_written),E(main_mix),{ev_native,0},
    E(track_send),E(delay_in),E(reverb_in),E(fx_cv),E(pattern_menu),E(reload_menu)};
typedef char event_table_size[sizeof events/sizeof events[0]==ST_EV_COUNT?1:-1];
/* One byte per native operation: zero replays stock immediately; 0x80 is the
 * original full-list traversal (every entry wants it, an entry past 32, or
 * before initialization). Core 2.6.1: any other value only says someone is
 * interested, and the dispatcher takes the entries from the operation's word
 * in core_native_routes, one bit per list entry (up to 32).
 * The assembly adapters still only test nonzero. A subscriber is
 * interested in every operation unless it narrowed itself (host API 5).
 * Bytes are published atomically with the native list, and fail open before
 * application initialization, so a subscriber can never miss an operation
 * it asked for (POLY's private commands travel as IMMEDIATE operations). */
#define NATIVE_WORDS ((ST_NATIVE_OPERATION_LIMIT+3)/4)
/* Reserved bytes 18/19 filter TYPE: 18 = digital tracks
 * 0..7 whose type cursor is dispatched, 19 nonzero = every cursor. Host API 5
 * mask bits 24..31 narrow a TYPE subscriber to those tracks (0 or 0xff: all).
 * native.s tests these bytes by address. */
#define NATIVE_TYPE_TRACKS ST_NATIVE_OPERATION_COUNT
typedef char native_type_bytes[ST_NATIVE_OPERATION_COUNT==18 && ST_NATIVE_NOTE_ACCEPT==20?1:-1];
#define NATIVE_BYTES_INIT {128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,128,0xff,1,128,128,128,128}
#define NATIVE_ROUTES_INIT {~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u}
typedef char native_routes_size[ST_NATIVE_OPERATION_LIMIT==24?1:-1];
#ifdef __mcoldfire__
/* The words sit 96 bytes before the bytes, so the dispatcher
 * reads an operation's word with the base it already holds for its byte
 * (native-routes.s), at no extra instruction. */
static struct {u32 routes[ST_NATIVE_OPERATION_LIMIT];unsigned char interest[4*NATIVE_WORDS];}
    native_table __attribute__((aligned(4)))={NATIVE_ROUTES_INIT,NATIVE_BYTES_INIT};
typedef char native_table_layout[__builtin_offsetof(__typeof__(native_table),interest)==96?1:-1];
__asm__(".globl core_native_routes\n.set core_native_routes,native_table\n"
        ".globl core_native_interest\n.set core_native_interest,native_table+96");
#define core_native_routes (native_table.routes)
#define core_native_interest (native_table.interest)
#else
unsigned char core_native_interest[4*NATIVE_WORDS] __attribute__((aligned(4)))=NATIVE_BYTES_INIT;
u32 core_native_routes[ST_NATIVE_OPERATION_LIMIT]=NATIVE_ROUTES_INIT;
#endif
/* Per subscriber: operation mask (host API 5) and page view class (API 6).
 * A full table leaves the newcomer at every operation and view (open). */
/* Cover the bank's module slots (CORE_SMZ_MODULES, 32 since core 2.6.1). The
 * assembler uses the exported array end, so its lookup cannot silently
 * retain an older capacity. */
#define NATIVE_SLOTS CORE_SMZ_MODULES
#define NATIVE_STRING_(n) #n
#define NATIVE_STRING(n) NATIVE_STRING_(n)
#define NATIVE_PAGE_OPS ((1u<<ST_NATIVE_PAGE_SYNC)|(1u<<ST_NATIVE_PAGE_NAV))
static struct {u32 fn,mask,view;} native_masks[NATIVE_SLOTS];
/* PAGE_SYNC/PAGE_NAV reach the dispatcher only for views whose vtable is this
 * word (machine-pages.c); 0 = every view. Nonzero only when every subscriber
 * of those operations declared the same view class. Published with the list. */
u32 core_native_page_view;
u32 core_relation_scope=~0u,core_relation_offsets=~0u;
static u32 relation_callback,relation_tracks,relation_offsets;

static u32 native_mask(u32 fn)
{
    u32 i;
#pragma GCC unroll 1
    for(i=0;i<NATIVE_SLOTS;i++)if(native_masks[i].fn==fn)return native_masks[i].mask;
    return ~0u;
}

static u32 relation_scope(const u32 *list)
{
    u32 fn,scope=~0u;
    if(!relation_callback)return scope;
    while((fn=*list++)) {
        if(fn==relation_callback)scope=relation_tracks;
        else if(native_mask(fn)&((1u<<ST_NATIVE_RELATION_MASK)|(1u<<ST_NATIVE_RELATION_EVENT)))return ~0u;
    }
    return scope;
}

static void relation_refresh(void)
{
    u32 scope=relation_scope(events[ST_EV_NATIVE].list);
    core_relation_scope=scope;
    core_relation_offsets=scope==~0u?~0u:relation_offsets;
}

void core_native_relation_scope(void (*callback)(void),u32 tracks,u32 offsets)
{
    relation_callback=(u32)callback;relation_tracks=tracks;relation_offsets=offsets;
    relation_refresh();
}
/* Ordered subscriber routes, one byte per operation; returns the page view
 * filter. Shared by startup, host API 5/6 and bind (cold). Reserved bytes
 * 18/19 retain TYPE track filtering. No new allocation or published pointer. */
#ifdef __mcoldfire__
/* Private aliases for resident assembly; these add no data or host API. */
__asm__(".globl core_native_masks\n.set core_native_masks,native_masks\n"
        ".globl core_native_masks_end\n.set core_native_masks_end,native_masks+" NATIVE_STRING(NATIVE_SLOTS) "*12\n"
        ".globl core_native_list\n.set core_native_list,events+88");
typedef char native_list_offset[ST_EV_NATIVE==11?1:-1];
extern u32 core_native_route_fill(unsigned char *,const u32 *);
#define native_fill core_native_route_fill
#else
__attribute__((noinline)) static u32 native_fill(unsigned char *out,const u32 *list)
{
    u32 fn,t=0,i,own,view,pv=0,any=0,bit=1,all=0;
    for(i=0;i<4*NATIVE_WORDS;i++)out[i]=0;
    while((fn=*list++)){
        own=~0u;view=0;
#pragma GCC unroll 1
        for(i=0;i<NATIVE_SLOTS;i++)if(native_masks[i].fn==fn){own=native_masks[i].mask;view=native_masks[i].view;break;}
        if(own>>ST_NATIVE_TYPE&1u)t|=own>>24 && own>>24!=0xffu?own>>24:0x100u;
        if(own&NATIVE_PAGE_OPS){if(!view || (pv && pv!=view))any=1;pv=view;}
        all|=bit;
        for(i=0;i<4*NATIVE_WORDS;i++,own>>=1)if(own&1u)out[i]|=(unsigned char)bit;
        if(bit<128u)bit<<=1;
    }
    if(all)for(i=0;i<4*NATIVE_WORDS;i++)if(out[i]==all)out[i]=128;
    out[NATIVE_TYPE_TRACKS]=(unsigned char)t;out[NATIVE_TYPE_TRACKS+1]=(unsigned char)(t>>8);
    return any?0:pv;
}
#endif
/* The words for bit-7 bytes (cold, as the bytes). Same masks and order as
 * native_fill: an unregistered subscriber wants every operation. */
__attribute__((noinline)) static void native_routes(u32 *out,const u32 *list)
{
    u32 fn,own,op,index=0,all=0;
    for(op=0;op<ST_NATIVE_OPERATION_LIMIT;op++)out[op]=0;
    while((fn=*list++)){
        own=native_mask(fn);
        for(op=0;op<ST_NATIVE_OPERATION_LIMIT;op++)if(own>>op&1u)
            out[op]=index<32u?out[op]|1u<<index:~0u;
        if(index<32u)all|=1u<<index;
        index++;
    }
    for(op=0;op<ST_NATIVE_OPERATION_LIMIT;op++)
        if((all && out[op]==all) || op-ST_NATIVE_OPERATION_COUNT<2u)out[op]=~0u;
}
/* The encoder shares bit 7 past the seventh entry. Rewrite each interested
 * byte from its word: 0x80 the full traversal, otherwise nonzero (the first
 * seven's own bits, or 0x7f past them) for the word. TYPE bytes stay. */
__attribute__((noinline)) static void native_merge(unsigned char *bytes,const u32 *words)
{
    u32 op,w;
    for(op=0;op<ST_NATIVE_OPERATION_LIMIT;op++){
        if(op-ST_NATIVE_OPERATION_COUNT<2u || !bytes[op] || !(w=words[op]))continue;
        bytes[op]=(unsigned char)(w==~0u?128u:w>>7?127u:w);
    }
}
/* Words first, then the bytes: a positive byte always finds a current word. */
static u32 native_refresh(void)
{
    u32 view;
    native_routes(core_native_routes,events[ST_EV_NATIVE].list);
    view=native_fill(core_native_interest,events[ST_EV_NATIVE].list);
    native_merge(core_native_interest,core_native_routes);
    relation_refresh();
    return view;
}
void core_native_initialize(void)
{core_native_page_view=native_refresh();}
/* Host API 5/6. Call from the subscriber's init, its audio IRQ or an
 * IRQ-masked section. A new slot starts at every operation and view; a full
 * table ignores the newcomer's narrowing (open). field: 1 mask, 2 view. */
__attribute__((noinline)) static void native_set(void (*callback)(void),u32 value,u32 field)
{
    u32 i,*e=0,*slot=0;
#pragma GCC unroll 1
    for(i=0;i<NATIVE_SLOTS;i++){
        u32 *x=&native_masks[i].fn;
        if(*x==(u32)callback){e=x;break;}
        if(!*x && !slot)slot=x;
    }
    if(!e){if(!slot)return;e=slot;e[0]=(u32)callback;e[1]=~0u;e[2]=0;}
    if(e[field]==value)return;
    e[field]=value;
    core_native_page_view=native_refresh();
}
void core_native_interest_set(void (*callback)(void),u32 mask){native_set(callback,mask,1);}
void core_native_page_view_set(void (*callback)(void),u32 vtable){native_set(callback,vtable,2);}
#ifndef __mcoldfire__
int core_event_native(u32 operation,const u32 *args,u32 *result)
{
    const u32 *p=events[ST_EV_NATIVE].list;u32 fn,bits=core_native_interest[operation];
    if(!(bits&128u) && operation-ST_NATIVE_OPERATION_COUNT>=2u)bits=core_native_routes[operation];
    else bits=~0u;
    if(bits==~0u){
        while((fn=*p++))if(((int (*)(u32,const u32 *,u32 *))fn)(operation,args,result))return 1;
        return 0;
    }
    while(bits && (fn=*p++)){
        if((bits&1u) && ((int (*)(u32,const u32 *,u32 *))fn)(operation,args,result))return 1;
        bits>>=1;
    }
    return 0;
}
#endif
u32 core_module_count,core_module_subscriptions;
/* These two audited modules write resident code during init. Their stock
 * preimages are checked by machine-boot.c before any initializer runs, and
 * they initialize after every module that may reject for an API/resource
 * reason. Subscription order below stays in the bank's original order. */
/* Initialization only: the two names have different initial bytes. Select
 * one candidate, then compare the complete string in a shared pointer loop;
 * avoid duplicating the comparison in the resident initializer. */
static int patching_module(const char *name)
{
    const char *wanted=*name=='L'?"LFO3":*name=='S'?"STATE":"MODULATION SKIP";
    while(*name && *name==*wanted){name++;wanted++;}
    return *name==*wanted;
}
/* Reuse the published collection, selecting only subscribers that requested
 * its native-view context. No module scan, input registry or retained view. */
int core_event_view(u32 event,void *view,void *data,u32 id)
{
    const u32 *p=events[event].list;u32 fn,mask=VIEW_ENTRY,wanted=VIEW_ENTRY;int taken;
    int consuming=event==ST_EV_ENC || (event==ST_EV_KEY && id==1);
    if(event==ST_EV_KEY){mask|=1u;wanted|=id==1;}
    while((fn=*p++))if((fn&mask)==wanted){
        if(consuming){
            taken=((int (*)(void *,void *,u32))(fn&~(VIEW_ENTRY|1u)))(view,data,id);
            if(taken)return taken;
        }else ((void (*)(void *,void *,u32))(fn&~VIEW_ENTRY))(view,data,id);
    }
    return 0;
}
int core_modules_bind(const struct st_smz_module *mods,u32 count)
{
    u32 i,j,k,e,phase,old[ST_EV_COUNT],add[ST_EV_COUNT],used=0,total=0,patches[4*ST_EV_COUNT+2*NATIVE_WORDS+2*ST_NATIVE_OPERATION_LIMIT+7],n=0;
    u32 *memory,*dst,*lists[ST_EV_COUNT];struct st_smz_api api={1,&core_machine_ui_api};
    core_machine_input_reset();
    core_audio_idle_reset();
    for(e=0;e<ST_EV_COUNT;e++){old[e]=add[e]=0;while(events[e].list[old[e]])old[e]++;}
    for(i=0;i<count;i++)for(j=0;j<mods[i].count;j++)add[ST_EV_INDEX(mods[i].subscriptions[j].event)]++;
    for(e=0;e<ST_EV_COUNT;e++)if(add[e])total+=old[e]+add[e]+1;
    memory=total?fw_malloc(total*4):0;
    if(total && (u32)memory<0x40000000u)return 0;
    for(phase=0;phase<2;phase++)for(i=0;i<count;i++){
        if((u32)patching_module(mods[i].name)!=phase)continue;
        /* Legacy initializers may require exactly version 1. Only a module
         * explicitly requesting native-view events needs service version 2. */
        api.version=1;
        for(j=0;j<mods[i].count;j++){
            e=mods[i].subscriptions[j].event;
            if(e&ST_EV_VIEW && api.version<2)api.version=2;
            if(e&ST_EV_PRE || e==ST_EV_NATIVE)api.version=3;
        }
        if((u32)mods[i].init!=0xffffffffu && mods[i].init(&api)){
            core_machine_input_reset();core_audio_idle_reset();if(memory)fw_free(memory);return 0;}
    }
    for(e=0;e<ST_EV_COUNT;e++)if(add[e]){
        dst=memory+used;if(events[e].operand){patches[n++]=(u32)events[e].operand;patches[n++]=(u32)dst;}
        lists[e]=dst;
        for(k=0;k<old[e];k++)*dst++=events[e].list[k];
        for(i=0;i<count;i++)for(j=0;j<mods[i].count;j++)
            if(ST_EV_INDEX(mods[i].subscriptions[j].event)==e)
                *dst++=(u32)mods[i].subscriptions[j].callback |
                    (mods[i].subscriptions[j].event&ST_EV_VIEW?VIEW_ENTRY:0) |
                    (mods[i].subscriptions[j].event&ST_EV_PRE?1u:0);
        *dst++=0;used=(u32)(dst-memory);
        patches[n++]=(u32)&events[e].list;patches[n++]=(u32)lists[e];
        core_module_subscriptions+=add[e];
    }
    {union {unsigned char b[4*NATIVE_WORDS];u32 w[NATIVE_WORDS];} next;u32 wide[ST_NATIVE_OPERATION_LIMIT];
     const u32 *list=add[ST_EV_NATIVE]?lists[ST_EV_NATIVE]:events[ST_EV_NATIVE].list;
     u32 view,scope,*q=patches+n;
     native_routes(wide,list);view=native_fill(next.b,list);native_merge(next.b,wide);
#pragma GCC unroll 1
     for(i=0;i<ST_NATIVE_OPERATION_LIMIT;i++){*q++=(u32)(core_native_routes+i);*q++=wide[i];}
#pragma GCC unroll 1
     for(i=0;i<NATIVE_WORDS;i++){*q++=(u32)(core_native_interest+4*i);*q++=next.w[i];}
     *q++=(u32)&core_native_page_view;*q++=view;
     scope=relation_scope(list);
     *q++=(u32)&core_relation_scope;*q++=scope;
     *q++=(u32)&core_relation_offsets;*q++=scope==~0u?~0u:relation_offsets;n=(u32)(q-patches);}
    patches[n]=0;core_event_publish(patches);
    /* Both C context/native lists and assembly operands publish inside the
     * SAME IRQ exclusion. A new render subscriber must never observe an old
     * native list while processing its private synchronous command. */
    core_module_count=count;return 1;
}
