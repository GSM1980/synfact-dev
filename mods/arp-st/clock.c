/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "clock.h"
const arp_u32 arp_clock_periods[18]={
    86400000u/1,86400000u/2,86400000u/3,86400000u/4,
    86400000u/5,86400000u/6,86400000u/8,86400000u/10,
    86400000u/12,86400000u/16,86400000u/20,86400000u/24,
    86400000u/32,86400000u/40,86400000u/48,86400000u/64,
    86400000u/80,86400000u/96
};
void arp_clock_stop(struct arp_clock *c){c->armed=0;}
int arp_clock_arm(struct arp_clock *c,arp_u32 when,unsigned int speed,arp_u32 generation)
{
    c->armed=0;
    if(speed>=18 || !generation)return 0;
    c->next=when;c->period=arp_clock_periods[speed];
    c->generation=generation;c->late=0;c->armed=1;return 1;
}
int arp_clock_poll(struct arp_clock *c,arp_u32 begin,arp_u32 end,arp_u32 generation,arp_u32 *due)
{
    return arp_clock_poll_inline(c,begin,end,generation,due);
}
