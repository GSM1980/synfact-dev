/* SPDX-License-Identifier: GPL-2.0-or-later; native 32-bit host API checks. */
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../mods/core-st/machines.c"
#include "../mods/core-st/machine-pages.c"
#include "../mods/fm-tone-st/main.c"
u32 fw_source_layout[11]={1,2};const u32 fw_lane_ids[54]={0};
const struct st_machine_action_codec *core_machine_action_codec;
void core_machine_action_select(const u32 *layout){(void)layout;core_machine_action_codec=0;}
const char *core_machine_format(u32 id,u32 raw){(void)id;(void)raw;return 0;}
u32 core_machine_policy(u32 id){return id==0x1fd?0x1f1:CORE_SMZ_NONE;}
static u32 type=64,resets;
static u32 allocations;
void *fw_malloc(u32 n){return VirtualAlloc((void *)(0x48000000u+0x100000u*allocations++),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}
void fw_free(void *p){if((u32)p>=0x40000000u)assert(VirtualFree(p,0,MEM_RELEASE));else free(p);}
u32 fw_view_machine(void *v){(void)v;return type;}
void fw_reset_increments(void *v){(void)v;resets++;}
void core_machine_copy_page(void *out,u32 borrow){assert(borrow==44);memset(out,0,132);}
u32 core_stock_slot_to_engine(u32 slot,u32 borrow){assert(borrow==44);return slot;}
static void compound_range(u32 *out){out[0]=0;out[1]=720;out[2]=120;}
static u8 default_sound[0xa4];static u32 notifications,default_vt[11];
static u32 *default_model=default_vt;static int sound_missing;
static u8 *default_get(void *model)
{assert(model==&default_model);return sound_missing?0:default_sound;}
static void default_notify(void *model,void *raw)
{
    u32 i,*event=raw;assert(model==&default_model && notifications<20);
    assert(event[0]==0x401c39d8 && event[1]==core_machine_slot(8+notifications) && event[2]==0x01000000);
    /* All values are committed before the first notification. */
    for(i=8;i<28;i++)assert(*(unsigned short *)(default_sound+0x14+2*core_machine_slot(i))==fm_tone.parameter[i].initial);
    notifications++;
}
static void verify_defaults(void)
{
    u32 i,q;u8 expected[0xa4];
    assert(core_machine_bind(&fm_tone,1));
    default_vt[0x28/4]=(u32)default_get;default_vt[0x10/4]=(u32)default_notify;
    for(q=0;q<3;q++){
        memset(default_sound,0x5a,sizeof(default_sound));memcpy(expected,default_sound,sizeof(expected));
        for(i=8;i<28;i++)*(unsigned short *)(expected+0x14+2*core_machine_slot(i))=(unsigned short)fm_tone.parameter[i].initial;
        notifications=0;core_machine_defaults(65,&default_model,q==2?0x100:q);
        assert(!memcmp(default_sound,expected,sizeof(expected)) && notifications==(q==1?0:20));
    }
    /* Stock/absent types, unavailable Sound and inactive bank leave it alone. */
    notifications=0;core_machine_defaults(44,&default_model,0);core_machine_defaults(66,&default_model,0);
    core_machine_defaults(65,0,0);sound_missing=1;core_machine_defaults(65,&default_model,0);sound_missing=0;
    core_machine_active=0;core_machine_defaults(65,&default_model,0);
    assert(!notifications && !memcmp(default_sound,expected,sizeof(expected)));
}
int main(void)
{
    struct core_machine_param p[28];struct core_machine_def def[2];
    u32 i,args[3]={0},result[33],value;u8 view[0xa0]={0},sound[0xa4]={0},record[0xac]={0};
    for(i=0;i<28;i++){p[i].label="P";p[i].name="PARAM";p[i].min=0;p[i].max=0x7f00;p[i].initial=0x4000;p[i].alias=0x13;}
    def[0]=(struct core_machine_def){64,28,"TEST","T","SYN",p,ST_MACHINE_NO_INIT,44,5,9};
    def[1]=def[0];def[1].type=65;def[1].params=27;
    assert(core_machine_bind(def,2));assert(core_machine_present(64) && !core_machine_present(63));
    {u32 *types=VirtualAlloc((void *)0x47000000u,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE),vector[3];
     assert(types);types[0]=1;types[1]=64;types[2]=65;types[3]=44;
     vector[0]=(u32)types;vector[1]=vector[2]=(u32)(types+4);
     core_machine_menu_observe(vector);assert(core_machine_vector_mask==3 && core_machine_vector_count==4);
     args[0]=64;args[1]=10;assert(core_machine_query(ST_AVAILABLE,args,0,&value) && value==1);
     assert(core_machine_available_mask==1 && core_machine_last_track==10);
     args[1]=0;assert(core_machine_query(ST_AVAILABLE,args,0,&value) && !value);
     assert(VirtualFree(types,0,MEM_RELEASE));}
    assert(core_machine_engine(def+1)==0x219 && core_machine_slot(27)==0x46 && core_machine_slot(28)==CORE_SMZ_NONE);
    /* machine-params.c reads this layout directly, without per-value lookups. */
    for(i=0;i<16;i++)assert(core_machine_slot(8+i)==0x36+i);
    assert(core_machine_slot(24)==0 && core_machine_slot(25)==0x11 && core_machine_slot(26)==0x18);
    /* Slot 0 stays the native None destination even though index 24 is stored there. */
    {u32 a2[3]={0,65,0};value=0xdead;assert(core_machine_query(ST_SLOT_ENGINE,a2,0,&value) && value==0);}
    for(i=0;i<28;i++){
        args[0]=0x1f9+i;assert(core_machine_query(ST_LOCAL,args,0,&value) && value==core_machine_slot(i));
        assert(core_machine_query(ST_OWNER,args,0,&value) && value==64);
        assert(core_machine_query(ST_RANGE,args,result,&value) && result[1]==0x7f00 && result[2]==0x4000);
    }
    args[0]=0x1f9+28;assert(!core_machine_query(ST_OWNER,args,0,&value));
    {const struct st_machine_action_codec c={0x1fc,compound_range,0,0};
     core_machine_action_codec=&c;args[0]=0x1fc;
     assert(core_machine_query(ST_RANGE,args,result,&value) && result[1]==720 && result[2]==120);
     args[0]=0x1fd;assert(core_machine_query(ST_RANGE,args,result,&value) && result[1]==0x7f00);
     core_machine_action_codec=0;args[0]=0x1fc;
     assert(core_machine_query(ST_RANGE,args,result,&value) && result[1]==0x7f00);}
    args[0]=0x1fd;assert(!core_machine_query(ST_CACHED,args,0,&value) && args[0]==0x13);
    args[0]=0x1fd;assert(!core_machine_query(ST_ENCODER_CACHED,args,0,&value) && args[0]==0x1f1);
    args[0]=0x1fe;assert(!core_machine_query(ST_ENCODER_CACHED,args,0,&value) && args[0]==0x13);
    args[0]=0x219+26;assert(core_machine_query(ST_OWNER,args,0,&value) && value==65);
    args[0]=0x219+27;assert(!core_machine_query(ST_OWNER,args,0,&value));
    *(u32 *)view=0x401ca0dcu;*(u32 *)(view+0x88)=0x14;*(u32 **)(view+0x7c)=malloc(4);**(u32 **)(view+0x7c)=6;
    *(u32 **)(view+0x80)=*(u32 **)(view+0x84)=*(u32 **)(view+0x7c)+1;
    assert(!core_machine_sync(view));assert(*(u32 **)(view+0x80)-*(u32 **)(view+0x7c)==4 && resets==1);
    *(u32 *)(view+0x90)=3;assert(core_machine_nav(view,3)==0x1f9+27);
    assert(core_machine_nav(view,4)==0 && core_machine_nav(view,8)==CORE_SMZ_NONE);
    assert(!core_machine_action_begin(view));assert(core_machine_action_layout(6)[5]==0x1f9+27);
    core_machine_action_end(0);assert(!core_machine_action_layout(6));
    type=1;assert(!core_machine_sync(view));assert(*(u32 **)(view+0x80)-*(u32 **)(view+0x7c)==1);
    fw_free(*(void **)(view+0x7c));sound[0xa2]=64;
    for(i=0;i<32;i++)sound[0x80+i]=(u8)(i+1);sound[0xa0]=77;sound[0xa1]=88;
    core_machine_sound_saved(record,sound);memset(sound,0x11,0xa2);core_machine_sound_loaded(sound,record);
    for(i=0;i<32;i++)assert(sound[0x80+i]==i+1);assert(sound[0xa0]==77 && sound[0xa1]==88);
    assert(core_machine_lanes[54]==0x47 && core_machine_lanes[70]==0x57);
    assert(core_machine_storage_slot(0,0x57)==0x46 && core_machine_storage_id(0,0x46)==0x46);
    core_machine_active=0;assert(!core_machine_query(ST_NAME,args,0,&value));
    assert(core_machine_storage_slot(0,0x57)==CORE_SMZ_NONE);
    *(u32 **)(view+0x7c)=0;assert(!core_machine_sync(view));
    verify_defaults();
    puts("PASS: registry/pages/storage and confirmed-machine defaults: 20 FM controls, neutral offsets, atomic values before notifications, quiet/stock/inactive/null fallbacks");return 0;
}

unsigned int core_native(unsigned int op,const unsigned int *a){(void)op;(void)a;return op==10?0xffffffffu:0;}
unsigned int core_native_view(unsigned int op,void *v,unsigned int b){(void)v;(void)b;return op==10?0xffffffffu:0;}

unsigned int fw_parameter_selecting(void *model){(void)model;return 0;}
