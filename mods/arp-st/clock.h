/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYNFACT_ST_ARP_CLOCK_H
#define SYNFACT_ST_ARP_CLOCK_H
#include "arp.h"
/* Native MAIN musical time, not UI ticks or elapsed wall-clock time.
 * Callers supply the actual frame [begin,end), including external tempo sync.
 * A discontinuity/STOP/pattern replacement must stop or rearm this timeline. */
struct arp_clock {arp_u32 next,period,generation,armed,late;};
/* Exact musical periods for the eighteen supported divisions. This is a
 * constant lookup, not cached runtime state; invalid speeds have no period. */
extern const arp_u32 arp_clock_periods[18];
static __inline__ arp_u32 arp_clock_period(unsigned int speed)
{return speed<18 ? arp_clock_periods[speed] : 0;}
int arp_clock_arm(struct arp_clock *,arp_u32 when,unsigned int speed,arp_u32 generation);
void arp_clock_stop(struct arp_clock *);
/* The frame consumer can fold this poll into its own state checks instead of
 * preparing five arguments and a separate frame on every inactive interval. */
static __inline__ int arp_clock_poll_inline(struct arp_clock *c,arp_u32 begin,arp_u32 end,
                                          arp_u32 generation,arp_u32 *due)
{
    if(!c->armed)return 0;
    if(c->generation!=generation){c->armed=0;return 0;}
    if((int)(end-begin)<=0)return 0;
    /* Bounded recovery: one trigger maximum; never replay a missed backlog. */
    if((int)(c->next-begin)<0){c->next=begin;c->late++;}
    if((int)(c->next-end)>=0)return 0;
    *due=c->next;c->next+=c->period;
    if((int)(c->next-end)<0){c->next=end;c->late++;}
    return 1;
}
int arp_clock_poll(struct arp_clock *,arp_u32 begin,arp_u32 end,
                   arp_u32 generation,arp_u32 *due);
#endif
