/* SPDX-License-Identifier: GPL-2.0-or-later
 * Actual Digi Mono UI and shared picker; only native services are doubles.
 * Adapted from the MD prototype tests recorded in picker-provenance.json.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned values[15],machine=71,track=3,selecting,trigs,recording,edits;
/* the other tracks' machines and TYPEs, for DPRO WAVE's one-track limit */
static unsigned track_machine[8],track_type[8];
#define DM_TRACK_MACHINE(t) track_machine[t]
#define DM_TRACK_TYPE(t) track_type[t]
static unsigned registered,boxes,lists,rectangles,foreign_edits;
static unsigned controls[15][8];
static unsigned pixels[256];
static unsigned get(void *view,unsigned id,unsigned char *locked)
{(void)view;assert(id==0x1f9u+7*32+8);*locked=0;return values[track];}
static void fill(void *ctx,int x1,int y1,int x2,int y2,unsigned color)
{
    (void)ctx;assert(x1>=0 && x2<128 && y1>=0 && y2<64 && x1<=x2 && y1<=y2);
    if(y1==22 && y2==41 && color==1 && x2-x1>=67)boxes++;
    if(x1==20 && y1==0 && x2==127 && y2==63 && !color)lists++;
    rectangles++;
    for(int x=x1;x<=x2;x++)for(int y=y1;y<=y2;y++){
        unsigned mask=1u<<(31-(y%32)),*p=pixels+2*x+y/32;
        if(color)*p|=mask;else *p&=~mask;
    }
}
#define STP_TEST_NATIVE 1
#define STP_GET get
#define STP_VIEW_TYPE(v) ((void)(v),machine)
#define STP_VIEW_TRACK(v) ((void)(v),track)
#define STP_TRIG(v) ((void)(v),trigs)
#define STP_RECORD(v) ((void)(v),recording)
#define STP_SELECTING(v) ((void)(v),selecting)
#define STP_FILL fill
#include "../mods/digimono-st/picker.c"
#define DM_VALUE get
#include "../mods/digimono-st/main.c"
static void edit(void *view,st_u32 id,int delta,st_u32 knob)
{
    (void)view;assert(id==DM_ENGINE+DM_TYPE_CONTROL && knob==DM_TYPE_CONTROL);
    assert(!selecting && !trigs && !recording && machine==DM_TYPE);
    values[track]=(unsigned)((int)values[track]+delta);edits++;
}
static void provider(int (*fn)(unsigned,unsigned *,void *,unsigned *))
{assert(fn==dm_destination_query);registered++;}
static int key(unsigned k,unsigned flags)
{unsigned event[5]={0,0,0,k,flags};return stp_key_global(0,event);}
static int turn(int delta)
{unsigned event[5]={0,0,0,5,(unsigned)delta};return stp_enc_global(0,event);}
static unsigned tick(void)
{unsigned char ctrl[0x40]={0};stp_tick(ctrl);return ctrl[0x20];}
static void seen(void *v){stp_view_draw(v,0,0);}
static int idle(void *v){return dm_idle(0,&dm_monomachine,v,0);}
static void draw(void *v){dm_ui.draw(0,&dm_monomachine,v,pixels,0);}
static void open(void *v)
{
    selecting=1;seen(v);
    assert(key(STP_KEY_YES,1)==1 && stp_sel.active);
    assert(key(STP_KEY_YES,0)==1);
}
static void choose(unsigned type)
{
    assert(turn((int)type-(int)stp_sel.cursor)==1 || stp_sel.cursor==type);
    /* Global encoder events deliberately move one position per event. */
    while(stp_sel.cursor!=type)assert(turn(type>stp_sel.cursor?1:-1));
    assert(!key(STP_KEY_YES,1) && !stp_sel.active);
    assert(!key(STP_KEY_YES,0));selecting=0;
}
int main(void)
{
    unsigned char view[0x110]={0},other[0x110]={0};static unsigned pages[]={6};
    unsigned model,other_model;
    *(unsigned *)(view+0x88)=0x14;*(unsigned **)(view+0x7c)=pages;*(void **)(view+0x6c)=&model;
    memcpy(other,view,sizeof view);*(void **)(other+0x6c)=&other_model;
    struct st_machine_host_api host={0};struct st_machine_ui_api ui={0};struct st_smz_api api={2,&ui};
    host.version=17;host.parameter_provider=provider;ui.version=7;ui.host=&host;ui.edit=edit;
    api.version=1;assert(stp_init(&api));api.version=2;
    ui.version=6;assert(stp_init(&api));ui.version=7;ui.edit=0;assert(stp_init(&api));ui.edit=edit;
    host.version=16;assert(stp_init(&api) && !registered);host.version=17;
    assert(!stp_init(&api) && registered==1);
    assert(dm_monomachine.params==9 && dm_ui.pages==1);
    assert(dm_controls[8].initial==DM_DEFAULT_TYPE<<8 && dm_controls[8].max==(DM_TYPES-1)<<8);
    for(unsigned i=0;i<8;i++)assert(dm_layout[i]!=8);
    for(unsigned t=0;t<15;t++){values[t]=DM_DEFAULT_TYPE<<8;for(unsigned i=0;i<8;i++)controls[t][i]=t*100+i;}
    unsigned saved[15][8];memcpy(saved,controls,sizeof saved);
    values[3]=4<<8;
    selecting=1;seen(view);machine=66;
    assert(!key(STP_KEY_YES,1) && !turn(1) && !stp_sel.active);machine=71;
    open(view);assert(stp_sel.cursor==4 && tick() && !tick());
    unsigned count=lists;stp_frame_draw(pixels,0);assert(lists==count+1 && rectangles>80);
    assert(turn(5) && stp_sel.cursor==5);assert(key(STP_KEY_UP,1) && stp_sel.cursor==4);
    assert(key(STP_KEY_DOWN,3) && stp_sel.cursor==11);assert(key(STP_KEY_DOWN,3) && stp_sel.cursor==14);
    assert(key(STP_KEY_DOWN,1) && stp_sel.cursor==14);
    assert(key(STP_KEY_UP,3) && stp_sel.cursor==7);
    for(unsigned i=0;i<3;i++)assert(key(STP_KEY_UP,1));
    assert(stp_sel.cursor==4);assert(!key(STP_KEY_FUNC,1));
    assert(key(STP_KEY_NO,1) && !stp_sel.active && key(STP_KEY_NO,0));
    assert(!key(STP_KEY_NO,1) && !key(STP_KEY_NO,0) && selecting && !edits);
    open(view);trigs=1;assert(key(STP_KEY_YES,1) && stp_sel.active && !edits);trigs=0;
    recording=1;assert(key(STP_KEY_YES,1) && stp_sel.active && !edits);recording=0;
    choose(10);assert(stp_pend.track==4 && !edits);
    machine=66;assert(!idle(view) || values[3]==4<<8);assert(!edits && stp_pend.track==4);machine=71;
    assert(idle(view) && values[3]==10<<8 && edits==1);
    assert(!idle(view) && edits==1);
    values[3]=DM_DEFAULT_TYPE<<8;tick();assert(idle(view) && values[3]==10<<8 && edits==2);
    /* Once a choice has landed, later user interaction cancels its bounded
     * late-default retry. Loading another Sound cannot reapply an old choice. */
    assert(!key(50,1));values[3]=DM_DEFAULT_TYPE<<8;idle(view);assert(values[3]==DM_DEFAULT_TYPE<<8 && edits==2);
    open(view);choose(8);trigs=1;idle(view);assert(edits==2);trigs=0;recording=1;idle(view);assert(edits==2);
    recording=0;idle(view);assert(values[3]==8<<8 && edits==3);
    open(view);choose(5);track=4;idle(view);assert(values[4]==DM_DEFAULT_TYPE<<8 && edits==3);track=3;
    idle(other);assert(!stp_pend.track && edits==3);
    open(view);choose(1);for(unsigned i=0;i<60;i++)tick();idle(view);assert(values[3]==8<<8 && edits==3);
    open(view);selecting=0;assert(!key(STP_KEY_DOWN,1) && !stp_sel.active && !turn(1));
    open(view);track=4;seen(view);assert(!key(STP_KEY_DOWN,1) && !stp_sel.active);selecting=0;track=3;
    /* Every subtype is loadable and rendered in full; only TYPE is edited. */
    for(unsigned type=0;type<DM_TYPES;type++){
        open(view);choose(type);idle(view);assert(values[3]==type<<8 && dm_shown==type);
        unsigned old_boxes=boxes;draw(view);assert(boxes==old_boxes+1);
        assert(stp_tag_width(dm_names[type])<=96);
        for(const char *s=dm_names[type];*s;s++)assert(stp_glyph(*s));
    }
    assert(!memcmp(saved,controls,sizeof saved) && !foreign_edits);
    /* Name box expires exactly at one second and reappears on another
     * track, another subtype, a SYN return, and across the wrapping clock. */
    stp_tag.view=0;assert(idle(view));unsigned old_boxes=boxes;draw(view);assert(boxes==old_boxes+1);
    for(unsigned i=1;i<30;i++){tick();assert(!idle(view));}
    draw(view);assert(boxes==old_boxes+2);tick();assert(idle(view));draw(view);assert(boxes==old_boxes+2);
    tick();assert(!idle(view));tick();tick();tick();assert(idle(view));
    track=4;assert(idle(view));track=3;assert(idle(view));
    machine=66;seen(view);machine=71;assert(idle(view));
    stp_now=0xfffffff0u;stp_tag.view=0;idle(view);old_boxes=boxes;draw(view);assert(boxes==old_boxes+1);
    for(unsigned i=0;i<30;i++){tick();idle(view);}draw(view);assert(boxes==old_boxes+1);
    /* Closed/foreign views take no native input. */
    assert(!key(STP_KEY_YES,1) && !turn(1));machine=66;draw(view);assert(!key(STP_KEY_YES,1));
    /* DPRO WAVE (12) on one track at a time: struck through elsewhere */
    assert(dm_fits(3,DM_DPWAVE) && dm_fits(3,DM_DPDDRW));
    track_machine[0]=71;track_type[0]=DM_DPWAVE;
    assert(!dm_fits(3,DM_DPWAVE) && dm_fits(0,DM_DPWAVE) && dm_fits(3,DM_DPDENS) && dm_fits(3,0));
    track_machine[0]=70;assert(dm_fits(3,DM_DPWAVE));
    track_machine[0]=71;track_type[0]=DM_DPDDRW;assert(dm_fits(3,DM_DPWAVE) && !dm_fits(3,DM_DPDDRW) && dm_fits(3,DM_DPDENS));
    track_type[0]=DM_DPDENS;assert(!dm_fits(3,DM_DPDENS) && dm_fits(3,DM_DPDDRW));
    for(unsigned k=0;k<DM_TYPES;k++){
        track_machine[0]=71;track_type[0]=k;
        unsigned heavy=k==DM_ENS || (k>=DM_FMSTAT && k<=DM_DPDENS);
        assert(dm_fits(3,k)==!heavy && dm_fits(0,k));
        if(k!=DM_SID)assert(dm_fits(3,DM_SID));
    }
    track_machine[0]=0;track_type[0]=0;
    puts("PASS: hidden TYPE, 15 choices, selector YES/NO/encoders, track/model/recording guards, late defaults, cancellation, bounded name box and wrap; no knob edits");
    return 0;
}
