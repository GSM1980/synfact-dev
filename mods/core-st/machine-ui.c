/* SPDX-License-Identifier: GPL-2.0-or-later
 * Generic machine-owned UI dispatch. No machine graphics or control kinds here.
 * Native caches are borrowed unchanged. No private input registry or vptr edits.
 */
#include "machines.h"
#include "sample-pool.h"
#include <syntakt/native.h>
typedef st_u32 u32;typedef core_u8 u8;
extern u32 fw_view_machine(void *);extern void fw_invalidate(void *);
extern void core_machine_dispatch_delta(void *,u32,int,u32);
extern const struct st_machine_host_api core_machine_host_api;
extern int core_event_view(u32,void *,void *,u32);
extern u32 core_native_view(u32,void *,u32);
static const struct st_machine_action_codec *codecs[16];
const struct st_machine_action_codec *core_machine_action_codec;
static int bind_codec(const struct st_machine_action_codec *c)
{u32 index;const struct core_machine_def *m=core_machine_parameter(c->id,&index);
 if(!m)return 0;codecs[m->type-64]=c;return 1;}
void core_machine_action_select(const u32 *layout)
{
    u32 i,index;const struct core_machine_def *m;
    core_machine_action_codec=0;if(!layout)return;
    for(i=2;i<10;i++){
        m=core_machine_parameter(layout[i],&index);
        if(m && codecs[m->type-64] && codecs[m->type-64]->id==layout[i]){
            core_machine_action_codec=codecs[m->type-64];return;}
    }
}
static int retired_input(u32 type,int (*encoder)(void *,void *,u32),void (*key)(void *,void *))
{(void)type;(void)encoder;(void)key;return 0;}
void core_machine_input_reset(void)
{u32 i;for(i=0;i<16;i++)codecs[i]=0;core_machine_action_codec=0;}
u32 core_machine_edit_id;
u32 core_machine_parameter_slot(u32 id)
{u32 index;return core_machine_parameter(id,&index)?core_machine_slot(index):0;}
static void edit(void *view,u32 id,int delta,u32 knob)
{
    u32 index,old=core_machine_edit_id;
    if(!core_machine_parameter(id,&index))return;
    core_machine_edit_id=id;core_machine_dispatch_delta(view,id,delta,knob);core_machine_edit_id=old;
}
static void clear(void *view,u32 id,u32 whole_page,u32 knob)
{
    u32 index,old=core_machine_edit_id,*vtable=*(u32 **)view;
    if(!core_machine_parameter(id,&index))return;
    core_machine_edit_id=id;((void (*)(void *,u32,u32))vtable[(whole_page?0x68:0x6c)/4])(view,id,knob);
    core_machine_edit_id=old;
}
#define UI_API(context) {7,core_pool_wave,core_pool_wave_idle,core_pool_sample_name,core_pool_cursor,context,edit,clear,retired_input,bind_codec,&core_machine_host_api}
const struct st_machine_ui_api core_machine_ui_api=UI_API(ST_UI_FORMAT_POPUP);
static const struct st_machine_ui_api control_api=UI_API(ST_UI_FORMAT_CONTROL);
#undef UI_API
#define api core_machine_ui_api
static const char *formatted(u32 id,u32 raw,const struct st_machine_ui_api *services)
{
    u32 index;const struct core_machine_def *m=core_machine_parameter(id,&index);
    const struct st_machine_ui *u=core_machine_ui(m);
    return u && (u32)u->format!=CORE_SMZ_NONE?u->format(services,m,index,raw):0;
}
const char *core_machine_format(u32 id,u32 raw)
{return formatted(id,raw,&api);}
u32 core_machine_policy(u32 id)
{
    u32 index;const struct core_machine_def *m=core_machine_parameter(id,&index);
    const struct st_machine_ui *u=core_machine_ui(m);
    return u && (u32)u->policy!=CORE_SMZ_NONE?u->policy(index):CORE_SMZ_NONE;
}
int core_machine_dial(void *model,u32 id,u32 raw,u32 a,u32 b,void *ctx,int x,int y)
{
    u32 index;struct st_machine_dial args={model,id,raw,a,b,ctx,x,y};
    const struct core_machine_def *m=core_machine_parameter(id,&index);
    const struct st_machine_ui *u=core_machine_ui(m);
    return u && (u32)u->dial!=CORE_SMZ_NONE?u->dial(&api,m,index,&args):0;
}
int core_machine_direct_format(u32 id,u32 raw,char *out)
{
    const char *text=formatted(id,raw,&control_api);u32 n=0;
    if(!text)return 0;
    while(n<15 && text[n]){out[n]=text[n];n++;}out[n]=0;return 1;
}
void core_machine_draw(void *raw,void *ctx)
{
    if(!core_machine_active)return;
    if(!core_machine_syn_view(raw))return;
    u8 *v=raw;u32 page=*(u32 *)(v+0x90);
    const struct core_machine_def *m=core_machine_find(fw_view_machine(v));const struct st_machine_ui *u=core_machine_ui(m);
    if(!u || page>=u->pages)return;
    if((u32)u->draw!=CORE_SMZ_NONE)u->draw(&api,m,v,ctx,page);
}
/* One gate before any module encoder: selector ownership and native page
 * guards (including POLY slaves) apply before FM can consume its paired dial. */
int core_machine_editor_event(void *view,void *event,u32 id)
{
    if(core_machine_parameter_view(view) && *(u32 *)((u8 *)view+0x88)==0x14 &&
       !core_machine_syn_view(view))return 1;
    if(core_native_view(ST_NATIVE_PAGE_GUARD,view,0))return 1;
    return core_event_view(ST_EV_ENC,view,event,id);
}
