/* SPDX-License-Identifier: GPL-2.0-or-later
 * The chorus's controls, as V56 had them: a second REVERB page (page 25) with
 * DPTH SPD HPF WDTH, -, DEL REV VOL, and the CHO send on AMP's knob E (VOL
 * moves to AMP's second page). Metadata, formats and dials answer the core's
 * parameter provider queries; the page layout is ST_NATIVE_PAGE_LAYOUT.
 * Five entries only the chorus uses are hooked here (sites.s), each checked
 * against its stock bytes first, as LFO3 does: the global value provider
 * (kit/get/set), REVERB's draw and AMP's send drawing.
 */
#include <syntakt/modules.h>
#include <syntakt/native.h>
#include <syntakt/host.h>
#include "ids.h"
typedef st_u32 u32;
typedef int s32;
typedef unsigned short u16;
typedef unsigned char u8;
#define DATA __attribute__((section(".data")))
#define DRAW ((void (*)(void *,u32,u32,u32,u32,void *,s32,s32))0x40011e74u)
#define BITMAP ((void (*)(void *,s32,s32,const u32 *,const u32 *))0x400f70fau)
#define BLIT ((void (*)(void *,void *,s32,s32,u32))0x400f8dacu)
#define PAGE_DRAW ((void (*)(void *,void *))0x4003a244u)
#define STRING ((void (*)(void *,const char *,const void *))0x401bf082u)
#define NEW ((u32 *(*)(u32))0x40103788u)
#define FREE ((void (*)(void *))0x40103794u)
#include "widgets.h"

static const char *const short_names[7]={"DPTH","SPD","HPF","WDTH","DEL","REV","VOL"};
static const char *const long_names[7]={"Depth","Speed","High-pass","Width","Delay Send","Reverb Send","Mix Volume"};
static const u16 initial[7]={0x7f00,0x2000,0,0x7f00,0,0,0x6400};
/* Page 25: two native strings, then the eight knobs (E empty). */
static u32 layout[11] DATA={0,0,CHORUS_ID,CHORUS_ID+1,CHORUS_ID+2,CHORUS_ID+3,0,
                            CHORUS_ID+4,CHORUS_ID+5,CHORUS_ID+6,0};
static char text[20] DATA={0};
static u32 widget[7] DATA={0};

static char *integer(char *p,u32 n)
{
    if (n>=100) *p++='0'+n/100;
    if (n>=10) *p++='0'+n/10%10;
    *p++='0'+n%10;*p=0;return p;
}
/* DPTH, SPD, DEL and REV show two decimals, as on the Digitone. */
static const char *format(u32 index,u32 value)
{
    char *p;
    if (value>0x7f00) value=0x7f00;
    p=integer(text,value>>8);
    if (index==0 || index==1 || index==4 || index==5) {
        u32 fraction=((value&255u)*100u)>>8;
        *p++='.';*p++='0'+fraction/10;*p++='0'+fraction%10;*p=0;
    }
    return text;
}
static u32 scaled(u32 raw,u32 n)
{
    if(raw>0x7f00u)raw=0x7f00u;
    return (raw*n+0x3f80u)/0x7f00u;
}
/* Query 30: DEL/REV take the native send dials; HPF, WDTH and the CHO send
 * are 19x17 bitmaps (tools/gen_chorus_widgets.py) blitted natively. */
static int dial(const u32 *a)
{
    u32 id=a[1],raw=a[2];const u32 *bits;
    if(id==CHORUS_ID+4 || id==CHORUS_ID+5){
        DRAW((void *)a[0],id==CHORUS_ID+4?0x4eu:0x4fu,raw,a[3],a[4],(void *)a[5],(s32)a[6],(s32)a[7]);return 1;}
    if(id==CHORUS_ID+2)bits=chorus_hpf_bits[scaled(raw,10)];
    else if(id==CHORUS_ID+3)bits=chorus_width_bits[scaled(raw,4)];
    else if(id==CHORUS_SEND)bits=chorus_cone_bits[scaled(raw,14)];
    else return 0;
    BITMAP(widget,CHORUS_WIDGET_W,CHORUS_WIDGET_H,bits,chorus_widget_mask);
    BLIT((void *)a[5],widget,(s32)a[6],(s32)a[7],0);
    return 1;
}
extern void chorus_params_save(u8 *,const u16 *),chorus_params_load(u16 *,const u8 *);
/* Host API 13 provider. The seven settings answer like V56's generic FX
 * machine (owner 0x31, not a synth or modulation destination, cached
 * record of 0x13, encoder policy of 0x1f1 but HPF/WDTH); the CHO send takes
 * DEL send's (78) cached record. The FX record codec carries the values. */
