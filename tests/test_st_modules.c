/* SPDX-License-Identifier: GPL-2.0-or-later; real event-list merge, native host. */
#include <assert.h>
#include <stdio.h>
#include <windows.h>
#include <syntakt/native.h>
typedef st_u32 u32;
static void builtin(void *c){(void)c;}
#define EV(n) const unsigned char core_##n##_table[6]={0};
EV(tick) EV(draw) EV(key) EV(enc) EV(settings) EV(render_in) EV(render_out)
EV(fs_route) EV(fs_write) EV(fs_written) EV(main_mix) EV(track_send) EV(delay_in) EV(reverb_in)
const u32 ev_native[]={0};
const u32 ev_tick[]={(u32)builtin,0};
#define EMPTY(n) const u32 ev_##n[]={0};
EMPTY(draw) EMPTY(key) EMPTY(enc) EMPTY(settings) EMPTY(render_in) EMPTY(render_out)
EMPTY(fs_route) EMPTY(fs_write) EMPTY(fs_written) EMPTY(main_mix) EMPTY(track_send) EMPTY(delay_in) EMPTY(reverb_in)
const struct st_machine_ui_api core_machine_ui_api={1};
static u32 fail_alloc,fail_init,published,initialized;static const u32 *ticks;
static u32 input_resets;
void core_machine_input_reset(void){input_resets++;}
void *fw_malloc(u32 n){return fail_alloc?0:VirtualAlloc((void *)0x47000000u,n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}
void fw_free(void *p){assert(VirtualFree(p,0,MEM_RELEASE));}
void core_event_publish(const u32 *p)
{assert(p[0]==(u32)(core_tick_table+2));ticks=(const u32 *)p[1];assert(p[2] && p[3]==p[1]);
 {u32 k;for(k=4;p[k];k+=2)*(u32 *)p[k]=p[k+1];assert(k==4+2*((ST_NATIVE_OPERATION_LIMIT+3)/4+1));}
 *(const u32 **)p[2]=ticks;published++;}
static void extra(void *c){(void)c;}
static int init(const struct st_smz_api *api){assert(api->version==1 && api->ui->version==1);initialized++;return fail_init;}
#include "../mods/core-st/modules.c"
int main(void)
{
    const struct st_smz_subscription s={ST_EV_TICK,(void (*)(void))extra};
    const struct st_smz_module mod={"TEST",init,&s,1};
    fail_alloc=1;assert(!core_modules_bind(&mod,1) && !initialized && !published);fail_alloc=0;
    fail_init=1;assert(!core_modules_bind(&mod,1) && initialized==1 && !published && input_resets==3);fail_init=0;
    assert(core_modules_bind(&mod,1) && initialized==2 && published==1);
    assert(ticks[0]==(u32)builtin && ticks[1]==(u32)extra && !ticks[2]);
    assert(core_module_count==1 && core_module_subscriptions==1);fw_free((void *)ticks);
    {u32 i;for(i=0;i<4*NATIVE_WORDS;i++)assert(!core_native_interest[i]);} /* stock fast paths stay active */
    puts("PASS: module init failure/heap failure publish nothing; successful module joins existing event table directly");return 0;
}
