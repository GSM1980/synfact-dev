/* SPDX-License-Identifier: GPL-2.0-or-later
 * Memory probe, MAIN side (test only, type 70, never in a release bank).
 * Its one SYN page shows what audio.c published in the shared window
 * (MAIN 0x100007e0) and checks the window's pattern itself.
 */
#include <syntakt/machines.h>
typedef st_u32 u32;
#define FILL ((void (*)(void *,int,int,int,int,u32))0x400f7df2u)
#define TEXT ((void (*)(void *,void *,int,int,u32,const char *,...))0x400f8a0cu)
#define FONT ((void *)0x443da210u)
#define STAT ((const volatile u32 *)0x100007e0u)
#define MAGIC 0x4d505231u
static const struct core_machine_param controls[]={ST_MACHINE_PARAM("NONE","NO CONTROL",0,0x7f00,0)};
const struct core_machine_def memory_probe_st={
    70,1,"MEM PROBE","MPROBE","SYN",controls,ST_MACHINE_NO_INIT,44,5,9
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
static char *hex(char *p,u32 n)
{
    int s;for(s=28;s>=0;s-=4)*p++="0123456789ABCDEF"[(n>>s)&15];
    *p=0;return p;
}
static char *text(char *p,const char *s){while(*s)*p++=*s++;*p=0;return p;}
static u32 last;
static int snapshot(u32 *v)
{
    u32 i,sum=MAGIC;
    if(STAT[0]!=MAGIC)return 0;
    for(i=0;i<7;i++)v[i]=STAT[i+1];
    for(i=0;i<6;i++)sum+=v[i];
    return sum==v[6];
}
static void line(void *ctx,int y,const char *label,u32 count,u32 first)
{
    char s[32],*p=text(s,label);
    p=decimal(p,count);
    if(first!=0xffffffffu){p=text(p," @");hex(p,first);}
    TEXT(ctx,FONT,24,y,0,"%s",s);
}
static void draw(const struct st_machine_ui_api *api,const struct core_machine_def *m,void *view,void *ctx,u32 page)
{
    u32 v[7];char s[32];
    (void)api;(void)m;(void)view;(void)page;
    FILL(ctx,22,0,127,50,0);
    TEXT(ctx,FONT,24,2,0,"%s","MEM PROBE");
    if(!snapshot(v)){TEXT(ctx,FONT,24,14,0,"%s","NO DATA YET");return;}
    text(decimal(text(s,"BLOCKS "),v[0]),"");TEXT(ctx,FONT,24,12,0,"%s",s);
    line(ctx,21,"WIN BAD ",v[1],v[4]);
    line(ctx,30,"SRAM BAD ",v[2],v[5]);
    line(ctx,39,"SRAM START ",v[3],0xffffffffu);
}
static int idle(const struct st_machine_ui_api *api,const struct core_machine_def *m,void *view,u32 page)
{
    u32 now=STAT[1]>>8;
    (void)api;(void)m;(void)view;(void)page;
    if(now==last)return 0;
    last=now;return 1;
}
const struct st_machine_ui memory_probe_ui={
    70,1,layout,draw,idle,ST_MACHINE_NO_CALLBACK,ST_MACHINE_NO_CALLBACK,ST_MACHINE_NO_CALLBACK
};
