/* Ordinary resident native subscribers remain active without any SMZ module. */
#include <assert.h>
#include <stdio.h>
#include <syntakt/modules.h>
typedef st_u32 u32;
#define EV(n) const unsigned char core_##n##_table[6]={0};const u32 ev_##n[]={0};
EV(tick) EV(draw) EV(key) EV(enc) EV(settings) EV(render_in) EV(render_out)
EV(fs_route) EV(fs_write) EV(fs_written) EV(main_mix)
static u32 calls,publications;
static int resident(u32 op,const u32 *args,u32 *result)
{assert(op==7 && args[0]==2);calls++;*result=123;return 1;}
const u32 ev_native[]={(u32)resident,0};
const struct st_machine_ui_api core_machine_ui_api={7};
void *fw_malloc(u32 n){(void)n;assert(0);return 0;}
void fw_free(void *p){(void)p;assert(0);}
void core_machine_input_reset(void){}
void core_event_publish(const u32 *p)
{
    u32 k;for(k=0;p[k];k+=2)*(u32 *)p[k]=p[k+1];                     /* interest + page view words */
    assert(k==2*6);publications++;
}
#include "../mods/core-st/modules.c"
static int interest_open(void){u32 i;for(i=0;i<4*NATIVE_WORDS;i++)if(i<ST_NATIVE_OPERATION_COUNT?core_native_interest[i]!=1:i==ST_NATIVE_OPERATION_COUNT+1 && !core_native_interest[i])return 0;return 1;}
int main(void)
{
    u32 args[]={2},result=0;
    assert(interest_open());core_native_initialize();assert(interest_open());
    assert(core_event_native(7,args,&result) && result==123 && calls==1);
    assert(core_modules_bind(0,0) && publications==1 && interest_open());
    assert(!core_module_count && !core_module_subscriptions);
    assert(core_event_native(7,args,&result) && calls==2);
    puts("PASS: resident native subscriptions survive boot and zero optional modules, without allocation or suppression");
}
