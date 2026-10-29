/* SPDX-License-Identifier: GPL-2.0-or-later
 * SLICE allocation menu at the original SYN view layer, sharing existing
 * guarded pre-KEY/ENC events. Native per-step locks only, no parallel store.
 * Included by ui.c for its bounded native drawing primitives.
 */
#include <syntakt/pattern.h>
#ifndef SP_ALLOCATE_NATIVE
#define SP_ALLOCATE_TRACK st_pattern_active_track
#define SP_ALLOCATE_DATA st_pattern_track_data
#define SP_ALLOCATE_LENGTH st_pattern_track_length
#define SP_ALLOCATE_PRESENT st_pattern_step_present
#define SP_ALLOCATE_GET st_pattern_lock_get
#define SP_ALLOCATE_SET st_pattern_lock_set
#define SP_ALLOCATE_CLEAR st_pattern_lock_clear
#define SP_ALLOCATE_SELECTING ((u32 (*)(void *))0x400c3294u)
#define SP_ALLOCATE_CLOCK ((u32 (*)(void))0x400c2caau)
#define SP_ALLOCATE_NOTIFY ((void (*)(void *,const char *,...))0x400c33d0u)
#endif
#define SP_SLICE_BASE (0x1f9u+5u*32u)
static struct {
    const struct st_machine_ui_api *api;
    u32 view,vtable,model,selection,track,page,choice,down,down_view;
    void *pattern_track,*pattern_data;
    const char *status;
} sp_allocate;
static u32 sp_allocate_random=0x6d2b79f5u;
void sp_allocate_reset(void)
{sp_allocate.view=sp_allocate.down=sp_allocate.down_view=sp_allocate.choice=0;sp_allocate.api=0;}
static int sp_allocate_eligible(void *view)
{
    u8 *v=view;const u32 *pages;u32 vt;
    if(!v)return 0;vt=*(u32 *)v;
    if(vt!=0x401c7838u && vt!=0x401c8838u && vt!=0x401ca0dcu &&
       vt!=0x401c8d94u && vt!=0x4023ff74u && vt!=0x401c97fcu)return 0;
    if(*(u32 *)(v+0x88)!=0x14 || *(u32 *)(v+0x90)>=2)return 0;
    /* Same source-page ownership gate as the core: no machine selector,
     * held-trig/selected-step edit context or foreign parameter page. */
    pages=*(const u32 **)(v+0x7c);
    return pages && pages[0]==6 && SP_BROWSER_TYPE(view)==69 &&
        !(u8)SP_ALLOCATE_SELECTING(*(void **)(v+0x6c)) &&
        !(u8)SP_BROWSER_TRIG(*(void **)(v+0x6c)) &&
        !(u8)SP_BROWSER_RECORD(*(void **)(v+0x6c));
}
static int sp_allocate_matches(void *view)
{
    u8 *v=view;void *track;
    if(!sp_allocate.view || sp_allocate.view!=(u32)view ||
       *(u32 *)v!=sp_allocate.vtable || *(u32 *)(v+0x74)!=sp_allocate.model ||
       *(u32 *)(v+0x6c)!=sp_allocate.selection ||
       *(u32 *)(v+0x90)!=sp_allocate.page || !sp_allocate_eligible(view) ||
       SP_BROWSER_TRACK(*(void **)(v+0x74))!=sp_allocate.track)return 0;
    track=SP_ALLOCATE_TRACK(*(void **)(v+0x68));
    return track && track==sp_allocate.pattern_track &&
        SP_ALLOCATE_DATA(track)==sp_allocate.pattern_data;
}
static void sp_allocate_close(void *view)
{
    u32 redraw=sp_allocate.view && sp_allocate.view==(u32)view;
    sp_allocate.view=0;if(redraw)SP_BROWSER_INVALIDATE(view);
}
int sp_allocate_active(void *view){return sp_allocate_matches(view);}
int sp_allocate_idle(void *view)
{
    if(!sp_allocate.view || sp_allocate.view!=(u32)view)return 0;
    if(!sp_allocate_matches(view)){sp_allocate.view=0;return 1;}return 0;
}
int sp_allocate_encoder(void *view)
{
    if(!sp_allocate.view)return 0;
    if(sp_allocate_matches(view))return 1;
    sp_allocate_close(view);return 0;
}
static u32 sp_allocate_next(u32 value)
{value^=value<<13;value^=value>>17;value^=value<<5;return value;}
static void sp_allocate_apply(void *view)
{
    u8 steps[64],locked=0;int before[64];u32 values[64];
    u32 n=0,step,length,grid,i,seed=sp_allocate_random;
    if(!sp_allocate_matches(view)){sp_allocate_close(view);return;}
    length=SP_ALLOCATE_LENGTH(sp_allocate.pattern_track);
    if(!length || length>64){sp_allocate.status="INVALID TRACK";return;}
    grid=(SP_BROWSER_GET(view,SP_SLICE_BASE+6,&locked)&65535u)>>8;
    if(grid>4)grid=4;grid=4u<<grid;
    for(step=0;step<length;step++)if(SP_ALLOCATE_PRESENT(sp_allocate.pattern_track,step)){
        steps[n]=(u8)step;before[n]=SP_ALLOCATE_GET(view,step,SP_SLICE_BASE+4);
        if(sp_allocate.choice){seed=sp_allocate_next(seed);values[n]=((seed&(grid-1))+1)<<8;}
        else values[n]=((n%grid)+1)<<8;
        n++;
    }
    if(!n){sp_allocate.status="NO TRIGS";return;}
    /* Absolute native setters preserve all other parameters and trig fields.
     * Read back each edit; on refusal restore this parameter's prior locks,
     * including absence. No partially applied allocation is reported as OK. */
    for(i=0;i<n;i++){
        SP_ALLOCATE_SET(view,steps[i],SP_SLICE_BASE+4,values[i]);
        if(SP_ALLOCATE_GET(view,steps[i],SP_SLICE_BASE+4)!=(int)values[i]){
            u32 j;int restored=1;
            for(j=0;j<=i;j++){
                if(before[j]<0)SP_ALLOCATE_CLEAR(view,steps[j],SP_SLICE_BASE+4);
                else SP_ALLOCATE_SET(view,steps[j],SP_SLICE_BASE+4,(u32)before[j]);
                if(SP_ALLOCATE_GET(view,steps[j],SP_SLICE_BASE+4)!=before[j])restored=0;
            }
            sp_allocate.status=restored?"COULD NOT APPLY":"CHECK SLICE LOCKS";return;
        }
    }
    if(sp_allocate.choice)sp_allocate_random=seed;
    sp_allocate.status="LOCKS CREATED";
}
int sp_allocate_pre_key(const struct st_machine_ui_api *api,void *view,void *event,u32 phase)
{
    const u32 *words=event;u32 key,flags,bit=0,was_down;u8 *v=view;void *track;
    if(phase!=1 || !v)return 0;
    key=words[3];flags=words[4];if(key>=12 && key<=15)bit=1u<<(key-12);
    was_down=sp_allocate.down_view==(u32)view?sp_allocate.down&bit:0;
    if(!(flags&1u) && bit && (sp_allocate.down&bit)){
        sp_allocate.down&=~bit;return sp_allocate.down_view==(u32)view;
    }
    if(sp_allocate.view && !sp_allocate_matches(view))sp_allocate_close(view);
    if(!sp_allocate.view){
        if(key!=12 || !(flags&1u) || (flags&(2u|8u)) || was_down ||
           !api || sp_browser_active(view) || !sp_allocate_eligible(view))return 0;
        track=SP_ALLOCATE_TRACK(*(void **)(v+0x68));
        if(!track || !SP_ALLOCATE_DATA(track))return 0;
        sp_allocate.api=api;sp_allocate.view=(u32)view;sp_allocate.vtable=*(u32 *)v;
        sp_allocate.model=*(u32 *)(v+0x74);sp_allocate.selection=*(u32 *)(v+0x6c);
        sp_allocate.page=*(u32 *)(v+0x90);sp_allocate.track=SP_BROWSER_TRACK(*(void **)(v+0x74));
        sp_allocate.pattern_track=track;sp_allocate.pattern_data=SP_ALLOCATE_DATA(track);
        sp_allocate.status=0;
        sp_allocate_random^=SP_ALLOCATE_CLOCK();if(!sp_allocate_random)sp_allocate_random=0x6d2b79f5u;
        sp_allocate.down_view=(u32)view;sp_allocate.down=bit;
        SP_BROWSER_INVALIDATE(view);return 1;
    }
    if(!bit || (flags&2u)){
        if(flags&1u)sp_allocate_close(view);return 0;
    }
    if(!(flags&1u))return 1;
    sp_allocate.down_view=(u32)view;sp_allocate.down|=bit;
    if(was_down || (flags&8u))return 1;
    if(key==13)sp_allocate_close(view);
    else {
        if(key==12){
            sp_allocate_apply(view);sp_allocate_close(view);
            if(sp_allocate.status)
                SP_ALLOCATE_NOTIFY(*(void **)(v+0x6c),"%s",sp_allocate.status);
        }
        else {sp_allocate.choice=key==15;sp_allocate.status=0;}
        SP_BROWSER_INVALIDATE(view);
    }
    return 1;
}
int sp_allocate_draw(void *view,void *ctx)
{
    void *font;int y;
    if(!sp_allocate_matches(view))return 0;
    font=SP_BROWSER_FONT(FONT);
    FILL(ctx,24,4,125,52,0);FRAME(ctx,24,4,125,52,1);
    SP_BROWSER_TEXT(ctx,font,29,44,92,"ALLOCATE SLICE");
    FILL(ctx,28,41,121,41,1);
    SP_BROWSER_TEXT(ctx,font,29,33,93,"CREATE LINEAR LOCKS");
    SP_BROWSER_TEXT(ctx,font,29,24,93,"CREATE RANDOM LOCKS");
    y=sp_allocate.choice?23:32;FILL(ctx,27,y,122,y+7,0xffffffffu);
    SP_BROWSER_TEXT(ctx,font,29,7,93,"NO: EXIT");return 1;
}
