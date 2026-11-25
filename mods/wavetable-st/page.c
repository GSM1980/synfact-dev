/* SPDX-License-Identifier: GPL-2.0-or-later
 * SP WAVETBL's SYN pages, laid out as the Tonverk's Wavefinder SRC page
 * (docs/SP-WAVETBL.md, "The screen"): page 1 is osc 1, page 2 osc 2.
 *
 *   top row     TUNE (DTUN) box . LEV with its bar . [POS] . W + SLOT
 *   middle      the wave at the position, crossfaded between neighbours,
 *               moving with ANIM while the track plays
 *   track       the position across the table
 *   bottom row  SPD scale . A.LEV and A.POS bipolar bars . ANIM's shape
 *
 * The native rail (x < 19), the header (y > 51) and the page indicator
 * (x > 124) stay native. Native LCD rows count upwards: y 0 is the bottom.
 * p-locked controls are inverted (XOR), as FM TONE marks them.
 * The wave comes from eMMC (UI API 9) when the slot changes: a wavetable's
 * level 5, or a plain sample's waves (2048 frames, at most 64 evenly spaced,
 * as AUDIO plays them) taken every 32nd frame; the moving position from
 * AUDIO's cursor records (audio.c; core 2.8).
 */
#include <syntakt/machines.h>
#include "slot-list.h"
typedef st_u32 u32;typedef int s32;typedef unsigned char u8;
#ifndef WT_UI_PRIMITIVES
#define FILL ((void (*)(void *,s32,s32,s32,s32,u32))0x400f7fb6u)
#define TEXT ((void (*)(void *,void *,s32,s32,u32,const char *,const char *))0x400f8bd0u)
#define SMALL ((void *)0x443db230u)
#define GET ((u32 (*)(void *,u32,u8 *))0x40039160u)
#define TRACK(view) (((u32 (*)(void *))0x4001ff74u)(*(void **)((u8 *)(view)+0x74)))
#define PAGE_INDICATOR ((void (*)(void *,void *,u32,u32,u32))0x4003a1c2u)
#endif
#define XOR 0xffffffffu
#define ID(index) (0x1f9u+(72u-64u)*32u+(index))
/* Columns: four cells of 23 pixels from x 20, as the Tonverk's four knobs. */
#define CX(i) (20+26*(i))
#define CW 22
#define WAVE_X 20
#define WAVE_W 102
#define WAVE_Y 29          /* centre row of the wave */
#define WAVE_H 11          /* half height */
#define TRACK_Y 15
#define LEVEL 5u           /* 64 samples a wave: enough for 102 columns */
#define SIZE 64u

static short view_pcm[64*SIZE],view_read[2048];
static u32 view_slot=0xffffffffu,view_frames,view_waves,view_tries;
static u32 shown[8],idle_calls,moved_at;
/* A moving position (ANIM while the track plays) redraws at most every
 * WT_MOVING_CALLS idle calls, and only for a 32nd of a wave or more. */
#define WT_MOVING_CALLS 2u

static u32 get(void *view,u32 index,u8 *locked){u8 l=0;u32 v=GET(view,ID(index),&l)&65535u;if(locked)*locked=l;return v;}
/* Pixels written straight into the native bitmap, as FM TONE does: +4 width,
 * +8 height, +12 words per column, +16 the 1-bit plane, column-major, row y
 * in bit 31 - (y & 31) of word y >> 5. The native PIX call per pixel made a
 * moving wave cost most of a redraw. The bitmap's fields are read once per
 * draw (canvas()); colour 1 only; clipped. The LCD is 64 rows: 2 words. */
