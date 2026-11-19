/* SPDX-License-Identifier: GPL-2.0-or-later
 * Core 2.2: the AUDIO companion installs the pool service at its new place
 * (4404ff50, sample store page 8001, above AUDIO's own page) and publishes the
 * channel table through the ABI directory's fourth word, which machines read
 * (machines.h st_machine_sample_channels). The real addresses are mapped on the
 * host; the directory is a local array. No emulator or device IO.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
typedef unsigned int u32;typedef unsigned char u8;
static volatile u32 directory[4]={0x454c5001u,0,128,0};
#define AUDIO_POOL_DIRECTORY directory
#define FRAME_TAG (*(const volatile u8 *)0x800021a9u)
#define PARAM_BANK(tag) ((const volatile u32 *)(0x10000c00u+(((tag)&1)<<9)))
#define UPPER
#include "../mods/core-st/audio-machines.c"
u32 audio_machine_updates[28],audio_machine_renders[28];
u8 audio_machine_types[80];
void audio_machine_flush(void *p,u32 n){(void)p;(void)n;}
void audio_machine_silent(void){}
static void map(u32 base,u32 size)
{assert(VirtualAlloc((void *)base,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)base);}
static u32 be_adler(const u8 *p,u32 n)
{u32 a=1,b=0;while(n--){a+=*p++;if(a>=65521)a-=65521;b+=a;if(b>=65521)b-=65521;}return b<<16|a;}
static void service(u32 entry,int corrupt)
{
    u32 *h=(u32 *)POOL_BASE;
    memset(h,0,4096);
    h[0]=0x454c5031u;h[1]=4096;h[3]=entry;h[4]=0x454c5001u;h[5]=128;h[6]=POOL_BASE+4096;h[7]=0x47feff50u;
    h[10]=0x454c4801u;h[11]=POOL_CHANNELS;
    ((u8 *)POOL_CHANNELS)[5]=2;
    h[2]=be_adler((const u8 *)h+12,4096-12)^(corrupt?1u:0u);
}
int main(void)
{
    /* the bank window's first page (no bank: no SMA3) and the pool's pages 8001..8002 */
    map(0x42040000u,0x20000);map(0x44040000u,0x20000);
    assert(POOL_BASE==0x4404ff50u && POOL_TABLE==0x44050b50u && POOL_CHANNELS==0x44050ad0u);
    /* A corrupt service publishes nothing: no samples, every slot mono. */
    service(POOL_BASE+64u,1);audio_machine_install();
    assert(!audio_pool_sample(5) && !directory[3]);
    /* An entry outside the service's code is refused too. */
    service(POOL_TABLE,0);audio_machine_install();
    assert(!audio_pool_sample(5) && !directory[3]);
    /* A valid one: descriptors at +c00, the channel table published. */
    service(POOL_BASE+64u,0);audio_machine_install();
    assert(audio_pool_sample(5)==(const void *)(POOL_TABLE+40u) && !audio_pool_sample(128));
    assert(directory[3]==POOL_CHANNELS && ((const u8 *)directory[3])[5]==2);
    printf("PASS: pool service installed at 4404ff50, descriptors at 44050b50, channel table published "
           "through the ABI directory; corrupt or misplaced services publish nothing\n");
    return 0;
}
