/* SPDX-License-Identifier: GPL-2.0-or-later
 * Core 2.8: what a machine reads of a wavetable slot (machines.h
 * st_machine_sample_wavetable) and the layout helpers (wavetable.h). The ABI
 * directory at 40040f50 is mapped on the host, as AUDIO has it. No emulator
 * or device IO.
 */
#include <assert.h>
#include <stdio.h>
#include <windows.h>
#include <syntakt/machines.h>
static struct st_pool_sample slots[128];
static unsigned char kinds[128];
static const struct st_pool_sample *sample(st_u32 slot){return slots[slot].frames?slots+slot:0;}
static st_u32 published[5],calls;
static void cursor(st_u32 t,st_u32 p,st_u32 f,st_u32 pos,st_u32 a){published[0]=t;published[1]=p;published[2]=f;published[3]=pos;published[4]=a;calls++;}
int main(void)
{
    volatile st_u32 *api=(volatile st_u32 *)0x40040f50u;st_u32 k,w;
    assert(VirtualAlloc((void *)0x40040000u,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)0x40040000u);
    /* No pool service yet: nothing is a wavetable. */
    assert(!st_machine_sample_wavetable(3));
    api[0]=0x454c5001u;api[1]=(st_u32)sample;api[2]=128;api[3]=0;
    slots[3].frames=ST_WT_FRAMES*64;kinds[3]=ST_POOL_KIND_WAVETABLE;
    assert(!st_machine_sample_wavetable(3));            /* the channel table is not published */
    api[3]=(st_u32)kinds;
    assert(st_machine_sample_wavetable(3)==64 && st_machine_sample_channels(3)==1);
    kinds[3]=1;assert(!st_machine_sample_wavetable(3));  /* a plain mono slot */
    kinds[3]=2;assert(!st_machine_sample_wavetable(3) && st_machine_sample_channels(3)==2);
    kinds[3]=ST_POOL_KIND_WAVETABLE;slots[3].frames=ST_WT_FRAMES*65;assert(!st_machine_sample_wavetable(3));
    slots[3].frames=ST_WT_FRAMES-1;assert(!st_machine_sample_wavetable(3));
    slots[3].frames=0;assert(!st_machine_sample_wavetable(3));  /* empty */
    assert(!st_machine_sample_wavetable(128));
    /* The layout: levels follow each other, each one W waves of 2048 >> k. */
    for(w=1;w<=ST_WT_MAX;w++){
        st_u32 at=0;for(k=0;k<ST_WT_LEVELS;k++){
            assert(st_wt_offset(w,k,0)==at && st_wt_offset(w,k,w-1)==at+(w-1)*(ST_WT_WAVE>>k));
            at+=w*(ST_WT_WAVE>>k);
        }
        assert(at==w*ST_WT_FRAMES && st_wt_waves(ST_HZ_WAVETABLE,at)==w);
    }
    assert(!st_wt_waves(1u|ST_HZ_WAVETABLE,ST_WT_FRAMES) && !st_wt_waves(0,ST_WT_FRAMES));
    /* The cursor record through the directory: the service is the channel
     * table - b80h, with {454c4801, table} at +40 and {454c4301, fn} at +32. */
    {static st_u32 service[0xb80/4+32];unsigned char *table=(unsigned char *)(service+0xb80/4);
     api[3]=(st_u32)table;st_machine_cursor_publish(9,1,2,3,4);assert(!calls);        /* no records */
     service[10]=0x454c4801u;service[11]=(st_u32)table;service[8]=0x454c4301u;service[9]=(st_u32)cursor;
     st_machine_cursor_publish(9,1,2,3,4);assert(calls==1 && published[0]==9 && published[3]==3);
     service[11]=0;st_machine_cursor_publish(9,1,2,3,4);assert(calls==1);              /* a mismatched table */
     api[3]=(st_u32)kinds;}
    assert(VirtualFree((void *)0x40040000u,0,MEM_RELEASE));
    puts("PASS: wavetable slots through the ABI directory (kind 0x81, 1 to 64 waves of 4088) the level-major layout and the cursor record found through the directory");
    return 0;
}
