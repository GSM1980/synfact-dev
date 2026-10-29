/* SPDX-License-Identifier: GPL-2.0-or-later
 * CPU2 boot-time registration. The native parser of the 64 sample descriptors
 * runs first. Registration precedes native hardware/voice initialization.
 */
typedef unsigned int u32;typedef unsigned char u8;
#define BASE 0x4204f750u
#define LIMIT (0x42100000u-BASE)
#ifndef FRAME_TAG
#define FRAME_TAG (*(const volatile u8 *)0x800081a9u)
#define PARAM_BANK(tag) ((const volatile u32 *)(0xc00u+(((tag)&1)<<9)))
#endif
extern void audio_machine_flush(void *,u32);
extern void audio_machine_silent(void);
extern u32 audio_machine_updates[],audio_machine_renders[];
extern u8 audio_machine_types[];
/* File-owned 8x24 u16 state follows the descriptor table. The checksum is
 * verified once before this state becomes mutable. A row is offered to its
 * machine only after one was accepted for the type now on that track: an
 * unreceived row is no default (FM ratio offsets 0 silence the carrier). */
static u32 *extended;
static u32 pool_ready,accepted;
static u8 row_type[8];
/* Entries reached through the fixed ABI directory live in the upper
 * reserve (sdk/syntakt/audio.py UPPER); the main reserve keeps the rest. */
#ifndef UPPER
#define UPPER __attribute__((section(".text.upper")))
#endif
UPPER const void *audio_pool_sample(u32 slot)
{return pool_ready && slot<128?(const void *)(0x42101350u+8*slot):0;}
static void pool_install(void)
{
    const u32 *h=(const u32 *)0x42100750u;const u8 *p=(const u8 *)h+12;
    u32 a=1,b=0,i;
    if(h[0]!=0x454c5031u || h[1]!=4096 || (h[3]&1) || h[3]<0x42100790u || h[3]>=0x42101350u)return;
    for(i=12;i<4096;i++){a+=*p++;if(a>=65521)a-=65521;b+=a;if(b>=65521)b-=65521;}
    if((b<<16|a)!=h[2])return;
    audio_machine_flush((void *)h,4096);pool_ready=h[3];
}
UPPER const unsigned short *audio_machine_params(const void *params)
{
    u32 at=(u32)params-0x80003c5cu,t=at/0x8eu;
    /* +0x22 is this voice's machine type in the received native frame. */
    if(!extended || t>=8 || at!=t*0x8e || row_type[t]!=((const u8 *)params)[0x22])return 0;
    return (const unsigned short *)extended+24*t;
}
/* The frame tag names MAIN's newest complete generation. Unchanged: no FPGA
 * access. A lagging frame may find its bank already being rewritten; the
 * other bank then holds the following generation. Every row is rechecked
 * against the header after its words: rewrites invalidate the header first. */
void audio_machine_receive(void)
{
    u32 tag=FRAME_TAG,t,j,type,sum,words[12],n,mask;
    const volatile u32 *bank,*r;
    if(pool_ready && *(const volatile u32 *)0x800u==0x45505331u)
        ((void (*)(void))pool_ready)();
    if(!extended || !tag || tag==accepted)return;
    bank=PARAM_BANK(tag);
    if(bank[0]!=0x4d503333u || bank[1]!=tag){
        tag=tag%254+1;bank=PARAM_BANK(tag);
        if(tag==accepted || bank[0]!=0x4d503333u || bank[1]!=tag)return;
    }
    mask=bank[2];
    for(t=0;t<8;t++){
        if(mask&(1u<<t)){
            r=bank+4+15*t;type=r[0];n=type>>16;sum=t+type;type&=65535u;
            if(n && n<=24 && type>=64 && type<80 && audio_machine_types[type]>=12){
                n=(n+1)/2;for(j=0;j<n;j++){words[j]=r[1+j];sum+=words[j];}
                /* Torn or corrupted: keep the previous row, retry later. */
                if(sum!=r[13] || bank[1]!=tag || bank[0]!=0x4d503333u)return;
                for(j=0;j<n;j++)extended[12*t+j]=words[j];
                row_type[t]=type;continue;
            }
        }
        row_type[t]=0;
    }
    if(bank[1]==tag)accepted=tag;
}
void audio_machine_install(void)
{
    const u32 *header=(const u32 *)BASE,*entry=header+4;
    const u8 *data=(const u8 *)(BASE+16);u32 n,size,i,j,seen=0,t,fn,a=1,b=0;
    pool_install();
    if(header[0]!=0x534d4133u)return;
    n=header[1];size=header[2];
    if(!n || n>16 || size>LIMIT || size<=16+16*n+384 || (size&3))return;
    for(i=16;i<size;i++){
        a+=*data++;if(a>=65521u)a-=65521u;b+=a;if(b>=65521u)b-=65521u;
    }
    if((b<<16|a)!=header[3])return;
    /* Validate the WHOLE bank before publishing the first backend. */
    for(i=0;i<n;i++){
        t=entry[4*i];if(t<64 || t>79 || entry[4*i+3] || (seen&(1u<<(t-64))))return;
        seen|=1u<<(t-64);
        for(j=1;j<=2;j++){
            fn=entry[4*i+j];if((fn&1) || fn<BASE+16+16*n+384 || fn>BASE+size-2)return;
        }
    }
    audio_machine_flush((void *)BASE,size);
    extended=(u32 *)(BASE+16+16*n);
    for(i=0;i<96;i++)extended[i]=0;
    for(i=0;i<8;i++)row_type[i]=0;
    accepted=0;
    for(i=0;i<n;i++){
        t=entry[4*i];j=12+t-64;
        audio_machine_updates[j]=entry[4*i+1];audio_machine_renders[j]=entry[4*i+2];
        audio_machine_types[t]=j;
    }
}
