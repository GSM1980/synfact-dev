/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "machines.h"
typedef st_u32 u32;typedef unsigned short u16;
extern const struct st_machine_action_codec *core_machine_action_codec;
volatile u32 core_machine_active;
const struct core_machine_def *core_machine_definitions;
u32 core_machine_count;
u32 core_machine_extended;
u32 *core_machine_shadow;
static const struct core_machine_def **directory;
const struct st_machine_ui *core_machine_ui_definitions;
const struct st_machine_ui *core_machine_ui(const struct core_machine_def *m)
{return m && core_machine_ui_definitions?core_machine_ui_definitions+(m-core_machine_definitions):0;}
volatile u32 core_machine_availability_seen,core_machine_last_track,core_machine_menu_limit;
volatile u32 core_machine_available_mask,core_machine_name_seen,core_machine_category_seen;
volatile u32 core_machine_vector_mask,core_machine_vector_count,core_machine_vector_calls;
void core_machine_menu_observe(const u32 *vector)
{
    const u32 *p=(const u32 *)vector[0],*end=(const u32 *)vector[1];u32 mask=0,n;
    if((u32)p<0x40000000u || (u32)end<(u32)p || (u32)end-(u32)p>320)return;
    n=((u32)end-(u32)p)/4;
    while(p<end){u32 type=*p++;if(type>=64 && type<80)mask|=1u<<(type-64);}
    core_machine_vector_mask=mask;core_machine_vector_count=n;core_machine_vector_calls++;
}
static u32 (*layouts)[4][11];
extern void *fw_malloc(u32);extern void fw_free(void *);
static const u32 slots[28]={0x12,0x13,0x14,0x15,0x16,0x17,0x1a,0x19,
    0x36,0x37,0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,0x40,0x41,0x42,0x43,0x44,0x45,
    0,0x11,0x18,0x46};
extern void core_machine_copy_page(void *,u32);
extern u32 core_stock_slot_to_engine(u32,u32);
extern u32 fw_source_layout[];
extern const u32 fw_lane_ids[];
u32 *core_machine_lanes;
const struct core_machine_def *core_machine_find(u32 type)
{
    return core_machine_active && type>=64 && type<80?directory[type-64]:0;
}
u32 core_machine_engine(const struct core_machine_def *m){return 0x1f9+(m->type-64)*32;}
u32 core_machine_slot(u32 index){return index<28?slots[index]:CORE_SMZ_NONE;}
#define find core_machine_find
#define engine core_machine_engine
static u32 ui(const struct core_machine_def *m){return 0x2e0+(m->type-64)*32;}
const struct core_machine_def *core_machine_parameter(u32 id,u32 *index)
{
    const struct core_machine_def *m;
    if(id<0x1f9 || id>=0x3f9)return 0;
    *index=(id-0x1f9)&31;m=find(64+(id-0x1f9)/32);
    return m && *index<m->params?m:0;
}
int core_machine_present(u32 type){return find(type)!=0;}
/* Complete the native machine-selection defaults transaction. Stock handles
 * its first eight controls; additional stored controls use the SAME descriptor
 * defaults, Sound getter and model notifications, before the final stock write.
 * Called only at the last slot lookup from 40024db2, never on Sound/project load.
 */
