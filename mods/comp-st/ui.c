/* SPDX-License-Identifier: GPL-2.0-or-later
 * Original Digitakt labels, ranges, value strings and bitmap animations are
 * rebuilt privately from its OS. Page 39 follows the FX track DRIVE page;
 * F is empty, G is SCF (Digitakt's position and dial), H is MIX. No borrowed
 * owning object copies. */
#include "comp.h"
#include "imported/metadata.h"
#include "imported/formats.h"
#include "imported/dials.h"
#include "time-tables.h"
#define BITMAP ((void (*)(void *,s32,s32,const u32 *,const u32 *))0x400f72beu)
#define BLIT ((void (*)(void *,void *,s32,s32,u32))0x400f8f70u)
#define FILL ((void (*)(void *,s32,s32,s32,s32,s32))0x400f7fb6u)
#define STRING ((void (*)(void *,const char *,const void *))0x401bf31eu)
#define NEW ((u32 *(*)(u32))0x4010394cu)
#define FREE ((void (*)(void *))0x40103958u)
#define INVALIDATE ((void (*)(void *))0x400ffe22u)
static u32 layout[11] DATA={0,0,COMP_ID,COMP_ID+1,COMP_ID+2,COMP_ID+3,COMP_ID+4,0,COMP_ID+COMP_SCF,COMP_ID+5,0};
/* SCF's Digitakt dial: OFF shows its flat centre (frame 13); the cutoffs,
 * 60 Hz to 1 kHz, its high-pass frames 15 to 21, spread evenly in pitch. */
static u32 scf_frame(u32 k){return k?15u+(6u*(k-1u)+63u)/126u:13u;}
static u32 widget[7] DATA={0};static char text[8] DATA={0};
static u32 last_height DATA=0;
/* Each meter repaint recomposes the whole screen (double-buffered, every
 * visible view: about 30,000 instructions for this page alone). 0.3 asks
 * for one at most every METER_TICKS UI ticks (30 Hz / 3 = 10 Hz) and ignores
 * 1-pixel moves (0.6 dB), except to reach 0. Other repaints (edits, page
 * changes) still draw the current height. */
