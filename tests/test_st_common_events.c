/* Production event merge/context dispatch and tick traversal, native host.
 * Native receiver and layering sites are separately checked in the final SYX. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <syntakt/native.h>
#include "../mods/core-st/machines.h"
typedef st_u32 u32;
static u32 raw_calls,enc_calls,key_calls,draw_calls,late_calls,init_calls,publish_calls;
static unsigned char view[256],ctrl[64],node[12],overlay[12],unknown[12];
static int idle_changed=1;static u32 idles,paints,allocations,selecting;
static struct core_machine_def definition={65,28};
static int raw(void *brain,void *event){(void)brain;(void)event;raw_calls++;return 1;}
static int encoder(void *v,void *e,u32 id)
{assert(v==view && e==node && id==0x21c);enc_calls++;return 1;}
static int late(void *v,void *e,u32 id){(void)v;(void)e;(void)id;late_calls++;return 1;}
static void key(void *v,void *e,u32 id){assert(v==view && e==node && !id);key_calls++;}
static void draw(void *v,void *b,u32 id){assert(v==view && b==ctrl && !id);draw_calls++;}
#define EV(n) const unsigned char core_##n##_table[6]={0};const u32 ev_##n[]={0};
EV(tick) EV(draw) EV(key) EV(enc) EV(settings) EV(render_in) EV(render_out)
EV(fs_route) EV(fs_write) EV(fs_written) EV(main_mix)
EV(track_send) EV(delay_in) EV(reverb_in)
const u32 ev_native[]={0};
const struct st_machine_ui_api core_machine_ui_api={4};
void core_machine_input_reset(void){}
void *fw_malloc(u32 n)
{allocations++;return VirtualAlloc((void *)0x47000000u,n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}
void fw_free(void *p){assert(VirtualFree(p,0,MEM_RELEASE));}
void core_event_publish(const u32 *p)
{u32 n=0;while(p[2*n])n++;assert(n==6+(ST_NATIVE_OPERATION_LIMIT+3)/4+1);for(n=0;n<3;n++)*(const u32 **)p[4*n+2]=(const u32 *)p[4*n+3];
 for(n=12;p[n];n+=2){assert(!p[n+1]);*(u32 *)p[n]=p[n+1];}publish_calls++;}   /* no native subscriber */
#include "../mods/core-st/modules.c"
static int init(const struct st_smz_api *a){assert(a->version==2 && a->ui->version==4);init_calls++;return 0;}
static int legacy_init(const struct st_smz_api *a){assert(a->version==1 && a->ui->version==4);init_calls++;return 0;}
volatile u32 core_machine_active=1;
u32 fw_view_machine(void *v){assert(v==view);return 65;}
const struct core_machine_def *core_machine_find(u32 t){return t==65?&definition:0;}
void fw_invalidate(void *v){assert(v==view);paints++;}
static int idle(const struct st_machine_ui_api *a,const struct core_machine_def *m,void *v,u32 page)
{assert(a==&core_machine_ui_api && m==&definition && v==view && page==1);idles++;return idle_changed;}
static struct st_machine_ui ui={65,2,0,0,idle};
const struct st_machine_ui *core_machine_ui(const struct core_machine_def *m){return m?&ui:0;}
void core_mods_auto_confirm_tick(void){}
#include "../mods/core-st/machine-events.c"
int main(void)
{
    const struct st_smz_subscription s[]={
        {ST_EV_ENC|ST_EV_VIEW,(void (*)(void))encoder},
        {ST_EV_ENC|ST_EV_VIEW,(void (*)(void))late},
        {ST_EV_KEY|ST_EV_VIEW,(void (*)(void))key},
        {ST_EV_DRAW|ST_EV_VIEW,(void (*)(void))draw}};
    const struct st_smz_subscription old={ST_EV_ENC,(void (*)(void))raw};
    const struct st_smz_module m[]={{"LEGACY",legacy_init,&old,1},{"COMMON",init,s,4}};
    assert(core_modules_bind(m,2) && init_calls==2 && publish_calls==1 && allocations==1);
    assert(events[ST_EV_ENC].list[0]==(u32)raw &&
        events[ST_EV_ENC].list[1]==((u32)encoder|VIEW_ENTRY) &&
        events[ST_EV_ENC].list[2]==((u32)late|VIEW_ENTRY) && !events[ST_EV_ENC].list[3]);
    assert(core_event_view(ST_EV_ENC,view,node,0x21c)==1);
    assert(enc_calls==1 && !raw_calls && !late_calls);
    assert(!core_event_view(ST_EV_KEY,view,node,0) && key_calls==1);
    assert(!core_event_view(ST_EV_DRAW,view,ctrl,0) && draw_calls==1);
    assert(!core_event_view(ST_EV_TICK,view,ctrl,0));
    /* Live native visible list, foreground gates, stock/unknown views,
     * stationary cursor, opaque overlay, deactivation and view removal. */
    static const u32 syn_pages[2]={6,6};
    *(const u32 **)(view+0x7c)=syn_pages;
    *(u32 *)view=0x401c8d94u;*(u32 *)(view+0x2c)=1;
    *(u32 *)(view+0x88)=0x14;*(u32 *)(view+0x90)=1;
    *(void **)(ctrl+0x1c)=node;*(void **)node=ctrl+0x14;*(void **)(node+8)=view;
    core_machine_tick(ctrl);assert(idles==1 && paints==1);
    idle_changed=0;core_machine_tick(ctrl);assert(idles==2 && paints==1);
    selecting=0xff;core_machine_tick(ctrl);assert(idles==2 && paints==1);selecting=0;
    view[0x17]=1;core_machine_tick(ctrl);view[0x17]=0;
    view[0x30]=1;core_machine_tick(ctrl);view[0x30]=0;
    *(u32 *)(view+0x2c)=0;core_machine_tick(ctrl);*(u32 *)(view+0x2c)=1;
    *(u32 *)(view+0x88)=0;core_machine_tick(ctrl);*(u32 *)(view+0x88)=0x14;
    *(u32 *)(view+0x90)=2;core_machine_tick(ctrl);*(u32 *)(view+0x90)=1;
    assert(idles==2 && paints==1);
    *(void **)(ctrl+0x1c)=overlay;*(void **)overlay=ctrl+0x14;*(void **)(overlay+8)=unknown;
    core_machine_tick(ctrl);assert(idles==2); /* opaque overlay excludes view */
    *(void **)overlay=node;core_machine_tick(ctrl);assert(idles==3); /* transparent overlay */
    core_machine_active=0;core_machine_tick(ctrl);assert(idles==3);core_machine_active=1;
    *(void **)(ctrl+0x1c)=ctrl+0x14;core_machine_tick(ctrl);assert(idles==3);
    assert(allocations==1 && *(u32 *)view==0x401c8d94u); /* no clones/cached view */
    fw_free((void *)events[ST_EV_DRAW].list);
    puts("PASS: one collection per event; raw/view isolation; encoder consumption; key/draw contexts; tick visibility/lifetime and stationary-wave gates; no idle vptr edits");
    return 0;
}

unsigned int fw_parameter_selecting(void *model){(void)model;return selecting;}
