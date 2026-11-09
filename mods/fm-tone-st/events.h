/* SPDX-License-Identifier: GPL-2.0-or-later
 * Machine-owned RATIO B stepping over its 361 combinations (fast digit first,
 * carry into the slow one), using the native edit/lock route on the ONE
 * stored control, and FUNC + encoder snaps (fm_func). Native lock clear,
 * copy/paste and undo need no help.
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
#ifndef FM_SNAP_ARM
#define FM_SNAP_ARM ((void (*)(void *,u32))0x400f6ad4u)
#define FM_SNAP_BLOCKED ((u32 (*)(void *,u32))0x400f6b02u)
#define FM_SNAP_ACTIVE (*(volatile u8 *)0x4060c748u)
#endif
/* List item 29: FUNC + encoder snaps HARM to -24/-12/0/+12/+24 (centre 3f00,
 * not TUNE's 4000), MIX to -64/0/+63 and the four RATIO OFFSETs to
 * -1/0/+0.999. As FUNC+TUNE on the sampler machines: the control's native
 * encoder policy and increment, then the native FUNC-entry/repetition gate
 * (400f6ad4/400f6b02) and its shared transition flag; every accepted snap
 * re-arms it, at an end too. One snap per accepted increment, to the next
 * stored value strictly past the current one; none past the last. */
static const u32 fm_snap_harm[5]={0x2700,0x3300,0x3f00,0x4b00,0x5700};
static const u32 fm_snap_mix[3]={0,0x4000,0x7f00};
static const u32 fm_snap_offset[3]={0,0x4000,0x7ffe};
static u32 fm_snap_next(u32 raw,int delta,const u32 *list,u32 n)
{
    u32 i;
    if(delta>0){for(i=0;i<n;i++)if(list[i]>raw)return list[i];}
    else for(i=n;i--;)if(list[i]<raw)return list[i];
    return raw;
}
static void fm_edit(void *view,u32 id,int delta,u32 knob)
{
    if((u8)FM_TRIG_HELD((void *)*(u32 *)((u8 *)view+0x6c)) &&
       (FM_KEY_HELD(2) || FM_KEY_HELD(0x12)))services->edit(view,id,0,knob);
    services->edit(view,id,delta,knob);
}
static int fm_func(void *view,void *event,u32 id,u32 page)
{
    const u32 *list;u32 n,knob,alias,policy,increment[7],i,raw,next;u8 locked=0;int delta;
    void *state=(u8 *)view+0x94;
    if(page==0 && id==0x21du){list=fm_snap_harm;n=5;knob=4;alias=0x1f1;}
    else if(page==0 && id==0x220u){list=fm_snap_mix;n=3;knob=7;alias=0x13;}
    else if(page==1 && (id==0x230u || (id>=0x232u && id<=0x234u))){
        list=fm_snap_offset;n=3;knob=id==0x230u?0:id-0x231u;alias=0x1f1;
    }else return 0;
    if(!FM_KEY_HELD(1))return 0;   /* ordinary and pressed turning stay native */
    policy=FM_POLICY(alias);
    for(i=0;i<7;i++)increment[i]=((u32 *)(*(u32 *)(policy+16)))[i];
    delta=FM_EVENT_DELTA(event,*(u32 *)(policy+4),*(u32 *)(policy+8));
    delta=FM_INCREMENT(state,knob,delta,increment,(u8)FM_EVENT_PRESSED(event));
    if(!delta)return 1;
    if(!FM_SNAP_ACTIVE)FM_SNAP_ARM(state,knob);
    FM_SNAP_ACTIVE=1;
    if((u8)FM_SNAP_BLOCKED(state,knob))return 1;
    /* GET returns the lock preview when one applies, else the Sound value. */
    raw=GET(view,id,&locked)&65535u;next=fm_snap_next(raw,delta,list,n);
    fm_edit(view,id,(int)next-(int)raw,knob);
    FM_SNAP_ARM(state,knob);
    return 1;
}
static int encoder(void *view,void *event,u32 id)
{
    u32 policy,increment[7],pressed,i,stored;u8 locked=0;int delta,index,next,steps;
    if(*(u32 *)((u8 *)view+0x88)!=0x14 || TYPE(view)!=65)return 0;
    if(id!=0x21c)return fm_func(view,event,id,*(u32 *)((u8 *)view+0x90));
    if(*(u32 *)((u8 *)view+0x90)!=0)return 0;
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
    fm_edit(view,0x21c,next*(int)FM_RATIO_STEP-(int)stored,3);
    return 1;
}
static int initialize(const struct st_smz_api *api)
{return !api || api->version<2 || !api->ui || api->ui->version<4?1:(services=api->ui,0);}
static const struct st_smz_subscription subscriptions[]={
    {ST_EV_ENC|ST_EV_VIEW,(void (*)(void))encoder}};
const struct st_smz_module fm_tone_module={"FM TONE INPUT",initialize,subscriptions,1};
