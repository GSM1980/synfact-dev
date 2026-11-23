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
static u32 allocations,fail_allocation;
static u32 codec_notifications,chain_calls[2],fx_notes[2][4];
/* Host API 13: two providers. A query stops at the first answer; every
 * notification (25..29, FX codec 28/29) reaches both; query 30 stops at an answer. */
static int chain(u32 k,u32 q,u32 *a,u32 *value)
{
    chain_calls[k]++;
    if(q==30)return k==0;
    if(q>=28){fx_notes[k][0]=q;fx_notes[k][1]=a[0];fx_notes[k][2]=a[1];fx_notes[k][3]=a[2];return 1;}
    if(q==18 && a[1]==0x23d+k){*value=100+k;return 1;}
    return 0;
}
static int chain_a(u32 q,u32 *a,void *r,u32 *v){(void)r;return chain(0,q,a,v);}
static int chain_b(u32 q,u32 *a,void *r,u32 *v){(void)r;return chain(1,q,a,v);}
/* Core 2.6.1: a bank's providers in registration order (SY MnM's selector,
 * POLY, Chorus, COMP, then LFO3 last) and up to sixteen, one per module.
 * Each answers query 18 for its own ID only. */
static u32 slot_calls[16];
static int slot_query(u32 k,u32 q,u32 *a,u32 *v)
{slot_calls[k]++;if(q==18 && a[1]==0x3f9+k){*v=200+k;return 1;}return 0;}
#define SLOT(n) static int slot_##n(u32 q,u32 *a,void *r,u32 *v){(void)r;return slot_query(n,q,a,v);}
SLOT(0) SLOT(1) SLOT(2) SLOT(3) SLOT(4) SLOT(5) SLOT(6) SLOT(7)
SLOT(8) SLOT(9) SLOT(10) SLOT(11) SLOT(12) SLOT(13) SLOT(14) SLOT(15)
static int (*const provider_fns[16])(u32,u32 *,void *,u32 *)={slot_0,slot_1,slot_2,slot_3,slot_4,slot_5,
    slot_6,slot_7,slot_8,slot_9,slot_10,slot_11,slot_12,slot_13,slot_14,slot_15};
