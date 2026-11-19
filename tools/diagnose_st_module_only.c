/* SPDX-License-Identifier: GPL-2.0-or-later
 * Offline 32-bit host probe for module-only SMZ parameter dispatch.
 * The registry/pages are production code; allocation, native dispatch and
 * parameter metadata are explicit doubles. No firmware or device needed.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#ifndef REGISTRY_SOURCE
#define REGISTRY_SOURCE "../mods/core-st/machines.c"
#endif
#include REGISTRY_SOURCE
#include "../mods/core-st/machine-pages.c"

u32 fw_source_layout[11]={0};
const u32 fw_lane_ids[54]={0};
const struct st_machine_action_codec *core_machine_action_codec;
static u32 allocations,provider_calls;

void *fw_malloc(u32 n)
{return VirtualAlloc((void *)(0x48000000u+0x100000u*allocations++),n,
                    MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}
void fw_free(void *p){assert(VirtualFree(p,0,MEM_RELEASE));}
void core_machine_copy_page(void *p,u32 b){(void)p;(void)b;assert(0);}
u32 core_stock_slot_to_engine(u32 s,u32 b){(void)b;return s;}
const char *core_machine_format(u32 id,u32 raw){(void)id;(void)raw;return 0;}
u32 core_machine_policy(u32 id){(void)id;return CORE_SMZ_NONE;}
void core_machine_action_select(const u32 *p){(void)p;}
u32 fw_view_machine(void *v){(void)v;return 1;}
void fw_reset_increments(void *v){(void)v;}
u32 fw_parameter_selecting(void *m){(void)m;return 0;}
u32 core_native(u32 op,const u32 *a){(void)op;(void)a;return CORE_SMZ_NONE;}
u32 core_native_view(u32 op,void *v,u32 knob)
{(void)v;return op==ST_NATIVE_PAGE_NAV && knob<8?0x3f9u+knob:CORE_SMZ_NONE;}

/* Only dispatch is under investigation: metadata is a double, not the real
 * LFO3 provider or firmware descriptor table. IDs/query numbers match ui.c. */
static int metadata(u32 q,u32 *a,void *r,u32 *value)
{
    u32 id=q==ST_LABEL || q==ST_LONG_LABEL?a[1]:a[0];
    provider_calls++;
    if(q==ST_UI_ENGINE){
        if(id-0x4e0u>=10)return 0;
        *value=0x3f9u+id-0x4e0u;return 1;
    }
    if(id-0x3f9u>=10)return 0;
    switch(q){
    case ST_LABEL:case ST_LONG_LABEL:*value=(u32)"LFO3 metadata double";return 1;
    case ST_ENGINE_UI:*value=0x4e0u+id-0x3f9u;return 1;
    case ST_RANGE:{u32 *out=r;out[0]=0;out[1]=0x7f00;out[2]=0x4000;
        *value=(u32)out;return 1;}
    case ST_CACHED:case ST_ENCODER_CACHED:case ST_FORMAT:
        a[0]=0x60u+id-0x3f9u;return 0;
    default:return 0;
    }
}
static int arp_metadata(u32 q,u32 *a,void *r,u32 *v)
{(void)q;(void)a;(void)r;(void)v;return 0;}

static void probe(const char *name,const struct core_machine_def *defs,u32 count,int arp)
{
    u32 a[3]={0,0x3f9,0},out[3],value=0,q,i,answered=0,aliases=0;
    static const u32 queries[]={ST_LABEL,ST_LONG_LABEL,ST_RANGE,ST_UI_ENGINE,ST_ENGINE_UI};
    memset(core_parameter_providers,0,sizeof(core_parameter_providers));
    assert(core_machine_bind(defs,count));
    core_parameter_provider_set(metadata);
    if(arp)core_parameter_provider_set(arp_metadata);
    assert(core_parameter_provider(ST_LABEL,a,0,&value));
    provider_calls=0;
    for(i=0;i<10;i++){
        for(q=0;q<sizeof(queries)/sizeof(queries[0]);q++){
            a[0]=queries[q]==ST_UI_ENGINE?0x4e0+i:0x3f9+i;a[1]=0x3f9+i;
            answered+=!!core_machine_query(queries[q],a,out,&value);
        }
        for(q=ST_CACHED;q<=ST_ENCODER_CACHED;q++){
            if(q!=ST_CACHED && q!=ST_FORMAT && q!=ST_ENCODER_CACHED)continue;
            a[0]=0x3f9+i;
            assert(!core_machine_query(q,a,0,&value));
            aliases+=a[0]==0x60+i;
        }
    }
    assert(core_machine_nav(0,0)==0x3f9); /* PAGE_NAV precedes the machine gate. */
    assert(!core_machine_present(79));   /* Candidate must initialize directory. */
    assert(answered==50u*!!core_machine_active && aliases==30u*!!core_machine_active);
    assert(provider_calls==80u*!!core_machine_active);
    printf("%s: bind=OK active=%u machines=%u direct_provider=OK navigation=OK "
           "metadata=%u/50 aliases=%u/30 provider_calls=%u\n",
           name,core_machine_active,core_machine_count,answered,aliases,provider_calls);
    for(i=0;i<0x1f9;i++){
        a[0]=0;a[1]=i;
        assert(!core_machine_query(ST_LABEL,a,0,&value));
    }
    if(!count){
        for(i=64;i<80;i++)assert(!core_machine_present(i));
        if(core_machine_active){
            assert(core_machine_lanes[54]==0x47 && core_machine_lanes[70]==0x57);
        }
    }
}

int main(void)
{
    struct core_machine_param p={"TEST","TEST",0,0x7f00,0x4000,0x13};
    struct core_machine_def machine={64,1,"TEST","T","SYN",&p,ST_MACHINE_NO_INIT,44,5,9};
    u32 a[2]={0,0x3f9},value=0;
    assert(!core_machine_active && !core_machine_count);
    assert(!core_machine_query(ST_LABEL,a,0,&value) && !allocations);
    puts("No bank: active=0 metadata=unhandled allocations=0");
    probe("LFO3 only",0,0,0);
    probe("LFO3 + ARP",0,0,1);
    probe("one machine + LFO3",&machine,1,0);
    return 0;
}
