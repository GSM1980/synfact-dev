/* SPDX-License-Identifier: GPL-2.0-or-later
 * Discrete-control stepping and extended FUNC TUNE. Uses the core's guarded
 * view event and native Sound/lock edit path; no private input hook or DSP work.
 */
#include <syntakt/modules.h>
#include "browser.h"
#include "allocate.h"
typedef st_u32 u32;typedef unsigned char u8;
#ifndef SP_INPUT_NATIVE
#define SP_INPUT_TYPE ((u32 (*)(void *))0x40030622u)
#define SP_INPUT_TRACK ((u32 (*)(void *))0x4001ff74u)
#define SP_INPUT_GET ((u32 (*)(void *,u32,u8 *))0x40039160u)
#define SP_INPUT_DELTA ((int (*)(void *,u32,u32))0x400f66a2u)
#define SP_INPUT_PRESSED ((u32 (*)(void *))0x400f66c0u)
#define SP_INPUT_TRIG ((u32 (*)(void *))0x400c2d2au)
#define SP_INPUT_HELD ((u32 (*)(u32))0x40102d7cu)
#define SP_INPUT_POLICY ((u32 (*)(u32))0x4007b9b2u)
#define SP_INPUT_INCREMENT ((int (*)(void *,u32,int,void *,u32))0x400f68d2u)
#define SP_INPUT_SNAP_ARM ((void (*)(void *,u32))0x400f6ad4u)
#define SP_INPUT_SNAP_BLOCKED ((u32 (*)(void *,u32))0x400f6b02u)
#define SP_INPUT_SNAP_ACTIVE (*(u8 *)0x4060c748u)
#define SP_INPUT_SNAP_LOOKUP ((int (*)(int,int,const void *))0x4007b97eu)
#endif
#define SP_DISCRETE_STEPS 16
extern const struct core_machine_def sp_one_shot,sp_werp,sp_repitch,sp_slice;
static const struct core_machine_def *const sp_input_machines[]={
    &sp_one_shot,&sp_werp,&sp_repitch,&sp_slice};
static const struct st_machine_ui_api *sp_input_api;
static struct {
    u32 view,type,track,page,id,raw,locked,pressed,func;
    int pending;
} sp_input;
static void sp_input_reset(void *view,void *event,u32 id)
{(void)view;(void)event;(void)id;sp_input.view=0;sp_input.pending=0;}
static void sp_input_edit(void *view,u32 id,int delta,u32 knob)
{
    u8 *v=view;
    if((u8)SP_INPUT_TRIG(*(void **)(v+0x6c)) &&
       (SP_INPUT_HELD(2) || SP_INPUT_HELD(0x12)))
        sp_input_api->edit(view,id,0,knob);
    sp_input_api->edit(view,id,delta,knob);
}
static int sp_input_steps(void *view,void *event,u32 id,u32 type,u32 page,
                          u32 raw,u32 locked,u32 func,int choices)
{
    u32 track=SP_INPUT_TRACK(*(void **)((u8 *)view+0x74));
    u32 pressed=(u8)SP_INPUT_PRESSED(event);
    int delta=SP_INPUT_DELTA(event,1,1),total,bound=choices*SP_DISCRETE_STEPS;
    if(sp_input.view!=(u32)view || sp_input.type!=type || sp_input.track!=track ||
       sp_input.page!=page || sp_input.id!=id || sp_input.raw!=raw ||
       sp_input.locked!=locked || sp_input.pressed!=pressed || sp_input.func!=func ||
       (delta<0 && sp_input.pending>0) || (delta>0 && sp_input.pending<0))
        sp_input.pending=0;
    sp_input.view=(u32)view;sp_input.type=type;sp_input.track=track;
    sp_input.page=page;sp_input.id=id;sp_input.raw=raw;
    sp_input.locked=locked;sp_input.pressed=pressed;sp_input.func=func;
    /* Count unaccelerated travel, equally with FUNC or the encoder pressed.
     * Clamp before adding the remainder, including malformed extreme batches. */
    if(delta>bound)delta=bound;else if(delta< -bound)delta= -bound;
    total=sp_input.pending+delta;sp_input.pending=total%SP_DISCRETE_STEPS;
    return total/SP_DISCRETE_STEPS;
}
/* Native ordered-list layout: sentinel and {next,previous,signed semitones}.
 * Private immutable list; the stock TUNE list/cache are never modified. */
struct sp_snap_node {const struct sp_snap_node *next,*previous;int note;};
static const struct sp_snap_node sp_snap_notes[]={
    {sp_snap_notes+1,sp_snap_notes+8,0},
    {sp_snap_notes+2,sp_snap_notes,-60},
    {sp_snap_notes+3,sp_snap_notes+1,-48},
    {sp_snap_notes+4,sp_snap_notes+2,-36},
    {sp_snap_notes+5,sp_snap_notes+3,-24},
    {sp_snap_notes+6,sp_snap_notes+4,-12},
    {sp_snap_notes+7,sp_snap_notes+5,0},
    {sp_snap_notes+8,sp_snap_notes+6,12},
    {sp_snap_notes,sp_snap_notes+7,24}};
