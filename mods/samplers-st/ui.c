/* SPDX-License-Identifier: GPL-2.0-or-later
 * Sampler-owned UI, ported from Synfact syn_controls.c and pool/syn_wave.c.
 * Lives in the SMZ. Native immutable bitmaps/caches remain firmware-owned.
 */
#include <syntakt/machines.h>
#include "browser.h"
#include "allocate.h"
typedef st_u32 u32;typedef unsigned char u8;
#ifndef ST_UI_PRIMITIVES
#define FILL ((void (*)(void *,int,int,int,int,u32))0x400f7df2u)
#define FRAME ((void (*)(void *,int,int,int,int,u32))0x400f7bd6u)
#define PIXEL ((void (*)(void *,int,int,u32))0x400f7140u)
#define TEXT ((void (*)(void *,void *,int,int,u32,const char *,...))0x400f8a0cu)
#define FONT ((void *)0x443da210u)
#endif
#ifndef ST_UI_NATIVE_DIAL
#define ST_UI_NATIVE_DIAL ((void (*)(void *,u32,u32,u32,u32,void *,int,int))0x40011e74u)
#define ST_UI_BITMAP ((void *(*)(void *,u32,u32))0x400f9258u)
#define ST_UI_BLIT ((void (*)(void *,void *,int,int,u32))0x400f8dacu)
#endif
enum {INTEGER,DECIMAL,PLAY,SAMPLE,SEG,MODE,BARS,SLICE,LENGTH,GRID,TUNE};
static char text[24];
static const u32 layout[] __attribute__((aligned(4)))={0,1,2,3,4,5,6,7,0,1,2,3,4,5,6,7};
/* REPITCH is tempo/window driven. Keep parameter 0 in the stored ABI, but
 * lend no control ID to encoder A or native page actions on either page. */
static const u32 repitch_layout[] __attribute__((aligned(4)))={
    0xffffffffu,1,2,3,4,5,6,7,0xffffffffu,1,2,3,4,5,6,7};
