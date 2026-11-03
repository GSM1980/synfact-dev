/* SPDX-License-Identifier: GPL-2.0-or-later
 * Telemetry port from Synfact pool/system_info.c, inspired by irpina/digihealth.
 * Native UI stays owned by the firmware. No machines, project data or storage.
 *
 * The SETTINGS row opens a list (screen): METERS ON/OFF, then the core and each
 * mod in the bank with its version (host API 12). ENC A and the arrows move,
 * YES on METERS toggles the meters, NO closes the list.
 */
typedef unsigned int u32;
typedef unsigned char u8;
#define UNKNOWN 0xffffffffu
#define WIN ((volatile u32 *)0x10000800u)
#define WIN16 ((volatile unsigned short *)0x10000800u)
#define QUERY_MAGIC 0x48543133u
#define REPLY_MAGIC 0x53493134u
#define LIST_LINES 10
#define KEY_YES 12
#define KEY_NO 13
#define KEY_UP 14
#define KEY_DOWN 15

#ifndef ST_SYSTEM_INFO_MODULE
extern void core_clock_snapshot(u32 *);
extern void core_additem(void *,const void *);
extern void fw_invalidate(void *);
extern void fw_fillrect(void *,int,int,int,int,u32);
extern void fw_bitmap_ctor(void *,u32,u32,const void *,const void *);
extern void fw_bitmap_dtor(void *);
extern void fw_blit(void *,const void *,int,int,u32);
extern void si_label(void);
extern const u32 core_pool_busy[];
extern u32 core_pool_query_owner[];
extern const unsigned char core_mod_info[];
extern const unsigned char *core_bank_info;
#define SI_CORE_INFO core_mod_info
#define SI_BANK_INFO core_bank_info
#endif
#include "ram.c"
static u32 enabled,ticks,page_ticks,page,primed,header_dirty;
static u32 previous[3],audio_previous[3],audio_primed;
static u32 audio[5],audio_age,seq,pending;
static u32 values[4]={UNKNOWN,UNKNOWN,UNKNOWN,UNKNOWN};
static char line[25]="CPU M --% CPU A --%";
static u32 list_open,cursor,top,list_dirty,swallowed;

const char *si_label_text(void) {return enabled?"SYSTEM INFO: ON":"SYSTEM INFO: OFF";}
struct capture {void *data;u32 unused,manager,invoke;};
static void set_enabled(u32 on)
{
    u32 i;
    enabled=on;ticks=page_ticks=page=primed=audio_primed=0;
    header_dirty=1;
    audio_age=0;pending=0;
    for(i=0;i<4;i++)values[i]=UNKNOWN;
    /* Stop publication when OFF; leave the native timer mode untouched. */
    if(core_pool_query_owner[0]==2){WIN[0]=0;core_pool_query_owner[0]=0;}
}
static void si_select(const struct capture *fn)
{
    list_open=1;cursor=0;top=0;list_dirty=1;
    if(fn->data)fw_invalidate((u8 *)fn->data+0x38);
}
/* Hand-rasterized Synfact info icon (Material Symbols info, Apache-2.0).
 * ROM columns are reversed vertically by the native Bitmap convention. */
static const u32 icon[9]={0x3e000000,0x41000000,0x80800000,0x80800000,
                         0xba800000,0x80800000,0x80800000,0x41000000,0x3e000000};
static const u32 mask[9]={0x3e000000,0x7f000000,0xff800000,0xff800000,
                         0xff800000,0xff800000,0xff800000,0x7f000000,0x3e000000};
static void si_icon(const struct capture *fn,void *item,void *bmp,int x,int y)
{
    u32 b[8];(void)fn;(void)item;
    fw_bitmap_ctor(b,9,9,icon,mask);fw_blit(bmp,b,x,y,0);fw_bitmap_dtor(b);
}
static void si_change(const struct capture *fn,int amount) {(void)fn;(void)amount;}
void si_settings(void *menu)
{
    static const void *row[]={si_label,si_select,si_icon,si_change};
    core_additem(menu,row);
}
/* The dedicated AUDIO companion accepts only this read-only CMD4 query.
 * Single outstanding request, a new sequence and cleared extension per query.
 * Sequence is committed last by both CPUs. No waiting in the UI thread. */
