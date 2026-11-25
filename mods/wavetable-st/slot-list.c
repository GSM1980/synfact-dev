/* SPDX-License-Identifier: GPL-2.0-or-later
 * The pool-slot list (slot-list.h): the SP sample list of samplers 1.6
 * (browser.inc), step for step, with the machine type, page and control id
 * given by the caller instead of the SP types and SAMP, and a WT tag.
 */
#include "slot-list.h"
typedef st_u32 u32;typedef unsigned char u8;
#ifndef SLOT_LIST_NATIVE
#define SL_TYPE ((u32 (*)(void *))0x40030622u)
#define SL_TRACK ((u32 (*)(void *))0x4001ff74u)
#define SL_GET ((u32 (*)(void *,u32,u8 *))0x40039160u)
/* Trigs held within the track length (400c3582) and live REC (400c3062), as
 * samplers 1.6.1 and the subtype picker read them. */
#define SL_TRIG ((u32 (*)(void *))0x400c3582u)
#define SL_RECORD ((u32 (*)(void *))0x400c3062u)
#define SL_INVALIDATE ((void (*)(void *))0x400ffe22u)
#define SL_FONT ((void *(*)(void *))0x400f9478u)
#define SL_TEXT ((void (*)(void *,void *,int,int,u32,const char *))0x400f8b68u)
#define SL_FILL ((void (*)(void *,int,int,int,int,u32))0x400f7fb6u)
#define SL_PIXEL ((void (*)(void *,int,int,u32))0x400f7304u)
#define SL_SMALL ((void *)0x443db210u)
#endif
#define SL_TICKS 240u
#define SL_ROWS 7u
#define SL_KEY_YES 12u
#define SL_KEY_NO 13u
static struct {
    const struct st_machine_ui_api *api;
    u32 view,vtable,model,type,page,track,id,original,locked,trig,ticks,shown;
    u32 swallow,swallow_view;
} sl;
static char *sl_number(char *p,u32 n)
{if(n>=100)*p++='0'+n/100;if(n>=10)*p++='0'+n/10%10;*p++='0'+n%10;*p=0;return p;}
void slot_list_reset(void)
{sl.view=sl.ticks=sl.swallow=sl.swallow_view=0;sl.api=0;}
void slot_list_close(void *view)
{
    u32 redraw=sl.view && sl.view==(u32)view;
    sl.view=sl.ticks=0;
    if(redraw)SL_INVALIDATE(view);
}
static int sl_matches(void *view)
{
    u8 *v=view;
    return sl.view==(u32)view && sl.view &&
        *(u32 *)v==sl.vtable && *(u32 *)(v+0x88)==0x14 &&
        *(u32 *)(v+0x74)==sl.model && *(u32 *)(v+0x90)==sl.page &&
        SL_TYPE(view)==sl.type &&
        SL_TRACK(*(void **)(v+0x74))==sl.track &&
        !!(u8)SL_TRIG(*(void **)(v+0x6c))==sl.trig &&
        !(u8)SL_RECORD(*(void **)(v+0x6c));
}
int slot_list_active(void *view){return sl_matches(view);}
void slot_list_begin(const struct st_machine_ui_api *api,void *view,u32 type,u32 page,u32 id)
{
    u8 *v=view,locked=0;u32 raw;
    if(!api || !api->sample_name || !api->edit || !api->clear)return;
    /* Live recording can touch several lock lanes: keep it native. */
    if((u8)SL_RECORD(*(void **)(v+0x6c))){slot_list_close(view);return;}
    if(!sl_matches(view) || sl.id!=id){
        sl.api=api;sl.view=(u32)view;sl.vtable=*(u32 *)v;
        sl.model=*(u32 *)(v+0x74);sl.type=type;sl.page=page;
        sl.track=SL_TRACK(*(void **)(v+0x74));sl.id=id;
        raw=SL_GET(view,id,&locked)&65535u;
        sl.original=raw;sl.locked=locked;
        sl.trig=!!(u8)SL_TRIG(*(void **)(v+0x6c));
        sl.shown=128;
    }
    sl.ticks=SL_TICKS;SL_INVALIDATE(view);
}
int slot_list_idle(void *view,u32 type,u32 page)
{
    u8 locked=0;u32 slot;
    if(!sl.view || sl.view!=(u32)view)return 0;
    if(type!=sl.type || page!=sl.page || !sl_matches(view) || !sl.ticks || !--sl.ticks){sl.view=0;return 1;}
    slot=(SL_GET(view,sl.id,&locked)&65535u)>>8;if(slot>127)slot=127;
    return slot!=sl.shown;
}
int slot_list_draw(void *view,void *ctx,u32 type,u32 page)
{
    const struct st_machine_ui_api *api=sl.api;
    char name[33],row[40],*p;const char *s,*tag;u8 locked=0;
    u32 slot,first,i,n,j;int y,thumb;void *font;
    if(!sl.view || sl.view!=(u32)view || type!=sl.type || page!=sl.page || !sl_matches(view))return 0;
    slot=(SL_GET(view,sl.id,&locked)&65535u)>>8;if(slot>127)slot=127;
    first=slot>3?slot-3:0;if(first>128-SL_ROWS)first=128-SL_ROWS;
    font=SL_FONT(SL_SMALL);
    /* Native rows count upwards: 54..63 is the header. The list covers the
     * page right of the track rail (x < 20), the controls included. */
    SL_FILL(ctx,20,0,127,50,0);
    for(i=0;i<SL_ROWS;i++){
        n=first+i;p=row;if(n<9)*p++='0';p=sl_number(p,n+1);*p++=':';
        name[0]=0;name[32]=0;s=api->sample_name(n,name)?name:"NO SAMPLE";
        for(j=0;*s && j<32;j++)*p++=*s++;*p=0;
        y=44-(int)i*7;
        SL_TEXT(ctx,font,22,y,101,row);
        /* The text call does not clip: clear under the scroll bar, and under
         * a tag, so a long name never runs into either. */
        SL_FILL(ctx,124,y-1,127,y+5,0);
        tag=0;
        if(api->version>=9 && api->sample_wavetable && api->sample_wavetable(n))tag="WT";
        else if(api->version>=8 && api->sample_channels && api->sample_channels(n)==2)tag="ST";
        if(tag){SL_FILL(ctx,111,y-1,123,y+5,0);SL_TEXT(ctx,font,114,y,10,tag);}
        if(n==slot)SL_FILL(ctx,20,y-1,123,y+5,0xffffffffu);
    }
    for(y=1;y<=50;y++){SL_PIXEL(ctx,126,y,(u32)y&1u);SL_PIXEL(ctx,127,y,((u32)y^1u)&1u);}
    thumb=47-(int)(first*46u/(128-SL_ROWS));
    SL_FILL(ctx,125,thumb,127,thumb+3,1);
    SL_FILL(ctx,20,54,127,63,0);
    SL_TEXT(ctx,font,23,56,104,"Press NO to revert");
    SL_FILL(ctx,20,54,127,63,0xffffffffu);
    sl.shown=slot;return 1;
}
int slot_list_pre_key(void *view,void *event,u32 phase)
{
    const u32 *words=event;u32 key,flags,raw;u8 locked=0;
    if(phase!=1)return 0;
    key=words[3];flags=words[4];
    if(sl.swallow){
        if(sl.swallow_view==(u32)view && key==sl.swallow){
            if(!(flags&1u))sl.swallow=0;return 1;}
        if(key==sl.swallow && !(flags&1u))sl.swallow=0;
    }
    if(!sl.view)return 0;
    if(!sl_matches(view)){slot_list_close(view);return 0;}
    if((key!=SL_KEY_YES && key!=SL_KEY_NO) || !(flags&1u) || (flags&8u))return 0;
    if(key==SL_KEY_NO){
        raw=SL_GET(view,sl.id,&locked)&65535u;
        if(sl.trig && !sl.locked && locked)sl.api->clear(view,sl.id,0,3);
        else if(raw!=sl.original)sl.api->edit(view,sl.id,(int)sl.original-(int)raw,3);
    }
    sl.swallow=key;sl.swallow_view=(u32)view;
    slot_list_close(view);return 1;
}
/* A held key's repeats (flag 8) are not a new action: a trig held for a
 * p-lock repeats while the control turns. */
void slot_list_post_key(void *view,void *event,u32 phase)
{
    (void)phase;
    if(event && (((const u32 *)event)[4]&8u))return;
    slot_list_close(view);
}
