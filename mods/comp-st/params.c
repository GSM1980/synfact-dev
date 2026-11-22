/* SPDX-License-Identifier: GPL-2.0-or-later
 * Seven controls in the encoded Kit's audited 16-byte gap 15d0..15e0,
 * between LFO3 and Playhead: six full-Q8 words; the SCF position (0..127)
 * shares RAT's word, whose low byte RAT never uses. Values live in
 * bank-owned tables at runtime. No native Sound, FX, Chorus or Playhead
 * field is repurposed. */
#include "comp.h"
#include "config-tables.h"
/* SCF: OFF, then 127 cutoffs log-spaced from 60 to 1000 Hz; one-pole
 * coefficient a = 1 - exp(-2*pi*fc/48000) in Q31 (generated, task-side). */
#include "scf-tables.h"
const u16 *const comp_scf_hz_table=comp_scf_hz;
#define ALL 127u
#define CONFIG_WORDS (sizeof(struct comp_config)/4)
struct row {u32 valid;u16 value[COMP_COUNT];struct comp_config config;};
static struct row live[128] DATA={{0,{0}}};
static struct {u32 address,hash;struct row r;} side[24] DATA={{0,0,{0,{0}}}};
static u32 next DATA=0;
static struct comp_config default_config DATA={0};
extern const u32 comp_k_expt[17];
static u32 frac_positive(u32 a,u32 b)
{
    u32 lo=(a&65535u)*(b&65535u);
    u32 cross=(a>>16)*(b&65535u)+(a&65535u)*(b>>16)+(lo>>16);
    return 2u*(a>>16)*(b>>16)+(cross>>15);
}
static u32 idle_gain(s32 makeup)
{
    u32 v=frac_positive(356689313u,(u32)-makeup),i=(v>>19)&15u;
    u32 f=(v&0x7ffffu)<<12,d=comp_k_expt[i]-comp_k_expt[i+1];
    u32 lo=d*(f&65535u),hi=d*(f>>16)+(lo>>16);
    u32 ceil=(hi>>15)+!!((hi&32767u)|(lo&65535u));
    return (comp_k_expt[i]-ceil)>>(v>>23);
}
static void prepare(const u16 *v,struct comp_config *c,u32 mask)
{
    u32 a=v[1]>>8,r=v[2]>>8,m=v[3];
    if(mask&1u){c->threshold=(s32)((u32)v[0]*15360u)-503316480;
        c->idle_peak=comp_idle_peak[v[0]>>8];}
    /* Exact (MUP_K * raw)>>15 using two bounded 32-bit products. */
    if(mask&8u)c->makeup=(s32)(m*(202017810u>>15)+((m*(202017810u&32767u))>>15))-202017810;
    if(mask&16u)c->slope=(s32)comp_ratio[v[4]>>8];
    if(mask&2u)c->attack32=(s32)comp_attack32[a];
    if(mask&4u)c->release32=(s32)comp_release32[r];
    if(mask&6u)c->cross=(s32)comp_cross[(a<<7)|r];
    if(mask&32u){c->wet=((u32)v[5]<<15)/0x7f00u;c->dry=4096u-(c->wet>>3);}
    if(mask&40u)c->idle_gain=((idle_gain(c->makeup)*c->wet)>>15)+c->dry;
    if(mask&64u)c->hpa=comp_scf_coefficient[v[COMP_SCF]>>8];
}
static u32 kit_hash(u32 kit)
{const volatile u8 *p=(const volatile u8 *)kit;u32 h=0x811c9dc5u,n=COMP_KIT_SIZE;while(n--)h=(h^*p++)*0x01000193u;return h;}
static u32 index_of(u32 kit){u32 off=kit-COMP_KIT_BASE;return kit>=COMP_KIT_BASE && off<128u*COMP_KIT_SIZE && !(off%COMP_KIT_SIZE)?off/COMP_KIT_SIZE:128;}
static struct row *row_of(u32 kit,u32 make)
{
    u32 k=index_of(kit),i;if(k<128)return live+k;
    if(!kit)return 0;
    for(i=0;i<24;i++)if(side[i].address==kit){
        if(!make && side[i].hash!=kit_hash(kit)){side[i].r.valid=0;return 0;}
        if(make)side[i].hash=kit_hash(kit);return &side[i].r;
    }
    if(!make)return 0;
    i=next++%24;side[i].address=kit;side[i].hash=kit_hash(kit);side[i].r.valid=0;return &side[i].r;
}
static void read_values(u32 kit,u16 *out)
{struct row *r=row_of(kit,0);u32 i;for(i=0;i<COMP_COUNT;i++)out[i]=r && r->valid?r->value[i]:comp_initial[i];}
static void store_mask(u32 kit,const u16 *v,u32 mask)
{
    struct row *r=row_of(kit,1);struct comp_config c;u32 i,sr;if(!r)return;
    if(mask!=ALL)c=r->valid?r->config:default_config;
    prepare(v,&c,mask);
    sr=comp_lock();r->valid=0;for(i=0;i<COMP_COUNT;i++)r->value[i]=v[i];
    for(i=0;i<CONFIG_WORDS;i++)((u32 *)&r->config)[i]=((const u32 *)&c)[i];
    if(kit==comp_hot->kit && index_of(kit)<128){
        u32 was_off=!comp_hot->config.hpa;
        for(i=0;i<CONFIG_WORDS;i++)((u32 *)&comp_hot->config)[i]=((const u32 *)&c)[i];
        /* Turning SCF or COMP on starts the detector filter from rest, not
         * from a stale state. Moving between cutoffs keeps it: a one-pole
         * filter takes a new coefficient smoothly while the knob turns. */
        if((mask&32u) || ((mask&64u) && was_off)){comp_hot->lp[0]=0;comp_hot->lp[1]=0;}
    }
    __asm__ volatile("" ::: "memory");r->valid=1;comp_unlock(sr);
}
static void store(u32 kit,const u16 *v){store_mask(kit,v,ALL);}
const struct comp_config *comp_current_config(u32 kit)
{u32 k=index_of(kit);return k<128?&live[k].config:&default_config;}
const u16 *comp_current_values(void)
{
    /* Live kits are a direct indexed lookup; no per-block hash or scan. */
    u32 kit=*(volatile u32 *)0x800030b8u,k=index_of(kit);
    return k<128 && live[k].valid?live[k].value:comp_initial;
}
static u32 current(void){return *(volatile u32 *)0x800030b8u;}
s32 comp_param_get(u32 id)
{u16 v[COMP_COUNT];if(id-COMP_ID>=COMP_COUNT)return 0;read_values(current(),v);return v[id-COMP_ID];}
s32 comp_param_set(u32 id,s32 value)
{
    u16 v[COMP_COUNT];u32 k=index_of(current()),i=id-COMP_ID;const volatile u32 *active=(const volatile u32 *)0x40a713f0u;
    if(i>=COMP_COUNT || k>=128)return -1;
    if(value<0)value=0;if((u32)value>comp_maximum[i])value=comp_maximum[i];
    if(i==4 || i==COMP_SCF)value&=~255;
    read_values(current(),v);if(v[i]==(u16)value)return 0;
    v[i]=(u16)value;store_mask(current(),v,1u<<i);
    /* Re-encode the same native mirrors that survive MRAM power cycles. */
    if(active[0]==0x401c3694u && active[4]==COMP_KIT_BASE &&
       ((const u32 *)0x401c3694u)[0x5c/4]==0x40172414u)
        ((void (*)(const volatile void *,u32,u32))0x40172414u)(active,k,0);
    {u8 *root=*(u8 *volatile *)0x444e2334u;if(root)root[96]=1;}
    return 0;
}
static u16 check(const u8 *p)
{u32 h=0x4350u,i;for(i=0;i<14;i++)h=(h*33u)^p[i];return (u16)(h^(h>>16));}
static u32 known_slot(u32 p)
{
    const u32 bases[3]={0x43c0dfc0u,0x43ff45d0u,0x40653514u};u32 i,off;
    if(p==0x40600554u || p==0x405fe354u)return 1;
    for(i=0;i<3;i++){off=p-(bases[i]+0x2e0200u);if(p>=bases[i]+0x2e0200u && off<128u*0x1600u && !(off%0x1600u))return 1;}
    return 0;
}
void comp_meta(const u32 *a)
{
    u16 v[COMP_COUNT],w;u8 *r;u32 i;if(!a[4])return;
    switch(a[0]){
    case 1:if(a[3]==COMP_KIT_SIZE){read_values(a[2],v);store(a[1],v);}break;
    case 2:if(a[3]==COMP_KIT_SIZE)store(a[1],comp_initial);break;
    case 5:
        if(!(a[3]&255u) || !known_slot(a[1]))break;
        r=(u8 *)(a[1]+COMP_RECORD);read_values(a[2],v);
        *(u16 *)r=0x4351u;for(i=0;i<6;i++)((u16 *)r)[i+1]=v[i];
        ((u16 *)r)[5]=(u16)(v[4]|v[COMP_SCF]>>8);
        ((u16 *)r)[7]=check(r);break;
    case 6:
        if(!(a[3]&255u))break;
        r=(u8 *)(a[2]+COMP_RECORD);
        if(!known_slot(a[2]) || *(u16 *)r!=0x4351u || ((u16 *)r)[7]!=check(r)){store(a[1],comp_initial);break;}
        /* RAT's word: RAT in the high byte, SCF in the low byte (0.2.0
         * wrote zero there: OFF). */
        w=((u16 *)r)[5];v[COMP_SCF]=(u16)((w&255u)<<8);
        for(i=0;i<6;i++){v[i]=i==4?(u16)(w&0xff00u):((u16 *)r)[i+1];if(v[i]>comp_maximum[i])break;}
        store(a[1],i==6 && v[COMP_SCF]<=comp_maximum[COMP_SCF]?v:comp_initial);break;
    }
}
void comp_params_init(void)
{u32 i,j;prepare(comp_initial,&default_config,ALL);
 for(i=0;i<128;i++){live[i].valid=0;for(j=0;j<CONFIG_WORDS;j++)((u32 *)&live[i].config)[j]=((u32 *)&default_config)[j];}
 for(i=0;i<24;i++)side[i].address=0;next=0;}
