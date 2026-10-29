/* SPDX-License-Identifier: GPL-2.0-or-later
 * Machine-owned RATIO B stepping over its 361 combinations (fast digit first,
 * carry into the slow one), using the native edit/lock route on the ONE
 * stored control. Native lock clear, copy/paste and undo need no help.
 * Included by ui.c to share the exact Synfact getter and presentation state. */
#include <syntakt/modules.h>
static const struct st_machine_ui_api *services;
#ifndef FM_POLICY
#define FM_POLICY ((u32 (*)(u32))0x4007b9b2u)
#define FM_EVENT_DELTA ((int (*)(void *,u32,u32))0x400f66a2u)
#define FM_EVENT_PRESSED ((u32 (*)(void *))0x400f66c0u)
#define FM_INCREMENT ((int (*)(void *,u32,int,void *,u32))0x400f68d2u)
#define FM_TRIG_HELD ((u32 (*)(void *))0x400c2d2au)
#define FM_KEY_HELD ((u32 (*)(u32))0x40102d7cu)
#endif
static int encoder(void *view,void *event,u32 id)
{
    u32 policy,increment[7],pressed,i,stored;u8 locked=0;int delta,index,next,steps;
    if(id!=0x21c || *(u32 *)((u8 *)view+0x88)!=0x14 || TYPE(view)!=65 ||
       *(u32 *)((u8 *)view+0x90)!=0)return 0;
    policy=FM_POLICY(0x13u);
    for(i=0;i<7;i++)increment[i]=((u32 *)(*(u32 *)(policy+16)))[i];
    delta=FM_EVENT_DELTA(event,*(u32 *)(policy+4),*(u32 *)(policy+8));
    pressed=FM_EVENT_PRESSED(event);
    delta=FM_INCREMENT((u8 *)view+0x94,3,delta,increment,pressed);
    if(!delta)return 1;
    /* GET returns the lock preview when one applies, else the Sound value. */
    stored=GET(view,0x21cu,&locked)&65535u;index=(int)fm_ratio_index(stored);
    steps=delta/256;if(!steps)steps=delta<0?-1:1;
    next=index+steps;if(next<0)next=0;if(next>(int)FM_RATIO_MAX)next=FM_RATIO_MAX;
    if((u8)FM_TRIG_HELD((void *)*(u32 *)((u8 *)view+0x6c)) &&
       (FM_KEY_HELD(2) || FM_KEY_HELD(0x12)))services->edit(view,0x21c,0,3);
    services->edit(view,0x21c,next*(int)FM_RATIO_STEP-(int)stored,3);
    return 1;
}
static int initialize(const struct st_smz_api *api)
{return !api || api->version<2 || !api->ui || api->ui->version<4?1:(services=api->ui,0);}
static const struct st_smz_subscription subscriptions[]={
    {ST_EV_ENC|ST_EV_VIEW,(void (*)(void))encoder}};
const struct st_smz_module fm_tone_module={"FM TONE INPUT",initialize,subscriptions,1};