#define METER_TICKS 3u
#define METER_STEP 2u
static u32 meter_wait DATA=METER_TICKS;
static u32 meter_height(void)
{
    u32 half_db=comp_meter_read()>>22;
    if(!comp_current_values()[5])return 0;
    return half_db>=63u?50u:half_db*50u/63u;
}
static u32 fx_view(const u8 *v)
{
    u32 vt;if(!v)return 0;vt=*(const u32 *)v;
    if(vt!=0x401c7ad8u && vt!=0x401c8ad8u && vt!=0x401ca37cu &&
       vt!=0x401c9034u && vt!=0x40240294u && vt!=0x401c9a9cu)return 0;
    return *(const u32 *)(v+0x88)==0x14u;
}
static u32 selected(void *raw)
{
    u8 *v=raw;const u32 *p;if(!fx_view(v))return 0;
    p=*(const u32 **)(v+0x7c);if(!p || p[0]!=32u)return 0;
    return *(u32 *)(v+0x80)-(u32)p==8u && *(u32 *)(v+0x90)==1 && p[1]==COMP_PAGE;
}
static void sync_page(void *raw)
{
    u8 *v=raw;u32 *p,*next;if(!fx_view(v))return;
    p=*(u32 **)(v+0x7c);
    if(!p || p[0]!=32u || *(u32 *)(v+0x80)-(u32)p!=4u)return;
    if(*(u32 *)(v+0x84)-(u32)p<8u){
        next=NEW(8);if(!next)return;next[0]=32;next[1]=COMP_PAGE;
        *(u32 **)(v+0x7c)=next;*(u32 **)(v+0x84)=next+2;FREE(p);p=next;
    }else p[1]=COMP_PAGE;
    *(u32 **)(v+0x80)=p+2;
}
int comp_native(u32 op,const u32 *a,u32 *result)
{
    if(!a)return 0;
    if(op==ST_NATIVE_PAGE_LAYOUT && a[0]==COMP_PAGE){*result=(u32)layout;return 1;}
    if(op==ST_NATIVE_PAGE_SYNC || op==ST_NATIVE_PAGE_NAV){
        sync_page((void *)a[0]);
        if(op==ST_NATIVE_PAGE_NAV && selected((void *)a[0]) && a[1]<8){*result=layout[2+a[1]];return 1;}
    }
    return 0;
}
void comp_draw(void *view,void *frame,u32 phase)
{
    u32 height;
    (void)phase;sync_page(view);if(!selected(view))return;
    /* Digitakt 1.54, 40038f80: quantize to half-dB, 50*GR/63,
     * anchored at y=50 and growing down on the LCD. Its x=126 line would
     * hit Syntakt's page dots at x=125..127. Controls end at x=119:
     * use two columns 122..123, retaining clear gutters on either side. */
    height=meter_height();last_height=height;
    FILL(frame,122,1,123,50,0);
    if(height)FILL(frame,122,51-(s32)height,123,50,1);
}
void comp_tick(void *raw)
{
    u8 *ctrl=raw,*node,*end,*view;
    comp_check_state();
    if(!ctrl)return;
    end=ctrl+0x14;node=*(u8 **)(ctrl+0x1c);
    /* Native visible range, as core_machine_tick. No retained view pointer;
     * hidden/closed views and views below opaque overlays do no meter work. */
    while(node && node!=end){
        view=*(u8 **)(node+8);node=*(u8 **)node;
        if(!selected(view) || !*(u32 *)(view+0x2c) || view[0x17] || view[0x30])continue;
        {
            u32 height=meter_height(),step=height>last_height?height-last_height:last_height-height;
            if(meter_wait<METER_TICKS)meter_wait++;
            if(step && meter_wait>=METER_TICKS && (step>=METER_STEP || !height)){meter_wait=0;INVALIDATE(view);}
        }
        break;
    }
}
static const char *format(u32 i,u32 value)
{
    u32 n=value>>8;char *p=text;if(value>comp_maximum[i])n=comp_maximum[i]>>8;
    if(i==1)return comp_atk_text[n];      /* time constants in ms (time-tables.h) */
    if(i==2)return comp_rel_text[n];
    if(i==3)return comp_mup_text[n];
    if(i==4)return comp_rat_text[n];
    if(i==COMP_SCF){
        u32 hz=comp_scf_hz_table[n];
        if(!n)return "OFF";
        if(hz>=1000u)return "1kHz";
        if(hz>=100u)*p++='0'+hz/100;
        *p++='0'+hz/10%10;*p++='0'+hz%10;*p++='H';*p++='z';*p=0;return text;
    }
    if(n>=100)*p++='0'+n/100;
    if(n>=10)*p++='0'+n/10%10;
    *p++='0'+n%10;*p=0;return text;
}
static int dial(const u32 *a)
{
    u32 i=a[1]-COMP_ID,raw=a[2],frame;const u32 *bits;
    if(i>=COMP_COUNT)return 0;if(raw>comp_maximum[i])raw=comp_maximum[i];
    if(i==0){frame=(raw*14u+0x3f80u)/0x7f00u;bits=comp_thr_bits[frame];}
    else if(i==1 || i==2){frame=(raw*12u+0x3f80u)/0x7f00u;bits=comp_time_bits[frame];}
    else if(i==3){frame=(raw*72u+0x3f80u)/0x7f00u+12u;bits=comp_mup_bits[frame];}
    else if(i==4)bits=comp_rat_bits[raw>>8];
    else if(i==COMP_SCF)bits=comp_scf_bits[scf_frame(raw>>8)];
    else {frame=(raw*17u+0x3f80u)/0x7f00u;bits=comp_mix_bits[frame];}
    BITMAP(widget,19,17,bits,bits+19);BLIT((void *)a[5],widget,(s32)a[6],(s32)a[7],0);return 1;
}
int comp_query(u32 q,u32 *a,void *out,u32 *value)
{
    u32 id,i;if(q==34){comp_meta(a);return 0;}
    if(q>=25 && q<=29)return 0;
    if(q==35)return 0;
    if(q==6){if(a[0]-COMP_UI>=COMP_COUNT)return 0;*value=COMP_ID+a[0]-COMP_UI;return 1;}
    id=q==18 || q==20 || q==24 || q==30 || q==32 || q==33?a[1]:a[0];
    i=id-COMP_ID;if(i>=COMP_COUNT)return 0;
    switch(q){
    case 8:case 14:*value=0x31;return 1;
    case 9:*value=COMP_UI+i;return 1;
    case 13:{u32 *r=out;r[0]=0;r[1]=comp_maximum[i];r[2]=comp_initial[i];*value=(u32)r;return 1;}
    case 16:case 23:a[0]=0x13;return 0;
    case 17:*value=(u32)format(i,a[1]);return 1;
    case 18:*value=(u32)comp_short_names[i];return 1;
    /* The long name titles the value pop-up only: ATK and REL carry their
     * unit there (their values, also drawn under the controls, have none). */
    case 20:*value=(u32)(i==1?"Attack (ms)":i==2?"Release (ms)":comp_long_names[i]);return 1;
    case 19:case 21:*value=0;return 1;
    case 24:return comp_param_set(id,(s32)a[2])==0;
    case 30:return dial(a);
    case 31:return 1;
    case 32:*value=a[-1]!=0x4003a3c4u;return 1;
    case 33:*value=(u32)comp_param_get(id);return 1;
    }
    return 0;
}
int comp_ui_init(const struct st_smz_api *api)
{
    const char allocator=0;const struct st_machine_host_api *host=api->ui->host;
    host->parameter_provider(comp_query);
    host->native_interest((void (*)(void))comp_native,(1u<<ST_NATIVE_PAGE_LAYOUT)|(1u<<ST_NATIVE_PAGE_SYNC)|(1u<<ST_NATIVE_PAGE_NAV));
    STRING(layout,"COMP",&allocator);STRING(layout+1,"Compressor",&allocator);return 0;
}