static u32 sp_input_octave(u32 raw,int delta,const struct core_machine_param *parameter)
{
    int note=SP_INPUT_SNAP_LOOKUP(((int)raw-0x4000)>>8,delta,sp_snap_notes);
    u32 next=(u32)(0x4000+note*256);
    if(next<parameter->min)next=parameter->min;
    else if(next>parameter->max)next=parameter->max;
    return next;
}
static int sp_input_tune(void *view,void *event,u32 id,
                         const struct core_machine_param *parameter)
{
    u32 policy,increment[7],i,raw,next;u8 locked=0;int delta;
    void *state=(u8 *)view+0x94;
    sp_input_reset(view,event,id);
    if(!SP_INPUT_HELD(1))return 0; /* Ordinary/fine turning stays native. */
    policy=SP_INPUT_POLICY(0x1f1);
    for(i=0;i<7;i++)increment[i]=((const u32 *)(*(u32 *)(policy+16)))[i];
    delta=SP_INPUT_DELTA(event,*(u32 *)(policy+4),*(u32 *)(policy+8));
    delta=SP_INPUT_INCREMENT(state,0,delta,increment,(u8)SP_INPUT_PRESSED(event));
    if(!delta)return 1;
    /* Match native SourcePageView's FUNC transition and snap gate after the
     * native increment. The view already owns its periodic counter service. */
    if(!SP_INPUT_SNAP_ACTIVE)SP_INPUT_SNAP_ARM(state,0);
    SP_INPUT_SNAP_ACTIVE=1;
    if((u8)SP_INPUT_SNAP_BLOCKED(state,0))return 1;
    raw=SP_INPUT_GET(view,id,&locked)&65535u;
    next=sp_input_octave(raw,delta,parameter);
    sp_input_edit(view,id,(int)next-(int)raw,0);
    /* Stock quantizer sets did_snap even at the limits: re-arm in both cases. */
    SP_INPUT_SNAP_ARM(state,0);
    return 1;
}
static int sp_input_encoder(void *view,void *event,u32 id)
{
    u8 *v=view,locked=0;u32 type,page,index,raw;
    const struct core_machine_param *parameter;
    int steps,next,minimum,maximum;
    if(sp_allocate_encoder(view)){sp_input_reset(view,event,id);return 1;}
    if(!sp_input_api || *(u32 *)(v+0x88)!=0x14)goto decline;
    type=SP_INPUT_TYPE(view);page=*(u32 *)(v+0x90);
    if(type<66 || type>69 || page>=2)goto decline;
    index=id-(0x1f9u+(type-64)*32);
    if(index==3){
        sp_input_reset(view,event,id);
        if(SP_INPUT_DELTA(event,1,1))sp_browser_begin(sp_input_api,view,type,page);
        return 0; /* Native edit still owns Sound/lock/undo and SAMP stepping. */
    }
    sp_browser_close(view);
    if(index==0 && type!=68)
        return sp_input_tune(view,event,id,sp_input_machines[type-66]->parameter);
    if(index!=1 && !(type==67 && index>=4 && index<=6) &&
       !(type==68 && index==6) && !(type==69 && (index==4 || index==6)))goto decline;
    parameter=sp_input_machines[type-66]->parameter+index;
    minimum=(int)(parameter->min>>8);maximum=(int)(parameter->max>>8);
    raw=SP_INPUT_GET(view,id,&locked)&65535u;
    /* Bound batched arithmetic to this descriptor's range. SLCE keeps all
     * NOTE/1..64 positions; SEG/GRID/BARS keep their own encoded choices. */
    steps=sp_input_steps(view,event,id,type,page,raw,locked,SP_INPUT_HELD(1)!=0,
                         maximum-minimum+1);
    if(!steps)return 1;
    next=(int)(raw>>8);if(next<minimum)next=minimum;else if(next>maximum)next=maximum;next+=steps;
    if(next<=minimum){next=minimum;sp_input.pending=0;}
    else if(next>=maximum){next=maximum;sp_input.pending=0;}
    sp_input_edit(view,id,(next<<8)-(int)raw,index);
    sp_input.raw=SP_INPUT_GET(view,id,&locked)&65535u;sp_input.locked=locked;
    return 1;
decline:
    sp_browser_close(view);sp_input_reset(view,event,id);return 0;
}
static int sp_input_key_pre(void *view,void *event,u32 phase)
{
    int taken=sp_browser_pre_key(view,event,phase);
    if(!taken)taken=sp_allocate_pre_key(sp_input_api,view,event,phase);
    if(taken)sp_input_reset(view,event,phase);return taken;
}
static void sp_input_key_post(void *view,void *event,u32 phase)
{sp_input_reset(view,event,phase);sp_browser_post_key(view,event,phase);}
static int sp_input_init(const struct st_smz_api *api)
{
    if(!api || api->version<3 || !api->ui || api->ui->version<7 || !api->ui->edit)return 1;
    sp_input_api=api->ui;sp_input_reset(0,0,0);sp_browser_reset();sp_allocate_reset();return 0;
}
static const struct st_smz_subscription sp_input_events[]={
    {ST_EV_ENC|ST_EV_VIEW,(void (*)(void))sp_input_encoder},
    {ST_EV_KEY|ST_EV_VIEW,(void (*)(void))sp_input_key_post},
    {ST_EV_KEY|ST_EV_VIEW|ST_EV_PRE,(void (*)(void))sp_input_key_pre}};
const struct st_smz_module sp_sampler_input_module={
    "SP DISCRETE INPUT",sp_input_init,sp_input_events,3};
