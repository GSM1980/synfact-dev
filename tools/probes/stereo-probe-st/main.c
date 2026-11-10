/* SPDX-License-Identifier: GPL-2.0-or-later
 * Stereo probe, MAIN side (test only, type 70, never in a release bank).
 * Its SYN page shows the stereo transport's counters: the AUDIO engine's,
 * from the shared window, and the MAIN side's (samplers-st stereo/main.c,
 * linked into the same bank).
 */
#include <syntakt/machines.h>
#include <syntakt/stereo.h>
typedef st_u32 u32;
#define FILL ((void (*)(void *,int,int,int,int,u32))0x400f7df2u)
#define TEXT ((void (*)(void *,void *,int,int,u32,const char *,...))0x400f8a0cu)
#define FONT ((void *)0x443da210u)
#define WIN(offset) (*(const volatile u32 *)(ST_STEREO_MAIN+(offset)))
struct st_stereo_counters {u32 interrupts,received,torn,dropped,stale,late,mask,again,read,mix;};
extern struct st_stereo_counters st_stereo_main_counters;
static const struct core_machine_param controls[]={ST_MACHINE_PARAM("NONE","NO CONTROL",0,0x7f00,0)};
const struct core_machine_def stereo_probe_st={
    70,1,"ST PROBE","STPROBE","SYN",controls,ST_MACHINE_NO_INIT,44,5,9
};
static const u32 layout[8] __attribute__((aligned(4)))={
    0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu,0xffffffffu};
static char *decimal(char *p,u32 n)
{
    char t[10];u32 k=0;
    do{t[k++]=(char)('0'+n%10);n/=10;}while(n);
    while(k)*p++=t[--k];
    *p=0;return p;
}
static char *hex2(char *p,u32 n){*p++="0123456789ABCDEF"[(n>>4)&15];*p++="0123456789ABCDEF"[n&15];*p=0;return p;}
static char *text(char *p,const char *s){while(*s)*p++=*s++;*p=0;return p;}
/* Every line fits its buffer whatever the values: at boot the window holds
 * whatever the last session left (a 40-byte line once overran and broke a
 * return address: EXCEPTION V0B, PC 3900C962). Labels <= 8 chars, 10 digits. */
static void pair(void *ctx,int y,const char *a,u32 x,const char *b,u32 z)
{
    char s[48],*p=text(s,a);p=decimal(p,x);p=text(p,b);decimal(p,z);
    TEXT(ctx,FONT,24,y,0,"%s",s);
}
static void draw(const struct st_machine_ui_api *api,const struct core_machine_def *m,void *view,void *ctx,u32 page)
{
    static const char *const label[6]={"S","D","M","A","X","R"};
    const struct st_stereo_counters *c=&st_stereo_main_counters;char s[48],*p;u32 k,v;
    (void)api;(void)m;(void)view;(void)page;
    FILL(ctx,22,0,127,50,0);
    p=text(s,"ST PROBE ");
    p=text(p,WIN(ST_STEREO_TAG)==ST_STEREO_MAGIC?"A ON M ":"A OFF M ");hex2(p,c->mask);
    TEXT(ctx,FONT,24,2,0,"%s",s);
    pair(ctx,12,"MAIN RD ",c->read," MIX ",c->mix);
    pair(ctx,21,"DROP ",c->dropped," LATE ",c->late);
    pair(ctx,30,"TORN ",c->torn," AGAIN ",c->again);
    /* Permille of the block: state copies, drive+width, multimode, AMP,
     * send, sampler renders (whole stereo render). */
    for(p=s,k=0;k<6;k++){
        v=WIN(ST_STEREO_PROFILE+4+4*(k>>1));v=k&1?v&0xffffu:v>>16;
        if(v>9999u)v=9999u;                         /* 6 x "X9999 " = 36 */
        p=text(p,label[k]);p=decimal(p,v);if(k<5)p=text(p," ");
    }
    TEXT(ctx,FONT,24,39,0,"%s",s);
}
static u32 last;
static int idle(const struct st_machine_ui_api *api,const struct core_machine_def *m,void *view,u32 page)
{
    u32 now=st_stereo_main_counters.interrupts>>9;
    (void)api;(void)m;(void)view;(void)page;
    if(now==last)return 0;
    last=now;return 1;
}
const struct st_machine_ui stereo_probe_ui={
    70,1,layout,draw,idle,ST_MACHINE_NO_CALLBACK,ST_MACHINE_NO_CALLBACK,ST_MACHINE_NO_CALLBACK
};
