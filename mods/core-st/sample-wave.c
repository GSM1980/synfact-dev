/* SPDX-License-Identifier: GPL-2.0-or-later
 * Generic min/max envelope cache. Data stays in owned native heap, not the
 * 64 KiB code reserve. Draw only borrows cached data. Stock PCM is scanned in
 * bounded idle slices; long PCM is accumulated while the loader already reads it.
 */
#include "sample-pool.h"
#include <syntakt/machines.h>
typedef pool_u32 u32;typedef pool_u8 u8;
extern void *fw_malloc(u32);
static struct st_machine_wave *waves;
static u32 work=128,position,bin,end;
static struct st_machine_wave *find(u32 code)
{u32 i;if(waves)for(i=0;i<128;i++)if(waves[i].code==code)return waves+i;return 0;}
void core_pool_wave_begin(u32 code,u32 frames)
{
    u32 i,j;struct st_machine_wave *w=find(code);const u8 *pcm;u32 n;
    work=128;if(!code || !frames)return;
    if(!waves){waves=fw_malloc(128*sizeof(*waves));if((u32)waves<0x40000000u){waves=0;return;}
        for(i=0;i<128;i++)waves[i].code=0;}
    if(!w)for(i=0;i<128;i++){
        if(!waves[i].code){w=waves+i;break;}
        for(j=0;j<128;j++)if(core_pool_sample_source(j,&pcm,&n)==waves[i].code)break;
        if(j==128){w=waves+i;break;}
    }
    if(!w)return;w->code=code;w->frames=frames;w->ready=0;
    for(i=0;i<96;i++){w->lo[i]=32767;w->hi[i]=-32768;}
    work=(u32)(w-waves);position=bin=0;end=frames/96;
}
void core_pool_wave_feed(const u8 *pcm,u32 bytes)
{
    u32 i;int v;struct st_machine_wave *w;if(work>=128)return;w=waves+work;
    for(i=0;i+1<bytes && position<w->frames;i+=2,position++){
        v=(short)((u32)pcm[i]<<8|pcm[i+1]);
        while(position>=end && bin<95){bin++;end=(w->frames*(bin+1))/96;}
        if(v<w->lo[bin])w->lo[bin]=(short)v;if(v>w->hi[bin])w->hi[bin]=(short)v;
    }
}
void core_pool_wave_finish(u32 ok)
{if(work<128)waves[work].ready=ok && position==waves[work].frames?2:0;work=128;}
const struct st_machine_wave *core_pool_wave(u32 slot)
{const u8 *pcm;u32 frames,code=core_pool_sample_source(slot,&pcm,&frames);return code?find(code):0;}
static void overview(struct st_machine_wave *w,const u8 *pcm,u32 frames)
{
    u32 i,at,bin,count=frames<2048?frames:2048,whole=frames/count,remainder=frames%count;
    int v;
    /* A bounded, evenly spaced pass makes the whole stock waveform visible on
     * the first UI visit. The normal sequential scan refines its exact peaks
     * on later visits without a large single-frame CPU spike. */
    for(i=0;i<count;i++){
        at=whole*i+(remainder*i)/count;bin=(at*96u)/frames;
        v=(short)((u32)pcm[2*at]<<8|pcm[2*at+1]);
        if(v<w->lo[bin])w->lo[bin]=(short)v;
        if(v>w->hi[bin])w->hi[bin]=(short)v;
    }
    w->ready=1;
}
void core_pool_wave_idle(u32 slot,u32 budget)
{
    const u8 *pcm;u32 frames,code,n;struct st_machine_wave *w;
    core_pool_cursor_tick();code=core_pool_sample_source(slot,&pcm,&frames);
    if(!code || !pcm || !frames || !budget)return;w=find(code);
    if(w && w->ready==2 && w->frames==frames)return;
    if(work>=128 || waves[work].code!=code || waves[work].frames!=frames)core_pool_wave_begin(code,frames);
    if(work>=128)return;w=waves+work;
    if(!position && !w->ready)overview(w,pcm,frames);
    n=frames-position;if(n>budget)n=budget;
    core_pool_wave_feed(pcm+2*position,2*n);if(position==frames)core_pool_wave_finish(1);
}
