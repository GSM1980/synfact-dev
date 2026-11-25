/* SPDX-License-Identifier: GPL-2.0-or-later
 * Task-only kit lifecycle, shared parameter-provider notifications (API 16).
 * Callback 34 receives {kind,a,b,n,phase}; phase 0 before codec, 1 after.
 * Query 35 may supply a synchronous task memcpy entry (same C ABI), allowing
 * LFO3's established owning-object copy path to stay intact. */
#include <syntakt/sound-meta.h>
#include <syntakt/persist.h>
typedef unsigned int u32;
extern int core_parameter_provider(u32,u32 *,void *,u32 *);
extern void *core_meta_copy_stock(void *,const void *,u32);
/* KitActive's +20h mirror vector contains bases of 128 encoded Kit slots;
 * +2Ch holds individual slots of the selected Kit. Neither is a project
 * container pointer. updateSingleMirror (4001082e) adds k*1600h, whereas
 * 400109ba uses the individual slot unchanged (jobs 4000ea98/4000eac8).
 * Resolve these on each notification: no borrowed heap pointer is retained. */
static const volatile u32 *kit_active(void)
{
    u32 app=*(volatile u32 *)0x444e23f4u;
    const volatile u32 *active;
    if(app<0x40000000u || app>0x47fff000u || (app&3u) || *(volatile u32 *)app!=0x401c5708u)return 0;
    active=(const volatile u32 *)(app+0x130u);
    return active[0]==0x401c3694u && active[4]==0x41ad6134u?active:0;
}
static int vector(const volatile u32 *active,u32 index,u32 *begin,u32 *end)
{
    *begin=active[index];*end=active[index+1];
    return *begin>=0x40000000u && *end<=0x48000000u && !(*begin&3u) && *end>=*begin &&
           !((*end-*begin)&3u) && *end-*begin<=128u*4u;
}
/* 4001b336 registers container+2e0200h through 401728b0. Unlike the
 * individual-slot vector, every array registration carries a full project. */
static u32 kit_context(u32 slot,u32 *container)
{
    const volatile u32 *active=kit_active();
    const u32 bases[3]={0x43c0dfc0u,0x43ff45d0u,0x40653514u};
    u32 i,j,base,begin,end,off;
    *container=0;
    if(slot==ST_SOUND_KIT_SAVE_BUFFER || slot==ST_SOUND_KIT_RELOAD_BUFFER)return 0x1600;
    for(i=0;i<3;i++){
        base=bases[i]+0x2e0200u;off=slot-base;
        if(slot>=base && off<128u*0x1600u && !(off%0x1600u)){*container=bases[i];return 0x1600;}
    }
    if(!active)return 0;
    for(i=0x20/4;i<=0x2c/4;i+=3){
        if(!vector(active,i,&begin,&end))continue;
        for(j=begin;j<end;j+=4){
            base=*(const volatile u32 *)j;off=slot-base;
            if(base && (i==0x2c/4?slot==base:
               slot>=base && off<128u*0x1600u && !(off%0x1600u))){
                if(i==0x20/4 && base>=0x402e0200u && base<=0x47c00000u)*container=base-0x2e0200u;
                return 0x1600;
            }
        }
    }
    return 0;
}
static u32 kit_slot(u32 slot){u32 c;return kit_context(slot,&c);}
static u32 container_of(u32 slot){u32 c;kit_context(slot,&c);return c;}
static u32 kit_container(u32 k)
{
    const volatile u32 *active=kit_active();u32 begin,end,base;
    if(k>=128 || !active || !vector(active,8,&begin,&end))return 0;
    while(begin<end){base=*(volatile u32 *)begin;begin+=4;
        if(base>=0x402e0200u && base<=0x47c00000u)return base-0x2e0200u;}
    return 0;
}
static int kit_changed(u32 k)
{
    const volatile u32 *active=kit_active();
    if(k>=128 || !active || ((const u32 *)0x401c3694u)[0x5c/4]!=0x40172414u)return 0;
    ((void (*)(const volatile void *,u32,u32))0x40172414u)(active,k,0);return 1;
}
extern int core_preferences(u32,u32 *,u32,u32);
const struct st_persistence_api core_persistence_api={1,kit_slot,container_of,kit_container,kit_changed,core_preferences};
void core_kit_notify(u32 kind,u32 a,u32 b,u32 n,u32 phase)
{
    u32 bytes=phase && (n&255u) && (kind==ST_SOUND_KIT_ENCODE || kind==ST_SOUND_KIT_DECODE)?
              kit_slot(kind==ST_SOUND_KIT_ENCODE?a:b):0;
    /* An established project-codec adapter may also know a temporary full
     * container during serialization. It lends capacity only for this call. */
    if(!bytes && phase && (n&255u) && (kind==ST_SOUND_KIT_ENCODE || kind==ST_SOUND_KIT_DECODE)){
        u32 slot=kind==ST_SOUND_KIT_ENCODE?a:b;
        core_parameter_provider(37,&slot,0,&bytes);if(bytes!=0x1600u)bytes=0;
    }
    u32 args[6]={kind,a,b,n,phase,bytes};core_parameter_provider(34,args,0,0);
}
void *core_meta_copy_task(void *dst,const void *src,u32 n)
{
    u32 a[3]={(u32)dst,(u32)src,n},fn=0;void *result;
    core_parameter_provider(35,a,0,&fn);
    result=fn?((void *(*)(void *,const void *,u32))fn)(dst,src,n):core_meta_copy_stock(dst,src,n);
    core_kit_notify(ST_SOUND_COPY,(u32)dst,(u32)src,n,1);return result;
}