void core_machine_defaults(u32 type,void *model,u32 quiet)
{
    const struct core_machine_def *m=find(type);u32 i,*vt;core_u8 *sound;
    if(!m || m->params<=8 || !model)return;
    vt=*(u32 **)model;sound=((core_u8 *(*)(void *))vt[0x28/4])(model);
    if(!sound)return;
    for(i=8;i<m->params;i++)
        *(u16 *)(sound+0x14+2*slots[i])=(u16)m->parameter[i].initial;
    if(!(quiet&255u))for(i=8;i<m->params;i++){
        u32 event[3]={0x401c39d8u,slots[i],0x01000000u};
        ((void (*)(void *,void *))vt[0x10/4])(model,event);
    }
}
int core_machine_bind(const struct core_machine_def *d,u32 count)
{
    u32 i,j,k,index;void *memory;
    core_machine_active=0;core_machine_definitions=0;core_machine_count=0;core_machine_extended=0;
    if(!count)return 1;
    memory=fw_malloc(count*sizeof(*layouts)+71*4+16*4+8*CORE_MACHINE_SHADOW_ROW*4);
    if((u32)memory<0x40000000u)return 0;
    if(layouts)fw_free(layouts);layouts=memory;core_machine_lanes=(u32 *)(layouts+count);
    directory=(const struct core_machine_def **)(core_machine_lanes+71);
    core_machine_shadow=(u32 *)(directory+16);
    for(i=0;i<16;i++)directory[i]=0;
    for(i=0;i<count;i++)if(d[i].on_load!=(void *)CORE_SMZ_NONE && d[i].on_load(d+i))return 0;
    for(i=0;i<count;i++){
        directory[d[i].type-64]=d+i;if(d[i].params>8)core_machine_extended=1;
        for(k=0;k<4;k++){
            for(j=0;j<11;j++)layouts[i][k][j]=0;
            layouts[i][k][10]=10;
            for(j=0;j<8;j++){index=8*k+j;
                if(core_machine_ui_definitions){const struct st_machine_ui *u=core_machine_ui_definitions+i;
                    index=k<u->pages?u->layout[8*k+j]:CORE_SMZ_NONE;}
                layouts[i][k][2+j]=index<d[i].params?engine(d+i)+index:0;}
        }
    }
    for(i=0;i<54;i++)core_machine_lanes[i]=fw_lane_ids[i];
    for(i=54;i<71;i++)core_machine_lanes[i]=0x47+i-54;
    core_machine_definitions=d;core_machine_count=count;
    __asm__ volatile("" ::: "memory");core_machine_active=1;return 1;
}
u32 core_machine_storage_id(u32 track,u32 slot)
{return core_machine_active && track<12 && slot>=0x36 && slot<=0x46?slot:CORE_SMZ_NONE;}
u32 core_machine_storage_slot(u32 track,u32 id)
{
    if(!core_machine_active || track>=12)return CORE_SMZ_NONE;
    if(id>=0x36 && id<=0x46)return id;
    return id>=0x47 && id<=0x57?id-0x11:CORE_SMZ_NONE;
}
u32 *core_machine_layout(const struct core_machine_def *m,u32 page)
{
    u32 *layout;if(page>=4)return 0;
    layout=layouts[m-core_machine_definitions][page];layout[0]=fw_source_layout[0];
    layout[1]=fw_source_layout[1];return layout;
}
/* Entry wrappers pass the original stack arguments and hidden A0 result.
 * A zero result declines to the exact displaced stock entry. These are
 * machine/parameter lookups only, not a runtime hook bus for the file.
 * Host API 7: IDs no loaded machine claims go to the module providers.
 * Host API 13 (core 1.2): up to four, asked in registration order until one
 * answers; notifications (25..29) reach every one. Same args pointer. */