static void query_audio(void)
{
    if(core_pool_busy[0])return;
    if(core_pool_query_owner[0]==1)return;
    if(pending && core_pool_query_owner[0]!=2)pending=0;
    u32 i,copy[5],generation;
    if(pending) {
        generation=WIN[41];
        if(WIN[40]==REPLY_MAGIC && generation==pending && WIN[6]==(pending<<16|1u)) {
            for(i=0;i<5;i++)copy[i]=WIN[42+i];
            __asm__ volatile("":::"memory");
            if(WIN[41]==generation && WIN[40]==REPLY_MAGIC) {
                for(i=0;i<5;i++)audio[i]=copy[i];
                audio_age=0;pending=0;core_pool_query_owner[0]=0;return;
            }
        }
        if(pending) {
            if(++audio_age<90)return;
            for(i=0;i<5;i++)audio[i]=0;
            /* A new sequence also recovers an invalid or timed-out reply. */
            pending=0;audio_primed=0;
            core_pool_query_owner[0]=0;
        }
    }
    if(core_pool_query_owner[0])return;
    core_pool_query_owner[0]=2;
    seq=(seq+1u)&65535u;if(!seq)seq=1;
    WIN[0]=0;WIN[40]=0;WIN[41]=0;WIN[6]=0;
    WIN[2]=0;WIN[3]=0;WIN[4]=QUERY_MAGIC+(seq<<16|4u);
    WIN16[3]=4;
    __asm__ volatile("":::"memory");
    WIN[0]=QUERY_MAGIC;
    WIN16[2]=(unsigned short)seq;
    pending=seq;
}
static u32 percent(u32 busy,u32 elapsed)
{
    u32 divisor=elapsed/1000u,n;
    if(!divisor || busy>elapsed)return UNKNOWN;
    n=busy/divisor;if(n>1000u)n=1000u;
    return (n+5u)/10u;
}
static void update(void)
{
    u32 now[3],elapsed,idle,render,i;
    struct si_ram_result ram;
    core_clock_snapshot(now);
    elapsed=now[0]-previous[0];idle=now[1]-previous[1];render=now[2]-previous[2];
    if(primed) {
        idle=idle>render?idle-render:0;
        values[0]=idle<=elapsed?percent(elapsed-idle,elapsed):UNKNOWN;
    }
    for(i=0;i<3;i++)previous[i]=now[i];
    primed=1;values[1]=values[3]=UNKNOWN;
    if(audio[4] && audio_age<90 && audio[3]!=audio_previous[2]) {
        if(audio_primed)values[1]=percent(audio[0]-audio_previous[0],audio[1]-audio_previous[1]);
        audio_previous[0]=audio[0];audio_previous[1]=audio[1];audio_previous[2]=audio[3];
        audio_primed=1;
        si_ram_audio(audio[2],&ram);values[3]=si_ram_percent(&ram);
    } else audio_primed=0;
    si_ram_main(&ram);values[2]=si_ram_percent(&ram);
}
static char *number(char *p,u32 n)
{
    if(n>100u){*p++='-';*p++='-';return p;}
    if(n==100u){*p++='1';*p++='0';*p++='0';return p;}
    if(n>=10u)*p++='0'+n/10u;
    *p++='0'+n%10u;return p;
}
static char *word(char *p,const char *s){while(*s)*p++=*s++;return p;}
static int format(void)
{
    char next[sizeof(line)],*p=next;u32 i;
    p=word(p,page?"RAM M ":"CPU M ");p=number(p,values[2*page]);
    p=word(p,page?"% RAM A ":"% CPU A ");p=number(p,values[2*page+1]);
    *p++='%';*p=0;
    for(i=0;next[i]==line[i];i++)if(!next[i])return 0;
    for(i=0;(line[i]=next[i])!=0;i++){}
    return 1;
}
/* The mod list: a zero-terminated list of 40-byte records (id, version), the
 * core's first. Lists are at most 32 records each. */
