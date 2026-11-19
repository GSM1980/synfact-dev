/* SPDX-License-Identifier: GPL-2.0-or-later
 * Generic min/max envelope cache. Data stays in owned native heap, not the
 * 64 KiB code reserve. Boot never computes envelopes. UI idle reads bounded
 * disk slices on demand; runtime HZFS imports may feed the same cache.
 * A stereo sample (interleaved L, R) counts frame pairs, as the AUDIO
 * descriptor does: lo/hi hold L's peaks and lo_r/hi_r R's (UI API 8).
 */
#include "sample-pool.h"
#include "crc.h"
#include <syntakt/machines.h>
typedef pool_u32 u32;typedef pool_u8 u8;
extern void *fw_malloc(u32);
extern void fw_free(void *);
extern int fw_mmc_read(u32,u32,void *);
static struct st_machine_wave *waves;
static u32 work=128,position,bin,end,wave_channels=1,half;
static u32 lazy_crc;
static u32 wave_rd(const u8 *p){return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static struct st_machine_wave *find(u32 code)
{u32 i;if(waves)for(i=0;i<128;i++)if(waves[i].code==code)return waves+i;return 0;}
void core_pool_wave_begin(u32 code,u32 frames,u32 count)
{
    u32 i;struct st_machine_wave *w=find(code);
    work=128;if(!code || !frames)return;
    if(!waves){waves=fw_malloc(128*sizeof(*waves));if((u32)waves<0x40000000u){waves=0;return;}
        for(i=0;i<128;i++)waves[i].code=0;}
    /* A free entry first; else one whose sample no pool slot names. "Names"
     * is the slot's code, set at boot before any PCM arrives: its frames are
     * published only once the whole boot load is done, so testing them let
     * each long sample loaded at boot evict the previous one's wave. */
    for(i=0;!w && i<128;i++)if(!waves[i].code)w=waves+i;
    for(i=0;!w && i<128;i++)if(!core_pool_code_used(waves[i].code))w=waves+i;
    if(!w)return;w->code=code;w->frames=frames;w->ready=0;w->channels=count==2?2:1;
    for(i=0;i<96;i++){w->lo[i]=w->lo_r[i]=32767;w->hi[i]=w->hi_r[i]=-32768;}
    work=(u32)(w-waves);position=bin=half=0;end=frames/96;wave_channels=count==2?2:1;
}
void core_pool_wave_feed(const u8 *pcm,u32 bytes)
{
    u32 i;int v;struct st_machine_wave *w;short *lo,*hi;if(work>=128)return;w=waves+work;
    for(i=0;i+1<bytes && position<w->frames;i+=2){
        v=(short)((u32)pcm[i]<<8|pcm[i+1]);
        while(position>=end && bin<95){bin++;end=(w->frames*(bin+1))/96;}
        lo=half?w->lo_r:w->lo;hi=half?w->hi_r:w->hi;
        if(v<lo[bin])lo[bin]=(short)v;if(v>hi[bin])hi[bin]=(short)v;
        if(++half>=wave_channels){half=0;position++;}
    }
}
void core_pool_wave_finish(u32 ok)
{if(work<128)waves[work].ready=ok && position==waves[work].frames?2:0;work=128;}
const struct st_machine_wave *core_pool_wave(u32 slot)
{const u8 *pcm;u32 frames,code=core_pool_sample_source(slot,&pcm,&frames);return code?find(code):0;}
void core_pool_wave_idle(u32 slot,u32 budget)
{
    u32 source[5],check[5],n,at,skip,physical,i,index,progress;u8 *buffer;struct st_machine_wave *w;
    core_pool_cursor_tick();if(!budget || !core_pool_wave_storage(slot,source))return;
    w=find(source[0]);if(w && w->ready==2 && w->frames==source[1] && w->channels==source[2])return;
    if(work>=128 || waves[work].code!=source[0] || waves[work].frames!=source[1])
        core_pool_wave_begin(source[0],source[1],source[2]);
    if(work>=128)return;
    index=work;progress=position;
    n=source[1]-position;if(n>budget)n=budget;
    if(n>2048/source[2])n=2048/source[2];n*=2*source[2];
    at=64+position*2*source[2];skip=at&511u;physical=(skip+n+511)&~511u;
    buffer=fw_malloc(5120);if((u32)buffer<0x40000000u)return;
    if(fw_mmc_read(source[3]+(at>>9),physical,buffer) ||
       !core_pool_wave_storage(slot,check))goto failed;
    for(i=0;i<5;i++)if(check[i]!=source[i])goto failed;
    if(work!=index || position!=progress)goto failed;
    if(!position){
        if(wave_rd(buffer)!=0x53414d50u || wave_rd(buffer+8)!=source[1]*source[2])goto failed;
        lazy_crc=core_crc_update(0,buffer,64);
    }
    if(source[0]&CORE_POOL_HZ)lazy_crc=core_crc_update(lazy_crc,buffer+skip,n);
    core_pool_wave_feed(buffer+skip,n);fw_free(buffer);
    if(position==source[1])core_pool_wave_finish(!(source[0]&CORE_POOL_HZ) || ~lazy_crc==source[4]);
    return;
failed:fw_free(buffer);if(!core_pool_busy && work==index && position==progress)core_pool_wave_finish(0);
}
