/* SPDX-License-Identifier: GPL-2.0-or-later
 * High memory probe, MAIN side (test only, type 70, never in a release bank).
 * Its one SYN page shows what audio.c published in the shared window
 * (MAIN 100007f0). OS 1.42 drawing calls, as mods/md-st's picker uses them.
 */
#include <syntakt/machines.h>
typedef st_u32 u32;
#define FONT_OF ((void *(*)(void *))0x400f9478u)
#define TEXT ((void (*)(void *, void *, int, int, u32, const char *))0x400f8b68u)
#define FILL ((void (*)(void *, int, int, int, int, u32))0x400f7fb6u)
#define FONT_DATA ((void *)0x443db210u)
#define STAT ((const volatile u32 *)0x100007f0u)
#define MAGIC 0x48505231u
#define HIGH_WORDS ((0x47ff0000u-0x4404ff50u)/4u)
#define FILL_BLOCKS ((HIGH_WORDS+8191u)/8192u)     /* audio.c FILL_SLICE */
#define SWEEP_BLOCKS ((HIGH_WORDS+2047u)/2048u)    /* audio.c CHECK_SLICE: one whole check */
static const struct core_machine_param controls[]={ST_MACHINE_PARAM("NONE","NO CONTROL",0,0x7f00,0)};
const struct core_machine_def high_memory_probe_st={
    70,1,"HIGH MEM PROBE","HPROBE","SYN",controls,ST_MACHINE_NO_INIT,44,5,9
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
    u32 i;
    for(i=0;i<4;i++)v[i]=STAT[i];
    return v[0]==(MAGIC^(v[1]+v[2]+v[3]));
}
/* Native LCD coordinates increase upwards: the top row is y = 56. */
static void draw(const struct st_machine_ui_api *api,const struct core_machine_def *m,void *view,void *ctx,u32 page)
{
    u32 v[4],blocks,bad,peak;char s[32],*p;
    void *font=FONT_OF(FONT_DATA);
    (void)api;(void)m;(void)view;(void)page;
    FILL(ctx,20,0,127,63,0);
    TEXT(ctx,font,24,56,104,"HIGH MEM PROBE");
    if(!snapshot(v)){TEXT(ctx,font,24,44,104,"NO DATA YET");return;}
    blocks=v[1];bad=v[2]&0xffffu;peak=(v[2]>>16)*16u;
    p=decimal(text(s,"BLOCKS "),blocks);TEXT(ctx,font,24,46,104,s);
    if(blocks<FILL_BLOCKS){p=decimal(text(s,"FILLING "),blocks*100u/FILL_BLOCKS);text(p,"%");}
    else decimal(text(s,"SWEEPS "),(blocks-FILL_BLOCKS)/SWEEP_BLOCKS);
    TEXT(ctx,font,24,36,104,s);
    p=decimal(text(s,"HIGH BAD "),bad);if(v[3]!=0xffffffffu){p=text(p," @");hex(p,v[3]);}
    TEXT(ctx,font,24,26,104,s);
    p=decimal(text(s,"STACK PEAK "),peak);text(p," B");TEXT(ctx,font,24,16,104,s);
}
static int idle(const struct st_machine_ui_api *api,const struct core_machine_def *m,void *view,u32 page)
{
    u32 now=STAT[1]>>8;
    (void)api;(void)m;(void)view;(void)page;
    if(now==last)return 0;
    last=now;return 1;
}
const struct st_machine_ui high_memory_probe_ui={
    70,1,layout,draw,idle,ST_MACHINE_NO_CALLBACK,ST_MACHINE_NO_CALLBACK,ST_MACHINE_NO_CALLBACK
};
