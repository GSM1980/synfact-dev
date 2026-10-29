/* SPDX-License-Identifier: GPL-2.0-or-later
 * Production browser state/draw with a native-view and raster fixture.
 * Optional private font data is only for a local visual preview.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <syntakt/machines.h>
typedef st_u32 u32;
static unsigned char pixels[64][128],left_before[64][128],view[0x100];
static u32 type=66,track,raw,locked,trig,recording,base_raw,edits,clears,invalidations,reads,grid;
static u32 read_slots[7];static char rows[7][40];
static void pixel(void *ctx,int x,int y,u32 color)
{(void)ctx;if(x>=0 && x<128 && y>=0 && y<64){if((int)color<0)pixels[y][x]^=1;else pixels[y][x]=color!=0;}}
static void fill(void *ctx,int x,int y,int xx,int yy,u32 color)
{assert(xx>=x && yy>=y);for(int a=x;a<=xx;a++)for(int b=y;b<=yy;b++)pixel(ctx,a,b,color);}
static void frame(void *ctx,int x,int y,int xx,int yy,u32 color)
{fill(ctx,x,y,xx,y,color);fill(ctx,x,yy,xx,yy,color);fill(ctx,x,y,x,yy,color);fill(ctx,xx,y,xx,yy,color);}
#ifdef SP_BROWSER_REAL_FONT
#include "../mods/fm-tone-st/imported/ui-data.h"
static const unsigned char widths[128]=FM_FONT_WIDTHS;
static const unsigned char glyphs[128][5]=FM_FONT_GLYPHS;
#endif
static void string_draw(void *ctx,void *font,int x,int y,u32 limit,const char *s)
{
    int start=x;(void)font;
    if(x==53 && y<=44 && y>=2 && (44-y)%7==0){u32 row=(44-y)/7;snprintf(rows[row],40,"%s",s);}
    while(*s){unsigned char c=(unsigned char)*s++;int width;
#ifdef SP_BROWSER_REAL_FONT
        if(c>=128)c='?';width=widths[c];
#else
        width=c==' '?2:3;
#endif
        if(x+width>start+(int)limit)break;
        for(int i=0;i<width;i++)for(int j=0;j<5;j++){
#ifdef SP_BROWSER_REAL_FONT
            if(glyphs[c][i]&(1u<<(6-j)))pixel(ctx,x+i,y+j,1);
#else
            if(c!=' ' && (i==0 || j==0 || j==4))pixel(ctx,x+i,y+j,1);
#endif
        }
        x+=width+1;
    }
}
static void text_draw(void *ctx,void *font,int x,int y,u32 flags,const char *fmt,...)
{va_list a;char line[80];va_start(a,fmt);vsnprintf(line,80,fmt,a);va_end(a);(void)flags;string_draw(ctx,font,x,y,128-x,line);}
static void native_dial(void *model,u32 id,u32 value,u32 a,u32 b,void *ctx,int x,int y)
{(void)model;(void)id;(void)value;(void)a;(void)b;for(int i=-7;i<=7;i++)for(int j=-7;j<=7;j++)if(i*i+j*j>=39 && i*i+j*j<=55)pixel(ctx,x+8+i,y+8+j,1);}
static void *bitmap(void *v,u32 i,u32 a){(void)v;(void)i;(void)a;return 0;}
static void blit(void *c,void *b,int x,int y,u32 f){(void)c;(void)b;(void)x;(void)y;(void)f;}
static void *font_get(void *font){return font;}
static u32 type_get(void *v){assert(v==view);return type;}
static u32 track_get(void *v){assert(v==(void *)0x1234);return track;}
static u32 value_get(void *v,u32 id,unsigned char *out)
{assert(v==view);*out=(unsigned char)locked;if(type==69 && id==0x29f)return grid<<8;
 assert(id==0x1f9u+(type-64)*32+3);return raw;}
static u32 trig_get(void *v){assert(v==(void *)0x5678);return trig;}
static u32 record_get(void *v){assert(v==(void *)0x5678);return recording;}
static void invalidate(void *v){assert(v==view);invalidations++;}
static void edit(void *v,u32 id,int delta,u32 knob)
{assert(v==view && id==0x1f9u+(type-64)*32+3 && knob==3);raw+=delta;edits++;}
static void clear(void *v,u32 id,u32 whole_page,u32 knob)
{assert(v==view && id==0x1f9u+(type-64)*32+3 && !whole_page && knob==3);raw=base_raw;locked=0;clears++;}
static int sample_name(u32 slot,char *out)
{
    static const char *const reference[]={"HH2 Byggin","RS Byggin","SD1 Byggin","PC1 Spring","BD1 Weird","BD2 Weird","CP Weird"};
    if(reads<7)read_slots[reads]=slot;reads++;
    if(slot==64)return 0;
    if(slot>=32 && slot<=38)snprintf(out,33,"%s",reference[slot-32]);
    else if(slot==127)snprintf(out,33,"ABCDEFGHIJKLMNOPQRSTUVWXYZ123456");
    else snprintf(out,33,"SAMPLE %u",slot+1);
    return 1;
}
#define ST_UI_PRIMITIVES
#define FILL fill
#define FRAME frame
#define PIXEL pixel
#define TEXT text_draw
#define FONT ((void *)0)
#define ST_UI_NATIVE_DIAL native_dial
#define ST_UI_BITMAP bitmap
#define ST_UI_BLIT blit
#define SP_BROWSER_NATIVE
#define SP_BROWSER_TYPE type_get
#define SP_BROWSER_TRACK track_get
#define SP_BROWSER_GET value_get
#define SP_BROWSER_TRIG trig_get
#define SP_BROWSER_RECORD record_get
#define SP_BROWSER_INVALIDATE invalidate
#define SP_BROWSER_FONT font_get
#define SP_BROWSER_TEXT string_draw
#include "../mods/samplers-st/main.c"
#include "../mods/samplers-st/ui.c"
static int event_delta(void *event,u32 normal,u32 pressed)
{(void)pressed;return (int)((const u32 *)event)[4]*(int)normal;}
static u32 event_pressed(void *event){return ((const u32 *)event)[5];}
static u32 held(u32 code){(void)code;return 0;}
static u32 unused_policy(u32 id){(void)id;assert(0);return 0;}
static int unused_increment(void *v,u32 k,int d,void *p,u32 held)
{(void)v;(void)k;(void)d;(void)p;(void)held;assert(0);return 0;}
static u32 unused_snap_active;
static void unused_arm(void *v,u32 k){(void)v;(void)k;assert(0);}
static u32 unused_blocked(void *v,u32 k){(void)v;(void)k;assert(0);return 0;}
static int unused_lookup(int n,int d,const void *p){(void)n;(void)d;(void)p;assert(0);return 0;}
#define SP_INPUT_NATIVE
#define SP_INPUT_TYPE type_get
#define SP_INPUT_TRACK track_get
#define SP_INPUT_GET value_get
#define SP_INPUT_DELTA event_delta
#define SP_INPUT_PRESSED event_pressed
#define SP_INPUT_TRIG trig_get
#define SP_INPUT_HELD held
#define SP_INPUT_POLICY unused_policy
#define SP_INPUT_INCREMENT unused_increment
#define SP_INPUT_SNAP_ARM unused_arm
#define SP_INPUT_SNAP_BLOCKED unused_blocked
#define SP_INPUT_SNAP_ACTIVE unused_snap_active
#define SP_INPUT_SNAP_LOOKUP unused_lookup
#include "../mods/samplers-st/input.c"
static struct st_machine_ui_api api;
static u32 key[5];
static void begin(u32 machine,u32 page,u32 original)
{
    sp_browser_reset();type=machine;raw=base_raw=original;locked=trig=recording=0;track=0;
    *(u32 *)view=0x401ca0dc;*(u32 *)(view+0x88)=0x14;*(u32 *)(view+0x90)=page;
    *(void **)(view+0x74)=(void *)0x1234;*(void **)(view+0x6c)=(void *)0x5678;
    edits=clears=reads=invalidations=0;sp_browser_begin(&api,view,type,page);
    assert(sp_browser_active(view) && invalidations==1);
}
static int press(u32 code,u32 flags)
{key[3]=code;key[4]=flags;return sp_browser_pre_key(view,key,1);}
static void background(void)
{
    memset(pixels,0,sizeof(pixels));
    string_draw(0,0,1,56,16,"A01");fill(0,0,54,16,63,0xffffffffu);
    fill(0,0,28,17,50,1);fill(0,1,29,16,49,0);string_draw(0,0,1,43,16,"SMP");string_draw(0,0,7,31,8,"1");
    native_dial(0,0,0,0,0,0,23,32);native_dial(0,0,0,0,0,0,23,6);
    string_draw(0,0,22,26,29,"TUNE");string_draw(0,0,22,0,29,"STRT");
}
int main(int argc,char **argv)
{
    const struct core_machine_def *machines[]={&sp_one_shot,&sp_werp,&sp_repitch,&sp_slice};
    const struct st_machine_ui *uis[]={&sp_one_ui,&sp_werp_ui,&sp_repitch_ui,&sp_slice_ui};
    u32 i,p,s,first,selected;
    api.version=7;api.sample_name=sample_name;api.edit=edit;api.clear=clear;
    for(i=0;i<4;i++)for(p=0;p<2;p++)for(s=0;s<128;s++){
        begin(66+i,p,35<<8);raw=s<<8;background();memcpy(left_before,pixels,sizeof(pixels));
        reads=0;memset(rows,0,sizeof(rows));uis[i]->draw(&api,machines[i],view,pixels,p);
        first=s>3?s-3:0;if(first>121)first=121;selected=s-first;
        assert(reads==7);for(u32 n=0;n<7;n++)assert(read_slots[n]==first+n);
        for(int x=0;x<51;x++)for(int y=0;y<54;y++)assert(pixels[y][x]==left_before[y][x]);
        for(u32 n=0;n<7;n++)assert(pixels[44-7*n][51]==(n==selected));
        assert(!sp_browser_idle(&api,machines[i],view,p));
    }
    begin(66,0,35<<8);raw=64<<8;reads=0;sp_browser_draw(&api,&sp_one_shot,view,pixels,0);
    assert(!strcmp(rows[3],"65:NO SAMPLE"));
    raw=127<<8;reads=0;sp_browser_draw(&api,&sp_one_shot,view,pixels,0);
    assert(!strncmp(rows[6],"128:ABCDEFGHIJKLMNOPQRSTUVWXYZ",30));
    /* Repeated SAMP events preserve the first value for a single NO revert. */
    begin(66,0,35<<8);raw=36<<8;sp_browser_begin(&api,view,66,0);raw=39<<8;
    assert(press(13,3) && raw==35<<8 && edits==1 && !sp_browser_active(view));
    assert(!press(14,0));assert(press(13,1)); /* another key's up cannot release our NO */
    assert(press(13,0) && edits==1);assert(!press(13,0));
    begin(66,0,35<<8);raw=39<<8;assert(press(12,3) && raw==39<<8 && !edits);assert(press(12,0));
    /* Existing and newly created locks use native edit/clear respectively. */
    begin(67,1,35<<8);trig=1;locked=1;sp_browser_begin(&api,view,67,1);raw=40<<8;
    assert(press(13,3) && raw==35<<8 && edits==1 && locked && !clears);
    begin(67,1,35<<8);trig=1;sp_browser_begin(&api,view,67,1);raw=40<<8;locked=1;
    assert(press(13,3) && raw==35<<8 && !locked && clears==1 && !edits);
    for(i=0;i<4;i++){
        begin(66,0,35<<8);raw=40<<8;
        if(i==0)track=1;else if(i==1)*(u32 *)(view+0x90)=1;else if(i==2)type=67;else trig=1;
        assert(!press(13,3) && raw==40<<8 && !edits && !clears && !sp_browser_active(view));
    }
    begin(66,0,35<<8);raw=40<<8;assert(!press(13,11));sp_browser_post_key(view,key,0);assert(!sp_browser_active(view) && !edits);
    begin(66,0,35<<8);raw=40<<8;sp_browser_post_key(view,key,0);assert(!sp_browser_active(view) && raw==40<<8);
    begin(66,0,35<<8);recording=1;sp_browser_begin(&api,view,66,0);assert(!sp_browser_active(view));recording=0;
    begin(66,0,35<<8);sp_browser_draw(&api,&sp_one_shot,view,pixels,0);
    for(i=1;i<SP_BROWSER_TICKS;i++)assert(!sp_browser_idle(&api,&sp_one_shot,view,0));
    assert(sp_browser_idle(&api,&sp_one_shot,view,0) && !sp_browser_active(view));
    /* Real file-owned input callbacks open, delegate the native edit, and
     * consume NO at the pre-key phase. Other controls dismiss the list. */
    {
        struct st_smz_api services={3,&api};u32 ev[6]={0,0,0,4,1,0};
        begin(66,1,35<<8);assert(!sp_input_init(&services) && !sp_browser_active(view));
        assert(!sp_input_encoder(view,ev,0x23c) && sp_browser_active(view) && raw==35<<8);
        edit(view,0x23c,256,3);reads=0;sp_one_ui.draw(&api,&sp_one_shot,view,pixels,1);
        assert(reads==7 && !strcmp(rows[3],"37:BD1 Weird"));
        assert(!sp_one_ui.idle(&api,&sp_one_shot,view,1)); /* no waveform work under the list */
        ev[3]=13;ev[4]=3;assert(sp_input_key_pre(view,ev,1) && raw==35<<8);
        ev[4]=0;assert(sp_input_key_pre(view,ev,1));
        ev[3]=4;ev[4]=1;assert(!sp_input_encoder(view,ev,0x23c) && sp_browser_active(view));
        assert(!sp_input_encoder(view,ev,0x23b) && !sp_browser_active(view));
    }
    if(argc>1){
        FILE *out;begin(66,0,35<<8);background();reads=0;sp_one_ui.draw(&api,&sp_one_shot,view,pixels,0);
        assert(reads==7 && sp_browser_active(view));
        out=fopen(argv[1],"wb");assert(out);fprintf(out,"P5\n128 64\n255\n");
        for(int y=63;y>=0;y--)for(int x=0;x<128;x++)fputc(pixels[y][x]?255:0,out);fclose(out);
    }
    puts("PASS: sample browser on four machines/two pages/all 128 positions, seven cached rows, clipping/selection/left rail, NO/YES edges, lock rollback, context dismissal and timeout");return 0;
}
