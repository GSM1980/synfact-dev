/* SPDX-License-Identifier: GPL-2.0-or-later
 * Production core Kit lifecycle, with a provider double and native KitActive
 * mirror vectors mapped at their OS 1.42 addresses. No firmware/device IO. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <syntakt/sound-meta.h>
typedef unsigned int u32;
extern void core_kit_notify(u32,u32,u32,u32,u32);
extern void *core_meta_copy_task(void *,const void *,u32);
static u32 observed[6],extra_slot,extra_bytes;
int core_preferences(u32 key,u32 *v,u32 n,u32 write){(void)key;(void)v;(void)n;(void)write;return 0;}
int core_parameter_provider(u32 q,u32 *args,void *out,u32 *value)
{
    (void)out;
    if(q==34){memcpy(observed,args,sizeof observed);return 0;}
    if(q==37 && args[0]==extra_slot){*value=extra_bytes;return 1;}
    return 0;
}
void *core_meta_copy_stock(void *dst,const void *src,u32 n){return memcpy(dst,src,n);}
static u32 capacity(u32 kind,u32 slot,u32 result,u32 phase)
{
    u32 a=kind==ST_SOUND_KIT_ENCODE?slot:0x41ad6134u;
    u32 b=kind==ST_SOUND_KIT_ENCODE?0x41ad6134u:slot;
    core_kit_notify(kind,a,b,result,phase);
    assert(observed[0]==kind && observed[1]==a && observed[2]==b &&
           observed[3]==result && observed[4]==phase);
    return observed[5];
}
int main(void)
{
    volatile u32 *active=(volatile u32 *)0x45500130u,*vectors=(volatile u32 *)0x45000000u;
    const u32 bases[4]={0x43eee1c0u,0x442d47d0u,0x40933714u,0x45100000u};
    u32 i,k,kind,slot;
    assert(VirtualAlloc((void *)0x45500000u,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)0x45500000u);
    assert(VirtualAlloc((void *)0x444e0000u,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)0x444e0000u);
    *(volatile u32 *)0x444e23f4u=0x45500000u;*(volatile u32 *)0x45500000u=0x401c5708u;
    assert(VirtualAlloc((void *)vectors,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)vectors);
    active[0]=0x401c3694u;active[4]=0x41ad6134u;
    active[8]=(u32)vectors;active[9]=(u32)(vectors+2);vectors[0]=0;vectors[1]=bases[3];
    active[11]=(u32)(vectors+2);active[12]=(u32)(vectors+4);vectors[2]=0;vectors[3]=0x45200000u;
    for(kind=ST_SOUND_KIT_ENCODE;kind<=ST_SOUND_KIT_DECODE;kind++){
        for(i=0;i<4;i++){
            for(k=0;k<128;k++){
                slot=bases[i]+k*0x1600u;
                assert(capacity(kind,slot,1,1)==0x1600);
                assert(!capacity(kind,slot+2,1,1));
            }
            assert(!capacity(kind,bases[i]-0x1600u,1,1));
            assert(!capacity(kind,bases[i]+128u*0x1600u,1,1));
        }
        assert(capacity(kind,ST_SOUND_KIT_SAVE_BUFFER,1,1)==0x1600);
        assert(capacity(kind,ST_SOUND_KIT_RELOAD_BUFFER,1,1)==0x1600);
        assert(capacity(kind,vectors[3],1,1)==0x1600);
        assert(!capacity(kind,vectors[3]+0x1600u,1,1));  /* single-slot mirror, not an array */
        assert(!capacity(kind,vectors[3]-0x1600u,1,1));
        assert(!capacity(kind,vectors[3],0x100,1));     /* failed native codec */
        assert(!capacity(kind,vectors[3],1,0));        /* before native codec */
        assert(!capacity(kind,0x46000000u,1,1));       /* unknown/short SysEx */
    }
    /* No stale registration is retained when mirrors or KitActive change. */
    vectors[1]=0;assert(!capacity(5,bases[3],1,1));vectors[1]=bases[3];
    active[0]=0;assert(!capacity(5,bases[3],1,1));active[0]=0x401c3694u;
    active[4]=0;assert(!capacity(5,bases[3],1,1));active[4]=0x41ad6134u;
    active[9]=active[8]-4;assert(!capacity(5,bases[3],1,1));
    active[9]=active[8]+129*4;assert(!capacity(5,bases[3],1,1));
    active[9]=active[8]+3;assert(!capacity(5,bases[3],1,1));
    active[8]=1;active[9]=5;assert(!capacity(5,bases[3],1,1));
    extra_slot=0x46000000u;extra_bytes=0x1600;
    assert(capacity(5,extra_slot,1,1)==0x1600);       /* scoped adapter */
    extra_bytes=0x14d2;assert(!capacity(5,extra_slot,1,1));
    extra_slot=0;assert(!capacity(5,0x46000000u,1,1));
    {char src[8]="abcdefg",dst[8];
     assert(core_meta_copy_task(dst,src,8)==dst && !memcmp(src,dst,8));
     assert(observed[0]==ST_SOUND_COPY && observed[3]==8 && observed[4]==1 && !observed[5]);}
    puts("PASS Kit lifecycle: unchanged prefix/results/copy, fixed slots, all 128 array mirrors, single-slot mirrors, live registration, bounds, failed codecs, scoped capacity");
    return 0;
}