static u32 kind(const struct core_machine_def *m,u32 index)
{
    static const u8 kinds[4][8]={
        {TUNE,PLAY,INTEGER,SAMPLE,DECIMAL,DECIMAL,DECIMAL,INTEGER},
        {TUNE,PLAY,INTEGER,SAMPLE,SEG,MODE,BARS,INTEGER},
        {TUNE,PLAY,INTEGER,SAMPLE,DECIMAL,DECIMAL,BARS,INTEGER},
        {TUNE,PLAY,INTEGER,SAMPLE,SLICE,LENGTH,GRID,INTEGER}};
    return m->type>=66 && m->type<=69 && index<8?kinds[m->type-66][index]:INTEGER;
}
static char *number(char *p,u32 n)
{if(n>=100)*p++='0'+n/100;if(n>=10)*p++='0'+n/10%10;*p++='0'+n%10;*p=0;return p;}
static const char *format(const struct st_machine_ui_api *api,const struct core_machine_def *m,u32 index,u32 raw)
{
    u32 k=kind(m,index),n=raw>>8,f;char *p=text;const char *s=0;
    if(k==INTEGER)return 0;
    if(k==SAMPLE){
        if(api->version>=2 && api->format_context==ST_UI_FORMAT_CONTROL)return m->parameter[index].label;
        if(!api->sample_name(n<128?n:127,text)){
            s="NO SAMPLE";while(*s)*p++=*s++;*p=0;}return text;}
    if(k==PLAY || k==MODE){static const char *names[]={"REVERSE","REVERSE LOOP","FORWARD LOOP","FORWARD"};s=names[n<4?n:3];}
    else if(k==SEG){static const char *names[]={"1/32","1/16","1/8"};s=names[n<3?n:2];}
    else if(k==SLICE && !n)s="NOTE";
    if(s){while(*s)*p++=*s++;*p=0;return text;}
    if(k==LENGTH)n++;
    if(k==BARS)n=1u<<(n<4?n:3);
    if(k==GRID)n=4u<<(n<5?n:4);
    if(k==TUNE){int v=(int)raw-0x4000;*p++=v<0?'-':'+';raw=v<0?(u32)-v:(u32)v;n=raw>>8;}
    p=number(p,n);
    if(k==DECIMAL || k==TUNE){f=((raw&255u)*100u)>>8;*p++='.';*p++='0'+f/10;*p++='0'+f%10;*p=0;}
    return text;
}
static int dial(const struct st_machine_ui_api *api,const struct core_machine_def *m,u32 index,const struct st_machine_dial *d)
{
    u32 k=kind(m,index),raw=d->raw,n=raw>>8,i,x,y,ix,on,dx,dy;void *bmp;char value[8];
    if(k==INTEGER)return 0;
    if(k==DECIMAL || k==TUNE || k==LENGTH){
        const struct core_machine_param *p=m->parameter+index;u32 lo=p->min,hi=p->max,v=raw<lo?lo:raw>hi?hi:raw;
        u32 normalized=k==TUNE?(v<=0x4000u?(v-lo)*0x4000u/(0x4000u-lo):
            0x4000u+(v-0x4000u)*0x3f00u/(hi-0x4000u)):(v-lo)*0x7f00u/(hi-lo);
        ST_UI_NATIVE_DIAL(d->model,0x13,normalized,d->a,d->b,d->ctx,d->x,d->y);return 1;
    }
    if(k==PLAY || k==MODE){
        bmp=ST_UI_BITMAP((void *)0x444e0cc8u,n<4?n:3,0);
        ST_UI_BLIT(d->ctx,bmp,d->x,d->y,0);return 1;
    }
    FILL(d->ctx,d->x,d->y,d->x+18,d->y+16,0);
    if(k==SEG || k==GRID){
        /* Matching 17x7 SEG and 17x17 GRID glyphs expressed as drawing rules,
         * with no donor resource or binary input. Clear once, draw set pixels. */
        ix=n>(k==SEG?2u:4u)?(k==SEG?2u:4u):n;
        dx=ix?ix<3?4:2:8;dy=ix<2?8:ix<4?4:2;
        for(x=0;x<17;x++)for(y=0;y<(k==SEG?7u:17u);y++){
            if(k==GRID)on=(x%dx)!=0 && (y%dy)!=0;
            else if(y==1 || y==5)on=x>=2 && x<=14 &&
                !(ix<=1 && x>=5 && x<=6) && !(ix>=1 && x>=10 && x<=11);
            else on=y>=2 && y<=4 && (x==1 || x==15 || (x>=1+5*ix && x<=5+5*ix) ||
                (y==3 && (x==3 || x==8 || x==13)));
            if(on)PIXEL(d->ctx,d->x+1+x,d->y+(k==SEG?9:0)+y,1);
        }
        if(k==SEG){number(value,32u>>(n<3?n:2));TEXT(d->ctx,FONT,d->x+9,d->y+2,2,"%s",value);}
        return 1;
    }
    if(k==SAMPLE){number(value,(n<128?n:127)+1);}
    else {const char *s=format(api,m,index,raw);i=0;while(s && s[i] && i<7){value[i]=s[i];i++;}value[i]=0;}
    TEXT(d->ctx,FONT,d->x+9,d->y+6,0x12,"%s",value);
    FRAME(d->ctx,d->x+1,d->y,d->x+17,d->y+16,1);return 1;
}
/* Native TUNE's encoder policy keeps Q8 fractions. The actual range/default
 * still come from this machine's descriptor. All discrete controls stay coarse. */