#define PROVIDERS 4
typedef int (*provider_t)(u32,u32 *,void *,u32 *);
provider_t core_parameter_providers[PROVIDERS];      /* emulator tests seed slot 0 */
void core_parameter_provider_set(provider_t provider)
{
    u32 i;
#pragma GCC unroll 1
    for(i=0;i<PROVIDERS;i++)if(!core_parameter_providers[i] || core_parameter_providers[i]==provider)
        {core_parameter_providers[i]=provider;return;}
}
int core_parameter_provider(u32 q,u32 *args,void *result,u32 *value)
{
    u32 i;int r;
#pragma GCC unroll 1
    for(i=0;i<PROVIDERS && core_parameter_providers[i];i++)
        if((r=core_parameter_providers[i](q,args,result,value)) && (q<25 || q>29))return r;
    return 0;
}
int core_machine_query(u32 q,u32 *args,void *result,u32 *value)
{
    const struct core_machine_def *m=0;const struct core_machine_param *p;
    u32 id=args[0],index=0,i,*layout,*copy;
    if(!core_machine_active)return 0;
    if(q==ST_AVAILABLE && id>=64 && id<80){
        core_machine_availability_seen|=1u<<(id-64);core_machine_last_track=args[1];
    }
    if(q<=ST_DIGITAL || q==ST_PAGE_COPY || q==ST_LAYOUT)m=find(id);
    else if(q==ST_SLOT_ENGINE || q==ST_PAGE_LOCAL)m=find(args[1]);
    else if(q==ST_RANK){
        if(id!=0x4060c510u)return 0;
        m=find(*(u32 *)args[1]);
    }else if(q==ST_UI_ENGINE){
        if(id<0x2e0)return 0;
        /* 1.2: UI 0x4e0.. are the modules' (ST_MODULE_UI), always providers' */
        m=id<0x4e0?find(64+(id-0x2e0)/32):0;index=(id-0x2e0)&31;
        if(m && index>=m->params)m=0;
    }else m=core_machine_parameter(q==ST_LABEL || q==ST_LONG_LABEL?args[1]:id,&index);
    if(!m)return core_parameter_provider(q,args,result,value);
    if(q==ST_NAME)core_machine_name_seen|=1u<<(m->type-64);
    if(q==ST_CATEGORY)core_machine_category_seen|=1u<<(m->type-64);
    p=m->parameter+index;
    switch(q){
    case ST_NAME:*value=(u32)m->name;break;
    case ST_SHORT:*value=(u32)m->short_name;break;
    case ST_GROUP:*value=(u32)m->group;break;
    case ST_CATEGORY:*value=m->category;break;
    case ST_AVAILABLE:*value=args[1]>=4 && args[1]<12;
        if(*value)core_machine_available_mask|=1u<<(id-64);break;
    case ST_DIGITAL:case ST_SYNTH:*value=1;break;
    case ST_UI_ENGINE:*value=engine(m)+index;break;
    case ST_LOCAL:*value=slots[index];break;
    case ST_OWNER:case ST_VALID_OWNER:*value=m->type;break;
    case ST_ENGINE_UI:*value=ui(m)+index;break;
    case ST_FLAGS:*value=0xe00;break;
    case ST_LABEL:*value=(u32)p->label;break;
    case ST_LONG_LABEL:*value=(u32)p->name;break;
    case ST_RANGE:
        if(core_machine_action_codec && core_machine_action_codec->id==id){
            core_machine_action_codec->range(result);*value=(u32)result;break;}
        copy=result;copy[0]=p->min;copy[1]=p->max;copy[2]=p->initial;*value=(u32)result;break;
    case ST_FORMAT:{const char *text=core_machine_format(id,args[1]);
        if(text){*value=(u32)text;break;}}
        /* fall through */
    case ST_CACHED:args[0]=p->alias;return 0;
    case ST_ENCODER_CACHED:
        i=core_machine_policy(id);args[0]=i==CORE_SMZ_NONE?p->alias:i;return 0;
    case ST_SLOT_ENGINE:
        /* Slot 0 is the native None destination (engine 0x13, "---"): LFO and
         * Sound Setup lists and defaults use it. A control stored there is
         * never reported for it, nor modulated (machine-params.c). */
        for(i=0;i<m->params;i++)if(id && id==slots[i]){*value=engine(m)+i;return 1;}
        if((id>=0x11 && id<0x1b) || (id>=0x36 && id<=0x46))*value=0;
        else *value=core_stock_slot_to_engine(id,m->borrow);
        break;
    case ST_PAGE_LOCAL:
        if(args[2]>12){*value=CORE_SMZ_NONE;break;}
        if(args[2]<12)args[1]=m->borrow;
        return 0;
    case ST_PAGE_COPY:
        copy=result;core_machine_copy_page(copy,m->borrow);copy[1]=engine(m);copy[2]=copy[3]=0x13;*value=(u32)copy;break;
    case ST_LAYOUT:
        layout=core_machine_action_layout(6);
        if(!layout)layout=core_machine_layout(m,0);
        *value=(u32)layout;break;
    case ST_RANK:*value=(u32)&m->rank;break;
    default:return 0;
    }
    return 1;
}