static struct {u32 *plane;s32 stride,width,height;} cv;
static void canvas(void *ctx)
{const s32 *c=ctx;cv.plane=*(u32 *const *)((const u8 *)ctx+16);cv.stride=c[3];cv.width=c[1];cv.height=c[2]>64?64:c[2];}
static __inline__ void dot(void *ctx,s32 x,s32 y)
{
    (void)ctx;
    if((u32)x>=(u32)cv.width || (u32)y>=(u32)cv.height)return;
    cv.plane[x*cv.stride+(y>>5)]|=0x80000000u>>(y&31);
}
/* Rows y0..y1 of column x. */
static __inline__ void span(void *ctx,s32 x,s32 y0,s32 y1)
{
    u32 *col;s32 t;(void)ctx;
    if(y0>y1){t=y0;y0=y1;y1=t;}
    if((u32)x>=(u32)cv.width || y1<0 || y0>=cv.height)return;
    if(y0<0)y0=0;if(y1>=cv.height)y1=cv.height-1;
    col=cv.plane+x*cv.stride;
    if(y1<32)col[0]|=(0xffffffffu>>y0)&(0xffffffffu<<(31-y1));
    else if(y0>=32)col[1]|=(0xffffffffu>>(y0-32))&(0xffffffffu<<(63-y1));
    else{col[0]|=0xffffffffu>>y0;col[1]|=0xffffffffu<<(63-y1);}
}
/* Every other pixel of row y from x0 to x1: one mask, one word per dot. */
static void dot_line(void *ctx,s32 x0,s32 x1,s32 y)
{
    u32 m,*p;s32 x;(void)ctx;
    if((u32)y>=(u32)cv.height)return;
    if(x0<0)x0=0;if(x1>=cv.width)x1=cv.width-1;
    m=0x80000000u>>(y&31);p=cv.plane+(y>>5);
    for(x=x0;x<=x1;x+=2)p[x*cv.stride]|=m;
}
static char *number(char *p,u32 n)
{if(n>=100)*p++='0'+n/100;if(n>=10)*p++='0'+n/10%10;*p++='0'+n%10;*p=0;return p;}
static void signed_text(char *p,s32 v)
{if(v<0){*p++='-';v=-v;}else if(v)*p++='+';number(p,(u32)v);}

