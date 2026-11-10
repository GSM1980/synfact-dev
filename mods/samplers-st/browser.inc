/* SPDX-License-Identifier: GPL-2.0-or-later
 * Sample list painted at the SYN view's own layer. Names are borrowed from
 * the existing loaded-pool cache; no native view, disk IO or allocation.
 * Included by ui.c to share its native clipping/drawing primitives.
 */
#ifndef SP_BROWSER_NATIVE
#define SP_BROWSER_TYPE ((u32 (*)(void *))0x40030622u)
#define SP_BROWSER_TRACK ((u32 (*)(void *))0x4001ff74u)
#define SP_BROWSER_GET ((u32 (*)(void *,u32,u8 *))0x40039160u)
#define SP_BROWSER_TRIG ((u32 (*)(void *))0x400c2d2au)
#define SP_BROWSER_RECORD ((u32 (*)(void *))0x400c34d6u)
#define SP_BROWSER_INVALIDATE ((void (*)(void *))0x400ffc5eu)
#define SP_BROWSER_FONT ((void *(*)(void *))0x400f92b4u)
#define SP_BROWSER_TEXT ((void (*)(void *,void *,int,int,u32,const char *))0x400f89a4u)
#endif
#define SP_BROWSER_TICKS 240u
#define SP_BROWSER_ROWS 7u
static struct {
    const struct st_machine_ui_api *api;
    u32 view,vtable,model,type,page,track,id,original,locked,trig,ticks,shown;
    u32 swallow,swallow_view;
} sp_browser;
void sp_browser_reset(void)
{sp_browser.view=sp_browser.ticks=sp_browser.swallow=sp_browser.swallow_view=0;sp_browser.api=0;}
void sp_browser_close(void *view)
{
    u32 redraw=sp_browser.view && sp_browser.view==(u32)view;
    sp_browser.view=sp_browser.ticks=0;
    if(redraw)SP_BROWSER_INVALIDATE(view);
}
static int sp_browser_matches(void *view)
{
    u8 *v=view;
    return sp_browser.view==(u32)view && sp_browser.view &&
        *(u32 *)v==sp_browser.vtable && *(u32 *)(v+0x88)==0x14 &&
        *(u32 *)(v+0x74)==sp_browser.model && *(u32 *)(v+0x90)==sp_browser.page &&
        SP_BROWSER_TYPE(view)==sp_browser.type &&
        SP_BROWSER_TRACK(*(void **)(v+0x74))==sp_browser.track &&
        !!(u8)SP_BROWSER_TRIG(*(void **)(v+0x6c))==sp_browser.trig &&
        !(u8)SP_BROWSER_RECORD(*(void **)(v+0x6c));
}
int sp_browser_active(void *view){return sp_browser_matches(view);}
void sp_browser_begin(const struct st_machine_ui_api *api,void *view,u32 type,u32 page)
{
    u8 *v=view,locked=0;u32 raw;
    if(!api || !api->sample_name || !api->edit || !api->clear ||
       type<66 || type>69 || page>=2)return;
    /* Live recording can touch multiple lock lanes. Keep that transaction
     * native instead of promising that one saved value can revert it. */
    if((u8)SP_BROWSER_RECORD(*(void **)(v+0x6c))){sp_browser_close(view);return;}
    if(!sp_browser_matches(view)){
        sp_browser.api=api;sp_browser.view=(u32)view;sp_browser.vtable=*(u32 *)v;
        sp_browser.model=*(u32 *)(v+0x74);sp_browser.type=type;sp_browser.page=page;
        sp_browser.track=SP_BROWSER_TRACK(*(void **)(v+0x74));
        sp_browser.id=0x1f9u+(type-64)*32+3;
        raw=SP_BROWSER_GET(view,sp_browser.id,&locked)&65535u;
        sp_browser.original=raw;sp_browser.locked=locked;
        sp_browser.trig=!!(u8)SP_BROWSER_TRIG(*(void **)(v+0x6c));
        sp_browser.shown=128;
    }
    sp_browser.ticks=SP_BROWSER_TICKS;SP_BROWSER_INVALIDATE(view);
}
int sp_browser_idle(const struct st_machine_ui_api *api,const struct core_machine_def *m,void *view,u32 page)
{
    u8 locked=0;u32 slot;(void)api;
    if(!sp_browser.view || sp_browser.view!=(u32)view)return 0;
    if(m->type!=sp_browser.type || page!=sp_browser.page || !sp_browser_matches(view) ||
       !sp_browser.ticks || !--sp_browser.ticks){sp_browser.view=0;return 1;}
    slot=(SP_BROWSER_GET(view,sp_browser.id,&locked)&65535u)>>8;if(slot>127)slot=127;
    return slot!=sp_browser.shown;
}
int sp_browser_draw(const struct st_machine_ui_api *api,const struct core_machine_def *m,void *view,void *ctx,u32 page)
{
    char name[33],row[40],*p;const char *s;u8 locked=0;
    u32 slot,first,i,n;int y,thumb;void *font;
    if(!sp_browser.view || sp_browser.view!=(u32)view || m->type!=sp_browser.type ||
       page!=sp_browser.page || !sp_browser_matches(view))return 0;
    slot=(SP_BROWSER_GET(view,sp_browser.id,&locked)&65535u)>>8;if(slot>127)slot=127;
    first=slot>3?slot-3:0;if(first>128-SP_BROWSER_ROWS)first=128-SP_BROWSER_ROWS;
    font=SP_BROWSER_FONT(FONT);
    /* Native LCD coordinates increase upwards: y=54..63 is the header.
     * The list covers the whole page right of the track rail (x < 20), the
     * SYN controls included, so long names fit. */
    FILL(ctx,20,0,127,50,0);
    for(i=0;i<SP_BROWSER_ROWS;i++){
        n=first+i;p=row;if(n<9)*p++='0';p=number(p,n+1);*p++=':';
        name[0]=0;name[32]=0;s=api->sample_name(n,name)?name:"NO SAMPLE";
        for(u32 j=0;*s && j<32;j++)*p++=*s++;*p=0;
        y=44-(int)i*7;
        SP_BROWSER_TEXT(ctx,font,22,y,101,row);
        /* The native text call does not clip at its width: clear the right end
         * under the scroll bar, and under the tag of a stereo slot (UI API 8,
         * core 1.8), so a long name never runs into either. */
        FILL(ctx,124,y-1,127,y+5,0);
        if(api->version>=8 && api->sample_channels && api->sample_channels(n)==2){
            FILL(ctx,111,y-1,123,y+5,0);SP_BROWSER_TEXT(ctx,font,114,y,10,"ST");
        }
        if(n==slot)FILL(ctx,20,y-1,123,y+5,0xffffffffu);
    }
    for(y=1;y<=50;y++){PIXEL(ctx,126,y,(u32)y&1u);PIXEL(ctx,127,y,((u32)y^1u)&1u);}
    thumb=47-(int)(first*46u/(128-SP_BROWSER_ROWS));
    FILL(ctx,125,thumb,127,thumb+3,1);
    FILL(ctx,20,54,127,63,0);
    SP_BROWSER_TEXT(ctx,font,23,56,104,"Press NO to revert");
    FILL(ctx,20,54,127,63,0xffffffffu);
    sp_browser.shown=slot;return 1;
}
int sp_browser_pre_key(void *view,void *event,u32 phase)
{
    const u32 *words=event;u32 key,flags,raw;u8 locked=0;
    if(phase!=1)return 0;
    key=words[3];flags=words[4];
    if(sp_browser.swallow){
        if(sp_browser.swallow_view==(u32)view && key==sp_browser.swallow){
            if(!(flags&1u))sp_browser.swallow=0;return 1;}
        if(key==sp_browser.swallow && !(flags&1u))sp_browser.swallow=0;
    }
    if(!sp_browser.view)return 0;
    if(!sp_browser_matches(view)){sp_browser_close(view);return 0;}
    if((key!=12 && key!=13) || !(flags&1u) || (flags&8u))return 0;
    if(key==13){
        raw=SP_BROWSER_GET(view,sp_browser.id,&locked)&65535u;
        if(sp_browser.trig && !sp_browser.locked && locked)
            sp_browser.api->clear(view,sp_browser.id,0,3);
        else if(raw!=sp_browser.original)
            sp_browser.api->edit(view,sp_browser.id,(int)sp_browser.original-(int)raw,3);
    }
    sp_browser.swallow=key;sp_browser.swallow_view=(u32)view;
    sp_browser_close(view);return 1;
}
void sp_browser_post_key(void *view,void *event,u32 phase)
{(void)event;(void)phase;sp_browser_close(view);}
