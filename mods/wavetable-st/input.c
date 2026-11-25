/* SPDX-License-Identifier: GPL-2.0-or-later
 * SY WAVETABLE's SLOT list: turning SLOT (knob D, on either page) opens the
 * pool list (slot-list.c) as the SP machines open theirs on SAMP. The native
 * edit still owns the Sound, the lock and undo; this only shows the list and
 * handles NO (revert) and YES. Uses the core's guarded view events.
 */
#include <syntakt/modules.h>
#include "slot-list.h"
typedef st_u32 u32;typedef unsigned char u8;
#ifndef WT_INPUT_NATIVE
#define WT_INPUT_TYPE ((u32 (*)(void *))0x40030622u)
#define WT_INPUT_DELTA ((int (*)(void *,u32,u32))0x400f6866u)
#endif
#define WT_TYPE 72u
#define WT_SLOT(page) (0x1f9u+(WT_TYPE-64u)*32u+3u+8u*(page))
static const struct st_machine_ui_api *wt_input_api;
static int wt_input_encoder(void *view,void *event,u32 id)
{
    u8 *v=view;u32 page;
    if(!wt_input_api || *(u32 *)(v+0x88)!=0x14 || WT_INPUT_TYPE(view)!=WT_TYPE)return 0;
    page=*(u32 *)(v+0x90);
    if(page<2 && id==WT_SLOT(page)){
        if(WT_INPUT_DELTA(event,1,1))slot_list_begin(wt_input_api,view,WT_TYPE,page,id);
        return 0;        /* the native edit turns SLOT */
    }
    slot_list_close(view);return 0;
}
static int wt_input_key_pre(void *view,void *event,u32 phase)
{return slot_list_pre_key(view,event,phase);}
static void wt_input_key_post(void *view,void *event,u32 phase)
{slot_list_post_key(view,event,phase);}
static int wt_input_init(const struct st_smz_api *api)
{
    if(!api || api->version<3 || !api->ui || api->ui->version<7 || !api->ui->edit)return 1;
    wt_input_api=api->ui;slot_list_reset();return 0;
}
static const struct st_smz_subscription wt_input_events[]={
    {ST_EV_ENC|ST_EV_VIEW,(void (*)(void))wt_input_encoder},
    {ST_EV_KEY|ST_EV_VIEW,(void (*)(void))wt_input_key_post},
    {ST_EV_KEY|ST_EV_VIEW|ST_EV_PRE,(void (*)(void))wt_input_key_pre}};
const struct st_smz_module wt_slot_module={"WT SLOT LIST",wt_input_init,wt_input_events,3};
