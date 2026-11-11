/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real core registry/pages plus Digi Mono's real UI. The old bind-time
 * layout snapshot fails this test: SAW's empty E never becomes PULSE SUB2.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#ifndef DM_CORE_MACHINES_SOURCE
#define DM_CORE_MACHINES_SOURCE "../mods/core-st/machines.c"
#endif
#include DM_CORE_MACHINES_SOURCE
#include "../mods/core-st/machine-pages.c"

static u32 active_type=71,allocations;
static unsigned model_reads;
static st_u32 dm_get(void *view,st_u32 id,unsigned char *locked)
{assert(id==0x1f9u+(71-64)*32+8);*locked=0;return *(u32 *)((u8 *)view+0x100);}
#define DM_VALUE dm_get
#include "../mods/digimono-st/main.c"

u32 fw_source_layout[11]={1,2};const u32 fw_lane_ids[54]={0};
const struct st_machine_action_codec *core_machine_action_codec;
void core_machine_action_select(const u32 *p){(void)p;}
void *fw_malloc(u32 n)
{return VirtualAlloc((void *)(0x48000000u+0x10000u*allocations++),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}
void fw_free(void *p)
{if((u32)p>=0x40000000u)assert(VirtualFree(p,0,MEM_RELEASE));else free(p);}
u32 fw_view_machine(void *p){(void)p;return active_type;}
void fw_reset_increments(void *p){(void)p;}
void core_machine_copy_page(void *p,u32 b){(void)p;(void)b;assert(0);}
u32 core_stock_slot_to_engine(u32 s,u32 b){(void)b;return s;}
const char *core_machine_format(u32 id,u32 raw){(void)id;(void)raw;return 0;}
u32 core_machine_policy(u32 id){(void)id;return CORE_SMZ_NONE;}
unsigned int core_native(unsigned int op,const unsigned int *a)
{(void)a;return op==ST_NATIVE_PAGE_NAV?CORE_SMZ_NONE:0;}
unsigned int core_native_view(unsigned int op,void *v,unsigned int b)
{(void)v;(void)b;return op==ST_NATIVE_PAGE_NAV?CORE_SMZ_NONE:0;}
unsigned int fw_parameter_selecting(void *model){(void)model;return 0;}

static void init_view(u8 *view)
{
    memset(view,0,0x110);
    *(u32 *)view=0x401ca0dcu;*(u32 *)(view+0x88)=0x14;
    u32 *pages=malloc(8);assert(pages);pages[0]=pages[1]=6;
    *(u32 **)(view+0x7c)=pages;*(u32 **)(view+0x80)=*(u32 **)(view+0x84)=pages+2;
}
static void check_view(u8 *view,unsigned kind)
{
    static const unsigned counts[8]={1,4,7,8,8,8,6,8};
    unsigned seen=0,i;u32 old,args[2],value;
    *(u32 *)(view+0x100)=kind<<8;
    /* TYPE is changed on SYN 2, then return to SYN 1 on the same view. */
    *(u32 *)(view+0x90)=1;dm_idle(0,&dm_monomachine,view,1);
    assert(core_machine_nav(view,0)==DM_ENGINE+8);
    for(i=1;i<8;i++)assert(core_machine_nav(view,i)==0);
    *(u32 *)(view+0x90)=0;
    old=core_machine_action_begin(view);
    for(i=0;i<8;i++){
        u32 expected=dm_labels[kind][i][0]?DM_ENGINE+i:0;
        assert(core_machine_nav(view,i)==expected);
        assert(core_machine_view_layout(view)[2+i]==expected);
        assert(core_machine_action_layout(6)[2+i]==expected);
        if(expected){
            u32 index=99;
            seen++;assert(core_machine_parameter(expected,&index)==&dm_monomachine && index==i);
            args[0]=0;args[1]=expected;
            assert(core_machine_query(ST_LABEL,args,0,&value));
            assert(!strcmp((const char *)value,dm_labels[kind][i]));
        }
    }
    assert(seen==counts[kind]);
    assert(core_machine_nav(view,8)==CORE_SMZ_NONE); /* LEVEL stays native */
    core_machine_action_end(old);
    args[0]=71;
    assert(core_machine_query(ST_LAYOUT,args,0,&value));
    assert(((u32 *)value)[2]==DM_ENGINE); /* TUNE always present */
    assert(((u32 *)value)[10]==10);      /* native LEVEL descriptor unchanged */
}
struct destination_model {u32 *vt;u32 type;};
static u32 model_get(void *raw,u32 id)
{struct destination_model *model=raw;assert(id==DM_ENGINE+DM_TYPE_CONTROL);model_reads++;return model->type;}
static int module_flags(u32 q,u32 *args,void *model,u32 *value)
{(void)model;if(q==ST_FLAGS && args[0]==ST_MODULE_ID){*value=0x200;return 1;}return 0;}
static void check_destinations(void)
{
    u32 vt[11]={0},args[2],value,index;
    struct destination_model model[2]={{vt,0},{vt,0}};
    struct st_machine_host_api host={0};
    struct st_machine_ui_api ui={0};
    struct st_smz_api api={2,&ui};
    ui.version=5;ui.host=&host;host.parameter_provider=core_parameter_provider_set;
    host.version=14;assert(dm_destination_init(&api));assert(!core_parameter_providers[0]);
    host.version=15;assert(!dm_destination_init(&api));
    core_parameter_provider_set(module_flags);
    vt[0x28/4]=(u32)model_get;
    for(unsigned round=0;round<3;round++)for(unsigned t=0;t<8;t++){
        unsigned other=7-t,reads;
        model[0].type=t<<8;model[1].type=other<<8;
        for(unsigned m=0;m<2;m++){
            unsigned type=m?other:t;
            /* No SYN idle call: the popup's own model determines visibility. */
            for(index=0;index<=8;index++){
                u32 expected=index==8 || dm_labels[type][index][0]?0xe00:0;
                args[0]=DM_ENGINE+index;
                assert(core_machine_query(ST_UI_DEST_FLAGS,args,&model[m],&value) && value==expected);
                assert(dm_shown==type);
                reads=model_reads;
                assert(core_machine_query(ST_FLAGS,args,0,&value) && value==0xe00);
                assert(model_reads==reads); /* runtime never consults current UI/Sound */
                assert(core_machine_query(ST_LOCAL,args,0,&value) && value==core_machine_slot(index));
            }
            assert(model[0].type==t<<8 && model[1].type==other<<8);
        }
    }
    args[0]=ST_MODULE_ID;
    assert(core_machine_query(ST_UI_DEST_FLAGS,args,model,&value) && value==0x200);
    args[0]=DM_ENGINE;assert(core_machine_query(ST_UI_DEST_FLAGS,args,0,&value) && value==0xe00);
}
int main(void)
{
    u8 views[2][0x110];
    core_machine_ui_definitions=&dm_ui;assert(core_machine_bind(&dm_monomachine,1));
    init_view(views[0]);init_view(views[1]);
    /* Bind starts at SAW (E empty), then repeatedly restore all eight slots.
     * Different view/Sound selections exercise the shared mutable UI state. */
    for(unsigned round=0;round<3;round++){
        for(unsigned kind=0;kind<8;kind++)check_view(views[0],kind);
        for(unsigned kind=8;kind--;)check_view(views[1],kind);
    }
    check_view(views[0],3);assert(core_machine_nav(views[0],4)==DM_ENGINE+4); /* SUB2 */
    check_view(views[0],4);assert(core_machine_nav(views[0],4)==DM_ENGINE+4); /* WAVE */
    check_view(views[0],5);assert(core_machine_nav(views[0],4)==DM_ENGINE+4); /* VOIC */
    check_view(views[0],7);assert(core_machine_nav(views[0],4)==DM_ENGINE+4); /* WAVE */
    check_view(views[1],0);assert(core_machine_nav(views[1],1)==0);
    check_view(views[0],3);assert(core_machine_nav(views[0],1)==DM_ENGINE+1);
    check_destinations();
    fw_free(*(void **)(views[0]+0x7c));fw_free(*(void **)(views[1]+0x7c));
    puts("PASS: real core/Digi Mono pages and destinations: 8 types/two models, no SYN dependency, hidden UI targets, stable runtime flags/storage, module fallback, API gating and native LEVEL");
    return 0;
}
