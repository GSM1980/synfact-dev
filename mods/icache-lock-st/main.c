/* SPDX-License-Identifier: GPL-2.0-or-later
 * I-cache lock: keeps 4 KB of the stock audio block's hottest code in ways
 * 0-1 of MAIN's 8 KB instruction cache (lock.s, lines.c). No sites, no
 * patches, no UI. Every code change in the core and the mods ends with an
 * instruction-cache invalidate, which also unlocks the cache, so the lock is
 * applied again once a second from the UI task's tick.
 */
#include <syntakt/modules.h>
#define ICACHE_LOCK_PERIOD 30u   /* ticks; ev_tick runs 30 times a second */
extern const unsigned short icache_lock_tags[256];
extern void icache_lock_apply(const unsigned short *tags);
static st_u32 ticks;
static int start(const struct st_smz_api *api){ticks=0;return api->version==1?0:1;}
static void tick(void *controller)
{
    (void)controller;
    if(++ticks<ICACHE_LOCK_PERIOD)return;
    ticks=0;
    icache_lock_apply(icache_lock_tags);
}
static const struct st_smz_subscription events[] __attribute__((aligned(4)))={
    {ST_EV_TICK,(void (*)(void))tick}};
const struct st_smz_module icache_lock_module={"ICACHE LOCK",start,events,1};