/* The source in view, 64 points a wave, read once per slot and content. */
static void load(const struct st_machine_ui_api *api,u32 slot)
{
    u32 frames,n,w,i,j,k;
    if(api->version<9 || !api->wavetable_level || !api->sample_read || !api->sample_wavetable)return;
    frames=api->sample_read(slot,0,0,0);           /* no card read */
    if(slot==view_slot && frames==view_frames && (view_waves || view_tries>=3))return;
    if(slot!=view_slot || frames!=view_frames){view_slot=slot;view_frames=frames;view_waves=0;view_tries=0;}
    view_tries++;
    if(api->sample_wavetable(slot)){view_waves=api->wavetable_level(slot,LEVEL,view_pcm,64);return;}
    n=frames/2048u;w=n>64u?64u:n;
    for(i=0;i<w;i++){
        k=n>64u?(i*(n-1u)+31u)/63u:i;                /* as engine.c's wt_sample_starts */
        if(api->sample_read(slot,k*2048u,2048u,view_read)!=2048u)return;
        for(j=0;j<SIZE;j++)view_pcm[i*SIZE+j]=view_read[j*32u];
    }
    view_waves=w;
}
/* The position in waves, Q8: AUDIO's while the oscillator sounds, else POS. */
static u32 position(const struct st_machine_ui_api *api,void *view,u32 page,u32 slot,u32 pos_raw)
{
    const struct st_machine_cursor *c=api->cursor(TRACK(view)+8u*page,slot);
    u32 p;
    if(!view_waves)return 0;
    if(c && c->active)p=c->position>>3;            /* Q8 waves x 8 (audio.c) */
    else p=pos_raw*(view_waves-1u)*256u/0x7f00u;
    if(p>(view_waves-1u)<<8)p=(view_waves-1u)<<8;
    return p;
}
static void wave(void *ctx,u32 p)
{
    /* The two neighbours mixed once (64 points), then one interpolation a
     * column: the crossfade per column cost most of a redraw. */
    static s32 mixed[SIZE+1];
    u32 w0=p>>8,xf=p&255u,x,j;s32 y,last=0;const short *a,*b;
    if(w0+1u>=view_waves){w0=view_waves-1u;xf=0;}
    a=view_pcm+w0*SIZE;b=a+SIZE;
    for(j=0;j<SIZE;j++)mixed[j]=xf?a[j]+((((s32)b[j]-a[j])*(s32)xf)>>8):a[j];
    mixed[SIZE]=mixed[0];                            /* the wave is a cycle */
    for(x=0;x<WAVE_W;x++){
        u32 at=x*SIZE*256u/WAVE_W,i=at>>8;
        s32 s=mixed[i]+(((mixed[i+1u]-mixed[i])*(s32)(at&255u))>>8);
        y=WAVE_Y+((s*WAVE_H)>>15);
        /* A column's span from the last point to this one keeps the line whole. */
        span(ctx,WAVE_X+(s32)x,x?last:y,y);
        last=y;
    }
}
/* ANIM's shape, 20 x 6, with the engine's formulas (engine.c). */
static s32 shape_value(u32 shape,u32 x)
{
    s32 u=(s32)(x*32767u/19u),v;
    if(shape>=5 && shape<=8)u=(s32)((x*2u%20u)*32767u/19u);     /* loops: two cycles */
    switch(shape){
    case 0:v=32767-u;v=(v*v)>>15;return (v*v)>>15;
    case 1:case 5:return 32767-u;
    case 2:case 6:return u<16384?u*2:(32767-u)*2;
    case 3:case 8:return u;
    case 4:v=(u*u)>>15;return (v*v)>>15;
    case 7:return (x*2u%20u)<10u?32767:0;
    case 9:{static const s32 r[5]={12000,30000,4000,22000,16000};s32 a=r[x/5u%5u],b=r[(x/5u+1u)%5u];return a+(b-a)*(s32)(x%5u)/5;}
    case 10:{static const s32 r[4]={24000,6000,30000,14000};return r[x/5u%4u];}
    default:{static const s32 r[10]={16384,26000,32000,28000,18000,6000,1000,4000,10000,16384};return r[x/2u%10u];}
    }
}
static void shape(void *ctx,u32 n,s32 x0,s32 y0)
{
    u32 x;s32 last=0,y;
    for(x=0;x<20u;x++){
        y=y0+shape_value(n,x)*5/32767;
        span(ctx,x0+(s32)x,x?last:y,y);
        last=y;
    }
}
/* A bipolar bar (-64..+63 around 0x4000): the scale, a centre tick, the value. */
static void bipolar(void *ctx,s32 x0,u32 raw)
{
    s32 mid=x0+CW/2,v=((s32)raw-0x4000)*10/0x4000;
    dot_line(ctx,x0,x0+CW-1,9);dot(ctx,mid,10);dot(ctx,mid,8);
    if(v>0)FILL(ctx,mid,8,mid+v,10,1);else if(v<0)FILL(ctx,mid+v,8,mid,10,1);
}
void wt_draw(const struct st_machine_ui_api *api,const struct core_machine_def *m,void *view,void *ctx,u32 page)
{
    u32 raw[8],i,slot,p;u8 locked[8];char text[8];s32 x,v;
    if(m->type!=72 || page>1)return;
    if(slot_list_draw(view,ctx,72,page))return;     /* SLOT turning: the pool list */
    for(i=0;i<8;i++)raw[i]=get(view,8u*page+i,locked+i);
    slot=raw[3]>>8;if(slot>127u)slot=127u;
    load(api,slot);
    canvas(ctx);
    FILL(ctx,19,0,124,51,0);
    /* Top row. TUNE/DTUN: its value between dots, in a box. */
    x=CX(0);FILL(ctx,x+1,43,x+CW-1,43,1);FILL(ctx,x+1,51,x+CW-1,51,1);
    FILL(ctx,x,44,x,50,1);FILL(ctx,x+CW,44,x+CW,50,1);
    signed_text(text,((s32)raw[0]-0x4000)>>8);       /* semitones, or DTUN's steps */
    dot_line(ctx,x+2,x+4,46);dot_line(ctx,x+CW-4,x+CW-2,46);   /* clear of a 3-character value */
    TEXT(ctx,SMALL,x+CW/2,45,2,"%s",text);
    /* LEV: the label over a bar as long as the level. */
    x=CX(1);TEXT(ctx,SMALL,x+CW/2,46,2,"%s","LEV");
    FILL(ctx,x,43,x+(s32)((raw[1]>>8)*(u32)CW/127u),44,1);
    /* [POS]: the label in brackets over a dotted rule. */
    x=CX(2);TEXT(ctx,SMALL,x+CW/2,46,2,"%s","POS");
    FILL(ctx,x,44,x,51,1);FILL(ctx,x+CW,44,x+CW,51,1);dot_line(ctx,x+2,x+CW-2,43);
    /* W + the SLOT's pool position, inverted. */
    x=CX(3);TEXT(ctx,SMALL,x+3,45,0,"%s","W");
    number(text,slot+1u);
    TEXT(ctx,SMALL,x+(9+CW+2)/2,45,2,"%s",text);FILL(ctx,x+9,43,x+CW+2,51,XOR);
    /* The wave, and its position across the table. */
    if(view_waves){
        p=position(api,view,page,slot,raw[2]);
        wave(ctx,p);
        dot_line(ctx,WAVE_X,WAVE_X+WAVE_W-1,TRACK_Y);
        x=WAVE_X+(s32)(view_waves>1u?p*(u32)(WAVE_W-3)/((view_waves-1u)<<8):0u);
        FILL(ctx,x,TRACK_Y-1,x+2,TRACK_Y+1,1);
    }else{
        u32 frames=api->version>=9 && api->sample_read?api->sample_read(slot,0,0,0):0;
        TEXT(ctx,SMALL,WAVE_X+WAVE_W/2,26,2,"%s",!frames?"EMPTY SLOT":frames<2048u?"SAMPLE TOO SHORT":"NO WAVES");
    }
    /* Bottom row: SPD's scale and tick, the two depths, ANIM's shape. */
    x=CX(0);dot_line(ctx,x,x+CW-1,9);
    v=(s32)((raw[4]>>8)*(u32)(CW-1)/127u);FILL(ctx,x+v,8,x+v,11,1);
    bipolar(ctx,CX(1),raw[5]);bipolar(ctx,CX(2),raw[6]);
    shape(ctx,(raw[7]>>8)>11u?11u:raw[7]>>8,CX(3)+1,7);
    {static const char *const labels[2][4]={{"SPD","A.LEV","A.POS","ANIM"},{"SPD","A.LEV","A.POS","ANIM"}};
     for(i=0;i<4;i++)TEXT(ctx,SMALL,CX(i)+CW/2,1,2,"%s",labels[page][i]);}
    /* p-locks: the control's cell inverted. */
    for(i=0;i<8;i++){
        if(!locked[i])continue;
        x=CX(i&3u);
        if(i<4)FILL(ctx,x-1,42,x+CW+2,51,XOR);else FILL(ctx,x-1,0,x+CW+1,12,XOR);
    }
    PAGE_INDICATOR(view,ctx,2,page,7);
}
/* Redraw when the table arrives or the moving position changes. */
int wt_idle(const struct st_machine_ui_api *api,const struct core_machine_def *m,void *view,u32 page)
{
    u32 now[8],slot,i;int changed=0;const struct st_machine_cursor *c;
    if(m->type!=72 || page>1)return 0;
    if(slot_list_active(view))return slot_list_idle(view,72,page);
    if(slot_list_idle(view,72,page))changed=1;     /* the list just closed */
    slot=get(view,8u*page+3u,0)>>8;if(slot>127u)slot=127u;
    api->wave_idle(slot,0);                    /* AUDIO's cursor records, no card read */
    load(api,slot);
    c=api->cursor(TRACK(view)+8u*page,slot);
    now[0]=(u32)view;now[1]=page;now[2]=slot;now[3]=view_waves;
    now[4]=c && c->active?c->position>>6:0xffffffffu;             /* a 32nd of a wave */
    now[5]=c?c->active:0;now[6]=view_tries;now[7]=0;
    idle_calls++;
    for(i=0;i<8;i++)if(i!=4 && shown[i]!=now[i])changed=1;
    if(shown[4]!=now[4]){
        if(!changed && idle_calls-moved_at<WT_MOVING_CALLS)return 0;   /* keep it for later */
        moved_at=idle_calls;changed=1;
    }
    for(i=0;i<8;i++)shown[i]=now[i];
    return changed;
}