static u32 list_count(const u8 *p)
{
    u32 n=0;
    if(p)for(;p[0] && n<32;p+=40)n++;
    return n;
}
static u32 rows(void){return 1+list_count(SI_CORE_INFO)+list_count(SI_BANK_INFO);}
static const u8 *list_entry(u32 index)
{
    const u8 *core=SI_CORE_INFO,*bank=SI_BANK_INFO;u32 n=list_count(core);
    if(index<n)return core+40*index;
    index-=n;
    if(index<list_count(bank))return bank+40*index;
    return 0;
}
static char upper(char c){return c>='a' && c<='z'?(char)(c-32):c;}
/* Row 0 is the METERS switch; the rest are "ID VERSION". */
static void row_text(u32 row,char *out)
{
    char *p=out;const u8 *e;u32 i;
    if(!row){p=word(p,"METERS ");p=word(p,enabled?"ON":"OFF");*p=0;return;}
    e=list_entry(row-1);
    if(!e){*out=0;return;}
    for(i=0;i<24 && e[i];i++)*p++=upper((char)e[i]);
    *p++=' ';
    for(i=0;i<16 && e[24+i];i++)*p++=upper((char)e[24+i]);
    *p=0;
}
static void move(int delta)
{
    u32 n=rows();
    if(delta<0){if(cursor)cursor--;}
    else if(delta>0 && cursor+1<n)cursor++;
    if(cursor<top)top=cursor;
    if(cursor>=top+LIST_LINES)top=cursor-LIST_LINES+1;
    list_dirty=1;
}
static u32 be(const u8 *p){return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
/* ev_key: while the list is open it takes every key, and NO's release too. */
int si_key(void *brain,void *ev)
{
    const u8 *e=ev;u32 key=be(e+12),flags=be(e+16);
    (void)brain;
    if(!(flags&1)){
        if(swallowed && key==swallowed){swallowed=0;return 1;}
        return list_open?1:0;
    }
    if(!list_open)return 0;
    if(key==KEY_YES){if(!cursor)set_enabled(!enabled);list_dirty=1;}
    else if(key==KEY_NO){list_open=0;swallowed=KEY_NO;header_dirty=1;list_dirty=1;}
    else if(key==KEY_UP)move(-1);
    else if(key==KEY_DOWN)move(1);
    return 1;
}
/* ev_enc: encoder A (1) moves the cursor while the list is open. */
int si_enc(void *brain,void *ev)
{
    const u8 *e=ev;u32 encoder=be(e+12);int delta=(int)be(e+16);
    (void)brain;
    if(!list_open)return 0;
    if(encoder==1 && delta)move(delta>0?1:-1);
    return 1;
}
void si_tick(void *ctrl)
{
    u32 force=header_dirty,refresh=force;
    if(list_dirty){list_dirty=0;((u8 *)ctrl)[0x20]=1;}
    if(!enabled){
        if(core_pool_query_owner[0]==2){core_pool_query_owner[0]=0;pending=0;}
        if(force){header_dirty=0;((u8 *)ctrl)[0x20]=1;}
        return;
    }
    query_audio();
    if(++ticks>=30){ticks=0;update();refresh=1;}
    if(++page_ticks>=60){page_ticks=0;page^=1;refresh=1;}
    if(refresh){
        header_dirty=0;
        if(format() || force)((u8 *)ctrl)[0x20]=1;
    }
}
/* A 3x5 font: digits, A-Z, and - % : . Each row uses three low bits, left first.
 * Draw through the native clipping primitive. */
static const u8 font[40][5]={
 {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
 {7,4,7,1,7},{7,4,7,5,7},{7,1,1,1,1},{7,5,7,5,7},{7,5,7,1,7},
 {2,5,7,5,5},{6,5,6,5,6},{7,4,4,4,7},{6,5,5,5,6},{7,4,6,4,7},
 {7,4,6,4,4},{7,4,5,5,7},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,7},
 {5,5,6,5,5},{4,4,4,4,7},{5,7,7,5,5},{5,7,7,7,5},{7,5,5,5,7},
 {6,5,6,4,4},{7,5,5,7,1},{6,5,6,5,5},{7,4,7,1,7},{7,2,2,2,2},
 {5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5},{5,5,2,2,2},
 {7,1,2,4,7},
 {0,0,7,0,0},{5,1,2,4,5},{0,2,0,2,0},{0,0,0,0,2}};
static const u8 *glyph(char c)
{
    if(c>='0'&&c<='9')return font[c-'0'];
    c=upper(c);
    if(c>='A'&&c<='Z')return font[10+c-'A'];
    switch(c){case '-':return font[36];case '%':return font[37];
    case ':':return font[38];case '.':return font[39];default:return 0;}
}
static void draw_text(void *bmp,int base,const char *s,u32 ink)
{
    int x=2,r,c;const u8 *g;
    while(*s && x<=124){g=glyph(*s++);if(g)for(r=0;r<5;r++)for(c=0;c<3;c++)
        if(g[r]&(4u>>c))fw_fillrect(bmp,x+c,base-r,x+c,base-r,ink);
        x+=4;}
}
static void draw_list(void *bmp)
{
    char text_line[48];u32 n=rows(),k,row;int base;
    fw_fillrect(bmp,0,0,127,63,0);
    for(k=0;k<LIST_LINES && top+k<n;k++){
        row=top+k;base=60-6*(int)k;
        row_text(row,text_line);
        if(row==cursor){fw_fillrect(bmp,0,base-5,127,base+1,1);draw_text(bmp,base,text_line,0);}
        else draw_text(bmp,base,text_line,1);
    }
}
void si_draw(void *bmp,void *ctrl)
{
    int x=20,r,c;const char *s=line;const u8 *g;(void)ctrl;
    if(list_open){draw_list(bmp);return;}
    if(!enabled)return;
    fw_fillrect(bmp,18,54,127,63,0);
    while(*s && x<=124){g=glyph(*s++);if(g)for(r=0;r<5;r++)for(c=0;c<3;c++)
        if(g[r]&(4u>>c))fw_fillrect(bmp,x+c,60-r,x+c,60-r,1);
        x+=4;}
}