static int query(u32 q,u32 *args,void *result,u32 *value)
{
    u32 id,i;
    if(q==28 && (args[2]&255u))chorus_params_save((u8 *)args[0],(const u16 *)args[1]);
    if(q==29 && (args[2]&255u))chorus_params_load((u16 *)args[0],(const u8 *)args[1]);
    if(q>=25 && q!=30)return 0;
    if(q==6){if(args[0]-CHORUS_UI>=7u)return 0;*value=CHORUS_ID+args[0]-CHORUS_UI;return 1;}
    id=q==18 || q==20 || q==30?args[1]:args[0];
    if(q==30)return dial(args);
    if(id==CHORUS_SEND)switch(q){
    case 18:*value=(u32)"CHO";return 1;
    case 20:*value=(u32)"Chorus Send";return 1;
    case 17:*value=(u32)format(7,args[1]);return 1;
    case 16:args[0]=78;return 0;
    default:return 0;
    }
    i=id-CHORUS_ID;
    if(i>=7)return 0;
    switch(q){
    case 9:*value=CHORUS_UI+i;return 1;
    case 8:case 14:*value=0x31;return 1;
    case 13:{u32 *out=result;out[0]=0;out[1]=0x7f00;out[2]=initial[i];*value=(u32)out;return 1;}  /* the caller's buffer */
    case 16:args[0]=0x13;return 0;
    case 17:*value=(u32)format(i,args[1]);return 1;
    case 18:*value=(u32)short_names[i];return 1;
    case 20:*value=(u32)long_names[i];return 1;
    case 19:case 21:*value=0;return 1;
    case 23:args[0]=i==2 || i==3?0x13:0x1f1;return 0;
    }
    return 0;
}
/* ST_NATIVE_PAGE_LAYOUT: page 25 is the chorus's. UI task. */
int chorus_native(u32 op,const u32 *args,u32 *result)
{
    if(op!=ST_NATIVE_PAGE_LAYOUT || !args || args[0]!=25)return 0;
    *result=(u32)layout;return 1;
}
/* REVERB's draw (40042ce0, sites.s): give its view a second page, 25, then
 * draw page 25 as an ordinary parameter page. Native vectors, allocation and
 * vtables are kept; nothing of ours is stored in the view. */
u32 chorus_reverb_draw(u8 *view,void *frame)
{
    u32 *pages=*(u32 **)(view+0x7c),*replacement,index;
    if(!pages || pages[0]!=21u)return 0;
    if(*(u32 *)(view+0x80)-(u32)pages==4u){
        if(*(u32 *)(view+0x84)-(u32)pages<8u){
            replacement=NEW(8);
            if(!replacement)return 0;
            replacement[0]=21;replacement[1]=25;
            FREE(pages);pages=replacement;
            *(u32 *)(view+0x7c)=(u32)pages;*(u32 *)(view+0x84)=(u32)(pages+2);
        }else pages[1]=25;
        *(u32 *)(view+0x80)=(u32)(pages+2);
    }
    index=*(u32 *)(view+0x90);
    if(index<2 && pages[index]==25u){PAGE_DRAW(view,frame);return 1;}
    return 0;
}
extern u32 chorus_install(void);
u32 chorus_refused DATA=0;
int chorus_ui_init(const struct st_smz_api *api)
{
    const struct st_machine_host_api *host;
    const char allocator=0;u32 i,*a=(u32 *)0x41b9f61cu,*b=(u32 *)0x41b9f648u;
    static const u32 row[4]={CHORUS_SEND,78,79,80};
    if(!api || !api->ui || api->ui->version<7 || !(host=api->ui->host) || host->version<12){chorus_refused=1;return 0;}
    host->parameter_provider(query);                     /* the settings' codec, always */
    chorus_refused=chorus_install();                            /* stock bytes differ: no controls */
    host->native_interest((void (*)(void))chorus_native,chorus_refused?0:1u<<ST_NATIVE_PAGE_LAYOUT);
    if(chorus_refused)return 0;
    STRING(&layout[0],"CHO",&allocator);STRING(&layout[1],"Chorus",&allocator);
    /* AMP's two layouts: E-H become CHO, DEL, REV, PAN; VOL (81) moves to
     * the empty knob of AMP's second page in each. */
    for(i=0;i<4;i++){a[i]=row[i];b[i]=row[i];}
    *(u32 *)0x41b9f674u=81;*(u32 *)0x41b9f6a0u=81;
    return 0;
}
