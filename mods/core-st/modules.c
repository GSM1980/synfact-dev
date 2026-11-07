/* SPDX-License-Identifier: GPL-2.0-or-later
 * Optional SMZ modules join the ordinary event lists once, without a proxy
 * dispatcher or a per-event scan of modules. Only core-owned list references
 * are published; stock firmware and the file contain no runtime patch plan.
 */
#include <syntakt/modules.h>
#include <syntakt/native.h>
typedef st_u32 u32;
extern void *fw_malloc(u32);extern void fw_free(void *);
extern const struct st_machine_ui_api core_machine_ui_api;
extern void core_machine_input_reset(void);
extern void core_event_publish(const u32 *);
#define VIEW_ENTRY 0x80000000u
#define EVENT(name) extern const u32 ev_##name[];extern const unsigned char core_##name##_table[];
EVENT(tick) EVENT(draw) EVENT(key) EVENT(enc) EVENT(settings)
EVENT(render_in) EVENT(render_out) EVENT(fs_route) EVENT(fs_write) EVENT(fs_written) EVENT(main_mix)
EVENT(track_send) EVENT(delay_in) EVENT(reverb_in)
extern const u32 ev_native[];
#define E(name) {ev_##name,core_##name##_table+2}
static struct {const u32 *list;const unsigned char *operand;} events[]={
    E(tick),E(draw),E(key),E(enc),E(settings),E(render_in),E(render_out),
    E(fs_route),E(fs_write),E(fs_written),E(main_mix),{ev_native,0},
    E(track_send),E(delay_in),E(reverb_in)};
typedef char event_table_size[sizeof events/sizeof events[0]==ST_EV_COUNT?1:-1];
/* One byte per native operation: nonzero sends it through the C dispatcher,
 * zero lets its assembly adapter replay stock at once. A subscriber is
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
unsigned char core_native_interest[4*NATIVE_WORDS] __attribute__((aligned(4)))=
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0xff,1,1,1};
/* Per subscriber: operation mask (host API 5) and page view class (API 6).
 * A full table leaves the newcomer at every operation and view (open). */
#define NATIVE_SLOTS 4
#define NATIVE_PAGE_OPS ((1u<<ST_NATIVE_PAGE_SYNC)|(1u<<ST_NATIVE_PAGE_NAV))
static struct {u32 fn,mask,view;} native_masks[NATIVE_SLOTS];
/* PAGE_SYNC/PAGE_NAV reach the dispatcher only for views whose vtable is this
 * word (machine-pages.c); 0 = every view. Nonzero only when every subscriber
 * of those operations declared the same view class. Published with the list. */
u32 core_native_page_view;
/* The union of the listed subscribers' masks, one byte per operation; returns
 * the page view filter. Shared by startup, host API 5/6 and bind (cold). */
__attribute__((noinline)) static u32 native_fill(unsigned char *out,const u32 *list)
{
    u32 fn,m=0,t=0,i,own,view,pv=0,any=0;
    while((fn=*list++)){
        own=~0u;view=0;
#pragma GCC unroll 1
        for(i=0;i<NATIVE_SLOTS;i++)if(native_masks[i].fn==fn){own=native_masks[i].mask;view=native_masks[i].view;break;}
        m|=own;
        if(own>>ST_NATIVE_TYPE&1u)t|=own>>24 && own>>24!=0xffu?own>>24:0x100u;
        if(own&NATIVE_PAGE_OPS){if(!view || (pv && pv!=view))any=1;pv=view;}
    }
#pragma GCC unroll 1
    for(i=0;i<4*NATIVE_WORDS;i++,m>>=1)out[i]=(unsigned char)(m&1u);
    out[NATIVE_TYPE_TRACKS]=(unsigned char)t;out[NATIVE_TYPE_TRACKS+1]=(unsigned char)(t>>8);
    return any?0:pv;
}
void core_native_initialize(void)
{core_native_page_view=native_fill(core_native_interest,events[ST_EV_NATIVE].list);}
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
    e[field]=value;
    core_native_page_view=native_fill(core_native_interest,events[ST_EV_NATIVE].list);
}
void core_native_interest_set(void (*callback)(void),u32 mask){native_set(callback,mask,1);}
void core_native_page_view_set(void (*callback)(void),u32 vtable){native_set(callback,vtable,2);}
int core_event_native(u32 operation,const u32 *args,u32 *result)
{
    const u32 *p=events[ST_EV_NATIVE].list;u32 fn;
    while((fn=*p++))if(((int (*)(u32,const u32 *,u32 *))fn)(operation,args,result))return 1;
    return 0;
}
u32 core_module_count,core_module_subscriptions;
/* These two audited modules write resident code during init. Their stock
 * preimages are checked by machine-boot.c before any initializer runs, and
 * they initialize after every module that may reject for an API/resource
 * reason. Subscription order below stays in the bank's original order. */
static int module_name_is(const char *name,const char *wanted)
{
    u32 i=0;while(name[i] && name[i]==wanted[i])i++;
    return !name[i] && !wanted[i];
}
static int patching_module(const char *name)
{return module_name_is(name,"LFO3") || module_name_is(name,"MODULATION SKIP");}
/* Reuse the published collection, selecting only subscribers that requested
 * its native-view context. No module scan, input registry or retained view. */
int core_event_view(u32 event,void *view,void *data,u32 id)
{
    const u32 *p=events[event].list;u32 fn;int taken;
    while((fn=*p++))if(fn&VIEW_ENTRY){
        if(event==ST_EV_KEY && !!(fn&1)!=(id==1))continue;
        if(event==ST_EV_ENC || (event==ST_EV_KEY && id==1)){
            taken=((int (*)(void *,void *,u32))(fn&~(VIEW_ENTRY|1u)))(view,data,id);
            if(taken)return taken;
        }else ((void (*)(void *,void *,u32))(fn&~VIEW_ENTRY))(view,data,id);
    }
    return 0;
}
int core_modules_bind(const struct st_smz_module *mods,u32 count)
{
    u32 i,j,k,e,phase,old[ST_EV_COUNT],add[ST_EV_COUNT],used=0,total=0,patches[4*ST_EV_COUNT+2*NATIVE_WORDS+3],n=0;
    u32 *memory,*dst,*lists[ST_EV_COUNT];struct st_smz_api api={1,&core_machine_ui_api};
    core_machine_input_reset();
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
            core_machine_input_reset();if(memory)fw_free(memory);return 0;}
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
    {union {unsigned char b[4*NATIVE_WORDS];u32 w[NATIVE_WORDS];} next;
     u32 view=native_fill(next.b,add[ST_EV_NATIVE]?lists[ST_EV_NATIVE]:events[ST_EV_NATIVE].list),*q=patches+n;
#pragma GCC unroll 1
     for(i=0;i<NATIVE_WORDS;i++){*q++=(u32)(core_native_interest+4*i);*q++=next.w[i];}
     *q++=(u32)&core_native_page_view;*q++=view;n=(u32)(q-patches);}
    patches[n]=0;core_event_publish(patches);
    /* Both C context/native lists and assembly operands publish inside the
     * SAME IRQ exclusion. A new render subscriber must never observe an old
     * native list while processing its private synchronous command. */
    core_module_count=count;return 1;
}
