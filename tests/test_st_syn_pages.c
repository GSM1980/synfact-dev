/* Production registry, layouts and page selector with the real FM/SP UIs.
 * Native host only; target editor ABI is checked separately in the final SYX. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "../mods/core-st/machines.c"
#include "../mods/core-st/machine-pages.c"
extern const struct core_machine_def fm_tone,sp_one_shot,sp_werp,sp_repitch,sp_slice;
extern const struct st_machine_ui fm_tone_ui,sp_one_ui,sp_werp_ui,sp_repitch_ui,sp_slice_ui;
const struct st_machine_action_codec *core_machine_action_codec;
void core_machine_action_select(const u32 *p){(void)p;}
u32 fw_source_layout[11]={1,2};const u32 fw_lane_ids[54]={0};
static u32 active_type=65,allocations;
void *fw_malloc(u32 n){return VirtualAlloc((void *)(0x48000000u+0x10000u*allocations++),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}
void fw_free(void *p){if((u32)p>=0x40000000u)assert(VirtualFree(p,0,MEM_RELEASE));else free(p);}
u32 fw_view_machine(void *p){(void)p;return active_type;}
void fw_reset_increments(void *p){(void)p;}
void core_machine_copy_page(void *p,u32 b){(void)p;(void)b;assert(0);}
u32 core_stock_slot_to_engine(u32 s,u32 b){(void)b;return s;}
const char *core_machine_format(u32 id,u32 raw){(void)id;(void)raw;return 0;}
u32 core_machine_policy(u32 id){(void)id;return CORE_SMZ_NONE;}
int main(void)
{
    struct core_machine_def defs[5]={fm_tone,sp_one_shot,sp_werp,sp_repitch,sp_slice};
    struct st_machine_ui uis[5]={fm_tone_ui,sp_one_ui,sp_werp_ui,sp_repitch_ui,sp_slice_ui};
    const u32 expected[32]={0,1,2,3,4,5,6,7,23,25,26,27,~0u,~0u,~0u,~0u,
         8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,~0u};
    u8 view[0x100]={0};u32 i,page,k,id,index,args[2],result,selected,changed[28];
    core_machine_ui_definitions=uis;assert(core_machine_bind(defs,5));
    *(u32 *)view=0x401ca0dcu;*(u32 *)(view+0x88)=0x14;*(u32 **)(view+0x7c)=malloc(4);
    **(u32 **)(view+0x7c)=6;*(u32 **)(view+0x80)=*(u32 **)(view+0x84)=*(u32 **)(view+0x7c)+1;
    for(i=0;i<5;i++){
        active_type=defs[i].type;core_machine_sync(view);
        for(page=0;page<uis[i].pages;page++)for(k=0;k<8;k++){
            *(u32 *)(view+0x90)=page;index=uis[i].layout[8*page+k];
            if(!i)assert(index==expected[8*page+k]);
            else assert(index==(i==3 && k==0?~0u:k));
            id=core_machine_nav(view,k);
            assert(core_machine_view_layout(view)[2+k]==id);
            assert(id==(index<defs[i].params?core_machine_engine(defs+i)+index:0));
            if(!id){
                if(i==3){
                    /* Blank REPITCH A must stay blank in the shared native
                     * edit/page-action layout, rather than falling to stock. */
                    u32 old=core_machine_action_begin(view);
                    assert(core_machine_action_layout(6)[2]==0);
                    core_machine_action_end(old);
                }
                continue;
            }
            args[0]=id;assert(core_machine_query(ST_LOCAL,args,0,&result));
            assert(result==core_machine_slot(index));
            assert(core_machine_query(ST_SYNTH,args,0,&result) && result==1); /* classification stays SYN */
            assert(core_machine_parameter(id,&selected)==defs+i && selected==index);
            memset(changed,0,sizeof(changed));changed[selected]++;
            for(u32 j=0;j<28;j++)assert(changed[j]==(j==index));
        }
        assert(core_machine_nav(view,8)==CORE_SMZ_NONE); /* LEVEL remains native */
    }
    /* Hiding the knob never renumbers or drops the legacy stored parameter. */
    active_type=68;args[0]=core_machine_engine(defs+3);
    assert(defs[3].params==8 && defs[3].parameter[0].min==0x0400 &&
           defs[3].parameter[0].max==0x5800 && defs[3].parameter[0].initial==0x4000);
    assert(core_machine_parameter(args[0],&selected)==defs+3 && selected==0);
    assert(core_machine_query(ST_LOCAL,args,0,&result) && result==0x12);
    active_type=44;assert(!core_machine_sync(view));
    assert(core_machine_nav(view,0)==CORE_SMZ_NONE);
    fw_free(*(void **)(view+0x7c));
    puts("PASS: FM/SP layouts, REPITCH A empty on both pages and page actions, legacy TUNE storage, other sampler controls, LEVEL and stock fallback");return 0;
}

unsigned int core_native(unsigned int op,const unsigned int *a){(void)op;(void)a;return op==10?0xffffffffu:0;}
unsigned int core_native_view(unsigned int op,void *v,unsigned int b){(void)v;(void)b;return op==10?0xffffffffu:0;}

unsigned int fw_parameter_selecting(void *model){(void)model;return 0;}