static void provider_slots(void)
{
    u32 i,q[3]={0,0,0},value;
    memset(core_parameter_providers,0,sizeof core_parameter_providers);
    for(i=0;i<16;i++)core_parameter_provider_set(provider_fns[i]);
    for(i=0;i<16;i++)core_parameter_provider_set(provider_fns[i]);    /* re-registration keeps one slot */
    for(i=0;i<16;i++){
        q[1]=0x3f9+i;value=0;
        assert(core_parameter_provider(18,q,0,&value)==1 && value==200+i);
    }
    memset(slot_calls,0,sizeof slot_calls);q[1]=0x300;
    assert(!core_parameter_provider(18,q,0,&value));
    for(i=0;i<16;i++)assert(slot_calls[i]==1);
    memset(core_parameter_providers,0,sizeof core_parameter_providers);
}
static int codec_final(u32 q,u32 *a,void *result,u32 *value)
{
    u8 *record=(u8 *)a[q==25?0:1],*sound=(u8 *)a[q==25?1:0];u32 i;
    (void)result;(void)value;assert(q==25 || q==26);assert(a[2]==1);
    for(i=0;i<32;i++)assert(record[0x88+i]==sound[0x80+i]);
    assert(record[0xa8]==sound[0xa0] && record[0xa9]==sound[0xa1]);
    assert(record[0x1c]==sound[0x14] && record[0x1d]==sound[0x15]);
    codec_notifications++;return 0;
}
void *fw_malloc(u32 n)
{if(fail_allocation)return 0;
 return VirtualAlloc((void *)(0x48000000u+0x100000u*allocations++),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}
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
    assert(event[0]==0x401c3c78 && event[1]==core_machine_slot(8+notifications) && event[2]==0x01000000);
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
/* LFO3 is a parameter provider, not a machine. Model its metadata contract
 * without native descriptor addresses; exercise the real bind/query path. */
static u32 module_calls;
static int module_metadata(u32 q,u32 *a,void *result,u32 *value)
{
    u32 id=q==ST_LABEL || q==ST_LONG_LABEL?a[1]:a[0],i=id-0x3f9u;
    module_calls++;
    if(q==ST_UI_ENGINE){
        if(id-0x4e0u>=10)return 0;
        *value=id-0x4e0u+0x3f9u;return 1;
    }
    if(i>=10)return 0;
    switch(q){
    case ST_LABEL:case ST_LONG_LABEL:*value=(u32)"LFO3 TEST";return 1;
    case ST_ENGINE_UI:*value=0x4e0u+i;return 1;
    case ST_RANGE:{u32 *r=result;r[0]=0;r[1]=0x7f00;r[2]=0x4000;*value=(u32)r;return 1;}
    case ST_CACHED:case ST_FORMAT:case ST_ENCODER_CACHED:a[0]=0x60u+i;return 0;
    default:return 0;
    }
}
static int other_module(u32 q,u32 *a,void *r,u32 *v)
{(void)q;(void)a;(void)r;(void)v;return 0;}
static void verify_module_only(void)
{
    u32 i,j,q,args[3]={0,0x3f9,0},range[3],value;
    /* No bank: providers must remain unreachable through the query gate. */
    assert(!core_machine_active && !allocations);
    core_parameter_provider_set(module_metadata);
    assert(!core_machine_query(ST_LABEL,args,0,&value) && !module_calls);
    for(j=0;j<2;j++){
        assert(core_machine_bind(0,0));
        assert(core_machine_active && !core_machine_count && !core_machine_extended);
        assert(core_machine_lanes && core_machine_shadow);
        if(j)core_parameter_provider_set(other_module); /* LFO3 + ARP composition. */
        for(i=0;i<10;i++){
            args[0]=0x3f9+i;args[1]=0x3f9+i;
            assert(core_machine_query(ST_LABEL,args,0,&value) && !strcmp((char *)value,"LFO3 TEST"));
            assert(core_machine_query(ST_LONG_LABEL,args,0,&value) && !strcmp((char *)value,"LFO3 TEST"));
            assert(core_machine_query(ST_RANGE,args,range,&value) && value==(u32)range);
            assert(range[0]==0 && range[1]==0x7f00 && range[2]==0x4000);
            assert(core_machine_query(ST_ENGINE_UI,args,0,&value) && value==0x4e0+i);
            args[0]=0x4e0+i;assert(core_machine_query(ST_UI_ENGINE,args,0,&value) && value==0x3f9+i);
            for(q=ST_CACHED;q<=ST_ENCODER_CACHED;q++){
                if(q!=ST_CACHED && q!=ST_FORMAT && q!=ST_ENCODER_CACHED)continue;
                args[0]=0x3f9+i;assert(!core_machine_query(q,args,0,&value) && args[0]==0x60+i);
            }
        }
        for(i=64;i<80;i++)assert(!core_machine_present(i));
        for(i=0;i<71;i++)assert(core_machine_lanes[i]==(i<54?fw_lane_ids[i]:0x47+i-54));
        for(i=0;i<0x1f9;i++){
            args[1]=i;assert(!core_machine_query(ST_LABEL,args,0,&value));
        }
        args[0]=64;args[1]=4;assert(!core_machine_query(ST_AVAILABLE,args,0,&value));
    }
    /* Empty-bank allocation failure must fail closed, including after an
     * earlier successful bind; a subsequent successful bind can recover. */
    fail_allocation=1;assert(!core_machine_bind(0,0));
    assert(!core_machine_active && !core_machine_count);
    i=module_calls;args[1]=0x3f9;
    assert(!core_machine_query(ST_LABEL,args,0,&value) && module_calls==i);
    fail_allocation=0;assert(core_machine_bind(0,0) && core_machine_active);
    assert(core_machine_query(ST_LABEL,args,0,&value));
    core_parameter_providers[0]=core_parameter_providers[1]=0;
    puts("PASS: module-only registry, LFO3 metadata with/without second provider, native fallbacks, empty directory, allocation failure/recovery");
}
int main(void)
{
    struct core_machine_param p[28];struct core_machine_def def[2];
    u32 i,args[3]={0},result[33],value;u8 view[0xa0]={0},sound[0xa4]={0},record[0xac]={0};
    verify_module_only();
    provider_slots();
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
    *(u32 *)view=0x401ca37cu;*(u32 *)(view+0x88)=0x14;*(u32 **)(view+0x7c)=malloc(4);**(u32 **)(view+0x7c)=6;
    *(u32 **)(view+0x80)=*(u32 **)(view+0x84)=*(u32 **)(view+0x7c)+1;
    assert(!core_machine_sync(view));assert(*(u32 **)(view+0x80)-*(u32 **)(view+0x7c)==4 && resets==1);
    *(u32 *)(view+0x90)=3;assert(core_machine_nav(view,3)==0x1f9+27);
    assert(core_machine_nav(view,4)==0 && core_machine_nav(view,8)==CORE_SMZ_NONE);
    assert(!core_machine_action_begin(view));assert(core_machine_action_layout(6)[5]==0x1f9+27);
    core_machine_action_end(0);assert(!core_machine_action_layout(6));
    type=1;assert(!core_machine_sync(view));assert(*(u32 **)(view+0x80)-*(u32 **)(view+0x7c)==1);
    fw_free(*(void **)(view+0x7c));sound[0xa2]=64;
    for(i=0;i<32;i++)sound[0x80+i]=(u8)(i+1);sound[0xa0]=77;sound[0xa1]=88;
    sound[0x14]=0x12;sound[0x15]=0x34;
    core_parameter_provider_set(codec_final);
    core_machine_sound_saved(record,sound);memset(sound,0x11,0xa2);core_machine_sound_loaded(sound,record);
    assert(codec_notifications==2);core_parameter_providers[0]=0;
    {u32 q[3]={0,0x23e,0};
     core_parameter_provider_set(chain_a);core_parameter_provider_set(chain_b);core_parameter_provider_set(chain_a);
     assert(core_parameter_providers[0]==chain_a && core_parameter_providers[1]==chain_b && !core_parameter_providers[2]);
     assert(core_parameter_provider(18,q,0,&value) && value==101 && chain_calls[0]==1 && chain_calls[1]==1);
     q[1]=0x23d;assert(core_parameter_provider(18,q,0,&value) && value==100 && chain_calls[0]==2 && chain_calls[1]==1);
     q[1]=0x300;assert(!core_parameter_provider(18,q,0,&value) && chain_calls[0]==3 && chain_calls[1]==2);
     core_fx_codec_notify(28,0x1000,0x2000,1);
     assert(fx_notes[0][0]==28 && fx_notes[1][0]==28 && fx_notes[1][1]==0x1000 && fx_notes[1][2]==0x2000 && fx_notes[1][3]==1);
     core_fx_codec_notify(29,0x2000,0x1000,0);
     assert(fx_notes[0][0]==29 && fx_notes[1][0]==29 && fx_notes[0][1]==0x2000 && fx_notes[0][3]==0);
     {u32 a=chain_calls[0],b=chain_calls[1];
      assert(core_parameter_provider(30,q,0,0)==1 && chain_calls[0]==a+1 && chain_calls[1]==b);}
     core_parameter_providers[0]=core_parameter_providers[1]=0;}
    for(i=0;i<32;i++)assert(sound[0x80+i]==i+1);assert(sound[0xa0]==77 && sound[0xa1]==88);
    assert(sound[0x14]==0x12 && sound[0x15]==0x34);
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