static u32 one_policy(u32 i){return i==0 || (i>=4 && i<=6)?0x1f1:0xffffffffu;}
static u32 repitch_policy(u32 i){return i==4 || i==5?0x1f1:0xffffffffu;}
static u32 coarse_policy(u32 i){return i==0?0x1f1:0xffffffffu;}
static u32 value(void *view,u32 id)
{u8 locked=0;return ((u32 (*)(void *,u32,u8 *))0x40039160u)(view,id,&locked)&65535u;}
static void marker(void *ctx,u32 n,u32 denominator,const char *label,int dotted)
{
    int x=22+(int)((n>denominator?denominator:n)*105u/denominator),y;
    if(!dotted)FILL(ctx,x,14,x,39,1);else for(y=15;y<=39;y+=2)PIXEL(ctx,x,y,1);
    TEXT(ctx,FONT,x>124?124:x,9,2,"%s",label);
}
static u32 selected(const struct core_machine_def *m,void *view)
{u32 n=value(view,0x1f9+(m->type-64)*32+3)>>8;return n<128?n:127;}
static int idle(const struct st_machine_ui_api *api,const struct core_machine_def *m,void *view,u32 page)
{
    static u32 before[8];u32 now[8],slot,track,i;
    int changed=sp_browser_idle(api,m,view,page)|sp_allocate_idle(view);
    const struct st_machine_wave *w;const struct st_machine_cursor *c;
    if(page!=1 || sp_browser_active(view) || sp_allocate_active(view))return changed;
    slot=selected(m,view);api->wave_idle(slot,2048);
    w=api->wave(slot);track=((u32 (*)(void *))0x4001ff74u)(*(void **)((u8 *)view+0x74));c=api->cursor(track,slot);
    now[0]=(u32)view;now[1]=m->type;now[2]=slot;now[3]=w?w->frames:0;now[4]=w?w->ready:0;
    now[5]=c?c->pointer:0;now[6]=c?c->position:0;now[7]=c?c->active:0;
    for(i=0;i<8;i++){if(before[i]!=now[i])changed=1;before[i]=now[i];}
    return changed; /* Stationary waveform needs no extra paint. */
}
/* One channel's 96 bins over x 22..127, centred on y, +-half pixels. */
static void wave_band(void *ctx,const short *lo,const short *hi,int y,int half)
{
    u32 i,bin;int low,high;
    for(i=0;i<106;i++){
        bin=i*95u/105;low=y-((int)hi[bin]*half/32768);high=y-((int)lo[bin]*half/32768);
        if(lo[bin]>hi[bin])low=high=y;FILL(ctx,22+i,low,22+i,high,1);
    }
}
static void wave_draw(const struct st_machine_ui_api *api,const struct core_machine_def *m,void *view,void *ctx,u32 page)
{
    const struct st_machine_wave *w;const struct st_machine_cursor *cursor;
    u32 raw[8],i,k=m->type-66,slot,grid,s,e,l,track;int x,y;
    u32 e0=0x1f9+(m->type-64)*32;
    if(page!=1 || k>3)return;
    for(i=0;i<8;i++)raw[i]=value(view,e0+i);slot=raw[3]>>8;if(slot>=128)slot=127;w=api->wave(slot);
    FILL(ctx,22,0,127,50,0);
    for(i=0;i<4;i++){
        if(k!=2 || i)TEXT(ctx,FONT,23+26*i,44,0,"%s",m->parameter[i].label);
        TEXT(ctx,FONT,23+26*i,2,0,"%s",m->parameter[i+4].label);
    }
    if(!w || !w->ready){TEXT(ctx,FONT,74,27,2,"%s",w?"LOADING WAVE":"NO SAMPLE");return;}
    if(api->version>=8 && w->channels==2){
        /* UI API 8 (core 1.8): L above (y 28..38), R below (y 16..26), a gap between.
         * Native LCD y increases upwards. */
        wave_band(ctx,w->lo,w->hi,33,5);wave_band(ctx,w->lo_r,w->hi_r,21,5);
    }else wave_band(ctx,w->lo,w->hi,27,12);
    if(k==0 || k==2){
        s=raw[4];e=s+raw[5];if(e>0x7800)e=0x7800;l=k==0 && raw[6]?raw[6]-1:s;
        marker(ctx,s,0x7800,"S",0);marker(ctx,e,0x7800,"E",0);if(!k)marker(ctx,l,0x7800,"L",1);
    }else{
        if(k==3){grid=4u<<((raw[6]>>8)>4?4:raw[6]>>8);s=raw[4]>>8;s=s?s-1:0;
            e=s+(raw[5]>>8)+1;if(e>grid)e=grid;
            if(raw[4]>>8){marker(ctx,s,grid,"S",0);marker(ctx,e,grid,"E",0);}}
        else {u32 seg=raw[4]>>8,bars=raw[6]>>8;if(seg>2)seg=2;if(bars>3)bars=3;grid=(32u>>seg)<<bars;}
        for(i=0;i<=grid;i++){x=22+i*105u/grid;for(y=14;y<=39;y+=4)PIXEL(ctx,x,y,1);}
    }
    track=((u32 (*)(void *))0x4001ff74u)(*(void **)((u8 *)view+0x74));cursor=api->cursor(track,slot);
    if(cursor && cursor->active && cursor->frames==w->frames && cursor->frames){
        /* The core checks selected-slot PCM identity before lending a cursor. */
        x=22+(cursor->position>w->frames?w->frames:cursor->position)*105u/w->frames;FILL(ctx,x,12,x,41,1);
    }
}
static void draw(const struct st_machine_ui_api *api,const struct core_machine_def *m,void *view,void *ctx,u32 page)
{if(!sp_allocate_draw(view,ctx) && !sp_browser_draw(api,m,view,ctx,page))wave_draw(api,m,view,ctx,page);}
#include "browser.inc"
#include "allocate.inc"
#define UI(name,type,controls,policy_fn) const struct st_machine_ui name={type,2,controls,draw,idle,dial,format,policy_fn}
UI(sp_one_ui,66,layout,one_policy);UI(sp_werp_ui,67,layout,coarse_policy);
UI(sp_repitch_ui,68,repitch_layout,repitch_policy);UI(sp_slice_ui,69,layout,coarse_policy);
