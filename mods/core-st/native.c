/* SPDX-License-Identifier: GPL-2.0-or-later
 * Synchronous native operations missing from the ordinary UI/audio events.
 * Core owns the sites and exact stock replay. Optional modules only subscribe.
 */
#include <syntakt/native.h>
typedef unsigned int u32;
extern int core_event_native(u32,const u32 *,u32 *);
extern u32 core_native(u32,const u32 *);
extern unsigned char core_native_interest[];
extern u32 core_stock_trig(u32,u32,u32,u32,u32,u32);
extern u32 core_stock_header_load(u32,u32);
extern void core_stock_header_reset(u32,u32);
extern const u32 *core_stock_page_layout(u32);
/* Not inlined: the adapters already replay stock for uninterested operations,
 * so core_native reaches it only on a declined dispatch (resident RAM). */
__attribute__((noinline)) u32 core_native_original(u32 op,const u32 *a)
{
    switch(op){
    case ST_NATIVE_TRIG:return core_stock_trig(a[0],a[1],a[2],a[3],a[4],a[5]);
    case ST_NATIVE_ENQUEUE:return ((u32 (*)(u32,u32))0x4011a648u)(a[0],a[1]);
    case ST_NATIVE_TYPE:return (u32)(int)*(const signed char *)a[0];
    case ST_NATIVE_HEADER_LOAD:return core_stock_header_load(a[0],a[1]);
    case ST_NATIVE_HEADER_RESET:core_stock_header_reset(a[0],a[1]);return 0;
    case ST_NATIVE_PAGE_LAYOUT:return (u32)core_stock_page_layout(a[0]);
    case ST_NATIVE_SOUND_ALLOWED:case ST_NATIVE_MACHINE_ALLOWED:return 1;
    case ST_NATIVE_PAGE_NAV:return 0xffffffffu;
    default:return 0;
    }
}
/* Optional diagnostic (host API 4): a module's buffer of MAIN timer ticks
 * [0..17] and calls [18..35] per operation, nested calls counted in both.
 * Without one, the only cost is this null test. */
#ifndef CORE_NATIVE_TIMER
#define CORE_NATIVE_TIMER() (*(const volatile u32 *)0xfc07000cu)
#endif
static u32 *volatile native_profile;
void core_native_profile(u32 *buffer){native_profile=buffer;}
#ifdef __mcoldfire__
_Static_assert(ST_NATIVE_OPERATION_LIMIT==22 && ST_NATIVE_OPERATION_COUNT==18,
               "resident native service operation/profiler layout");
/* Private alias for the resident wrapper. Volatile keeps setter stores visible
 * to assembly, including when a callback replaces the profile buffer. */
__asm__(".globl core_native_profile_buffer\n.set core_native_profile_buffer,native_profile\n");
#else
__attribute__((noinline)) u32 core_native(u32 op,const u32 *a)
{
    u32 *p=native_profile,start=0,result=0;
    if(p)start=CORE_NATIVE_TIMER();
    if(op>=ST_NATIVE_OPERATION_LIMIT || !core_native_interest[op] || !core_event_native(op,a,&result))
        result=core_native_original(op,a);
    if(p && op<ST_NATIVE_OPERATION_COUNT){p[op]+=CORE_NATIVE_TIMER()-start;p[ST_NATIVE_OPERATION_COUNT+op]++;}
    return result;
}
#endif
/* Host API 6: PAGE_SYNC/PAGE_NAV for another view class than the one their
 * subscribers declared (modules.c, 0 = every view) replay stock directly. */
extern u32 core_native_page_view;
u32 core_native_view(u32 op,void *view,u32 other)
{
    u32 a[2]={(u32)view,other},v=core_native_page_view;
    if(v && (op==ST_NATIVE_PAGE_SYNC || op==ST_NATIVE_PAGE_NAV) && *(u32 *)view!=v)
        return core_native_original(op,a);
    return core_native(op,a);
}
