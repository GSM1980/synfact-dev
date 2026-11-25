/* SPDX-License-Identifier: GPL-2.0-or-later
 * SP WAVETBL's encoders, through the core's guarded view events:
 * - SLOT (knob D, either page) opens the pool list (slot-list.c) as the SP
 *   machines open theirs on SAMP; the native edit still turns it.
 * - FUNC + a knob sticks to values: one stop per detent of travel, from the
 *   current value to the next stop in the turning direction. TUNE by octaves,
 *   POS on each whole wave of the slot's table or sample, SPD on each tempo
 *   division (engine.c), LEV on 0/32/64/96/127, DTUN, A.LEV and A.POS on
 *   -64/-32/0/+32/+63. The edit goes through the core (Sound, lock, undo).
 */
#include <syntakt/modules.h>
#include "slot-list.h"
typedef st_u32 u32;typedef unsigned char u8;
#ifndef WT_INPUT_NATIVE
#define WT_INPUT_TYPE ((u32 (*)(void *))0x40030622u)
#define WT_INPUT_DELTA ((int (*)(void *,u32,u32))0x400f6866u)
#define WT_INPUT_GET ((u32 (*)(void *,u32,u8 *))0x40039160u)
#define WT_INPUT_HELD ((u32 (*)(u32))0x40102f40u)
#define WT_INPUT_TRIG ((u32 (*)(void *))0x400c2dd6u)
#endif
#define WT_TYPE 72u
#define WT_BASE (0x1f9u+(WT_TYPE-64u)*32u)
#define WT_SLOT(page) (WT_BASE+3u+8u*(page))
#define WT_KEY_FUNC 1u
#define WT_TRAVEL 16                    /* unaccelerated travel per stop, as the SP discrete steps */
static const struct st_machine_ui_api *wt_input_api;
static struct {u32 view,id;int pending;} wt_stick;

/* The stops of a control (raw Q8 words, ascending), or 0 for none. */
static u32 wt_stops(void *view,u32 index,u32 *stop)
{
    u32 k=index&7u,n=0,i,slot,waves,frames;u8 locked=0;
    static const int bipolar[5]={-64,-32,0,32,63};
    if(index==8 || k==5 || k==6){for(i=0;i<5;i++)stop[n++]=(u32)(0x4000+bipolar[i]*256);return n;}
    switch(k){
    case 0:for(i=0;i<9;i++)stop[n++]=0x1000u+i*12u*256u;return n;             /* -48..+48 by octaves */
    case 1:for(i=0;i<5;i++)stop[n++]=(i<4?i*32u:127u)<<8;return n;
    case 4:for(i=0;i<17;i++)stop[n++]=((i*128u+16u)/17u)<<8;return n;          /* first raw of each division */
    case 2:
        slot=(WT_INPUT_GET(view,WT_BASE+3u+(index&8u),&locked)&65535u)>>8;if(slot>127u)slot=127u;
        waves=wt_input_api->version>=9 && wt_input_api->sample_wavetable?wt_input_api->sample_wavetable(slot):0;
        if(!waves && wt_input_api->version>=9 && wt_input_api->sample_read){
            frames=wt_input_api->sample_read(slot,0,0,0)/2048u;waves=frames>64u?64u:frames;}
        if(waves<2)return 0;
        for(i=0;i<waves;i++)stop[n++]=(i*0x7f00u+waves-2u)/(waves-1u);        /* the first POS on wave i */
        return n;
    }
    return 0;
}
static int wt_input_stick(void *view,void *event,u32 id,u32 index)
{
    u32 stop[64],n,raw,next,i;u8 locked=0,*v=view;int delta,steps,bound;
    n=wt_stops(view,index,stop);
    if(!n)return 0;
    if(wt_stick.view!=(u32)view || wt_stick.id!=id)wt_stick.pending=0;
    wt_stick.view=(u32)view;wt_stick.id=id;
    delta=WT_INPUT_DELTA(event,1,1);
    if((delta<0 && wt_stick.pending>0) || (delta>0 && wt_stick.pending<0))wt_stick.pending=0;
    /* Bound a batched turn to the list (signed: a negative turn is not huge). */
    bound=WT_TRAVEL*(int)n;
    if(delta>bound)delta=bound;else if(delta< -bound)delta= -bound;
    steps=(wt_stick.pending+delta)/WT_TRAVEL;wt_stick.pending=(wt_stick.pending+delta)%WT_TRAVEL;
    if(!steps)return 1;
    raw=WT_INPUT_GET(view,id,&locked)&65535u;next=raw;
    for(;steps>0;steps--){for(i=0;i<n && stop[i]<=next;i++){}if(i<n)next=stop[i];}
    for(;steps<0;steps++){for(i=n;i && stop[i-1]>=next;i--){}if(i)next=stop[i-1];}
    if(next==raw)return 1;
    /* A trig held for a lock: the first edit makes the lock, as the SP input does. */
    if((u8)WT_INPUT_TRIG(*(void **)(v+0x6c)) && (WT_INPUT_HELD(2) || WT_INPUT_HELD(0x12)))
        wt_input_api->edit(view,id,0,index&7u);
    wt_input_api->edit(view,id,(int)next-(int)raw,index&7u);
    return 1;
}
static int wt_input_encoder(void *view,void *event,u32 id)
{
    u8 *v=view;u32 page,index;
    if(!wt_input_api || *(u32 *)(v+0x88)!=0x14 || WT_INPUT_TYPE(view)!=WT_TYPE)return 0;
    page=*(u32 *)(v+0x90);
    if(page<2 && id==WT_SLOT(page)){
        if(WT_INPUT_DELTA(event,1,1))slot_list_begin(wt_input_api,view,WT_TYPE,page,id);
        return 0;        /* the native edit turns SLOT */
    }
    slot_list_close(view);
    index=id-WT_BASE;
    if(page<2 && index<16 && index>>3==page && WT_INPUT_HELD(WT_KEY_FUNC))return wt_input_stick(view,event,id,index);
    wt_stick.pending=0;return 0;
}
static int wt_input_key_pre(void *view,void *event,u32 phase)
{return slot_list_pre_key(view,event,phase);}
static void wt_input_key_post(void *view,void *event,u32 phase)
{wt_stick.pending=0;slot_list_post_key(view,event,phase);}
static int wt_input_init(const struct st_smz_api *api)
{
    if(!api || api->version<3 || !api->ui || api->ui->version<7 || !api->ui->edit)return 1;
    wt_input_api=api->ui;slot_list_reset();wt_stick.view=0;wt_stick.pending=0;return 0;
}
static const struct st_smz_subscription wt_input_events[]={
    {ST_EV_ENC|ST_EV_VIEW,(void (*)(void))wt_input_encoder},
    {ST_EV_KEY|ST_EV_VIEW,(void (*)(void))wt_input_key_post},
    {ST_EV_KEY|ST_EV_VIEW|ST_EV_PRE,(void (*)(void))wt_input_key_pre}};
const struct st_smz_module wt_slot_module={"WT SLOT LIST",wt_input_init,wt_input_events,3};
