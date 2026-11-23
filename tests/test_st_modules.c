/* SPDX-License-Identifier: GPL-2.0-or-later; real event-list merge, native host. */
#include <assert.h>
#include <stdio.h>
#include <windows.h>
#include <syntakt/native.h>
typedef st_u32 u32;
static void builtin(void *c){(void)c;}
#define EV(n) const unsigned char core_##n##_table[6]={0};
EV(tick) EV(draw) EV(key) EV(enc) EV(settings) EV(render_in) EV(render_out)
EV(fs_route) EV(fs_write) EV(fs_written) EV(main_mix) EV(track_send) EV(delay_in) EV(reverb_in) EV(fx_cv)
EV(pattern_menu) EV(reload_menu)
const u32 ev_native[]={0};
const u32 ev_tick[]={(u32)builtin,0};
#define EMPTY(n) const u32 ev_##n[]={0};
EMPTY(draw) EMPTY(key) EMPTY(enc) EMPTY(settings) EMPTY(render_in) EMPTY(render_out)
EMPTY(fs_route) EMPTY(fs_write) EMPTY(fs_written) EMPTY(main_mix) EMPTY(track_send) EMPTY(delay_in) EMPTY(reverb_in) EMPTY(fx_cv)
EMPTY(pattern_menu) EMPTY(reload_menu)
const struct st_machine_ui_api core_machine_ui_api={1};
static u32 fail_alloc,fail_init,published,initialized;static const u32 *ticks;
static u32 order[40],order_count;
static u32 input_resets;
void core_machine_input_reset(void){input_resets++;}
static u32 test_seq=2;
#define CORE_IDLE_SEQ_STATE test_seq
#include "../mods/core-st/audio-idle.c"
static int test_idle_ready(void){return 1;}
static void test_idle_preview(u32 a,u32 b,void *c){(void)a;(void)b;(void)c;}
static const struct st_audio_idle_owner test_idle_owner={test_idle_ready,test_idle_preview};
void *fw_malloc(u32 n){return fail_alloc?0:VirtualAlloc((void *)0x47000000u,n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}
void fw_free(void *p){assert(VirtualFree(p,0,MEM_RELEASE));}
void core_event_publish(const u32 *p)
{assert(p[0]==(u32)(core_tick_table+2));ticks=(const u32 *)p[1];assert(p[2] && p[3]==p[1]);
 {u32 k;for(k=4;p[k];k+=2)*(u32 *)p[k]=p[k+1];assert(k==4+2*(ST_NATIVE_OPERATION_LIMIT+(ST_NATIVE_OPERATION_LIMIT+3)/4+1));}
 *(const u32 **)p[2]=ticks;published++;}
static void extra(void *c){(void)c;}
static int init(const struct st_smz_api *api){assert(api->version==1 && api->ui->version==1);initialized++;
 assert(!core_audio_idle_owner);core_audio_idle_owner=&test_idle_owner;return fail_init;}
static int lfo_init(const struct st_smz_api *api){(void)api;order[order_count++]=1;return 0;}
static int skip_init(const struct st_smz_api *api){(void)api;order[order_count++]=2;return 0;}
static int normal_init(const struct st_smz_api *api){(void)api;order[order_count++]=3;return fail_init;}
#include "../mods/core-st/modules.c"
int main(void)
{
    assert(patching_module("LFO3") && patching_module("MODULATION SKIP"));
    {const char *names[]={"","L","LFO","LFO3 extra","lfo3","MODULATION SKI","MODULATION SKIP extra","OTHER"};
     for(u32 i=0;i<sizeof(names)/sizeof(names[0]);i++)assert(!patching_module(names[i]));}
    const struct st_smz_subscription s={ST_EV_TICK,(void (*)(void))extra};
    const struct st_smz_module mod={"TEST",init,&s,1};
    fail_alloc=1;assert(!core_modules_bind(&mod,1) && !initialized && !published);fail_alloc=0;
    fail_init=1;assert(!core_modules_bind(&mod,1) && initialized==1 && !published && input_resets==3);
    assert(!core_audio_idle_begin() && !core_audio_idle_owner);fail_init=0;
    {
        const struct st_smz_module group[]={
            {"LFO3",lfo_init,0,0},{"MODULATION SKIP",skip_init,0,0},{"TEST",normal_init,&s,1}};
        order_count=0;fail_init=1;
        assert(!core_modules_bind(group,3) && order_count==1 && order[0]==3 && !published);
        order_count=0;fail_init=0;
        assert(core_modules_bind(group,3) && published==1);
        assert(order_count==3 && order[0]==3 && order[1]==1 && order[2]==2);
    }
    assert(ticks[0]==(u32)builtin && ticks[1]==(u32)extra && !ticks[2]);
    assert(core_module_count==3 && core_module_subscriptions==1);fw_free((void *)ticks);
    {u32 i;for(i=0;i<4*NATIVE_WORDS;i++)assert(!core_native_interest[i]);} /* stock fast paths stay active */
    /* Core 2.6.1: a bank of CORE_SMZ_MODULES modules initializes and joins
     * the list in order; the binder itself has no module count. */
    {static struct st_smz_module wide[CORE_SMZ_MODULES];u32 i;
     for(i=0;i<CORE_SMZ_MODULES;i++)wide[i]=(struct st_smz_module){"TEST",normal_init,&s,1};
     events[ST_EV_TICK].list=ev_tick;   /* the previous case freed its published list */
     order_count=0;assert(core_modules_bind(wide,CORE_SMZ_MODULES) && published==2 && order_count==CORE_SMZ_MODULES);
     for(i=0;i<CORE_SMZ_MODULES;i++)assert(ticks[1+i]==(u32)extra);
     assert(ticks[0]==(u32)builtin && !ticks[1+CORE_SMZ_MODULES] && core_module_count==CORE_SMZ_MODULES);
     fw_free((void *)ticks);}
    puts("PASS: module rejection precedes hook initializers; both patchers initialize last; publication remains atomic; 32 modules bind");return 0;
}
