/* SPDX-License-Identifier: GPL-2.0-or-later
 * Core AUDIO sample service, compiled at 42100790 and transported by the core.
 * Its code/table are outside the SMZ. No disk IO, allocation or waits in AUDIO.
 * BEGIN verifies a free, unpublished extent; DATA is sequential and checksummed
 * before copying; COMMIT publishes one complete descriptor after all PCM arrives.
 * Core 1.8: COMMIT and ALIAS carry the channel count in bit 8 of the slot
 * (set: stereo, interleaved L, R). The descriptor's frames count frame pairs;
 * CHANNELS holds 1 or 2 per slot (machines.h st_machine_sample_channels).
 */
typedef unsigned int u32;typedef unsigned short u16;
#define LO 0x42101750u
#define HI 0x4404f750u
#ifndef TABLE
#define TABLE ((volatile u32 *)0x42101350u)
#endif
#ifndef WIN
#define WIN ((volatile u32 *)0x800u)
#endif
#ifndef DOORBELL
#define DOORBELL (*(volatile u32 *)0x40040050u)
#endif
#ifndef CHANNELS
#define CHANNELS ((volatile unsigned char *)0x421012d0u)
#endif
#ifndef STOCK_TABLE
#define STOCK_TABLE ((const u32 *)0x4404f754u)
#endif
#define MAGIC 0x45505331u
static u32 last,answer,start,extent,received;
static u32 cursors[32];
void audio_pool_cursor(u32 track,u32 pointer,u32 frames,u32 position,u32 active)
{
    u32 *r;if(track>=8)return;r=cursors+4*track;
    r[0]=pointer;r[1]=frames;r[2]=position<frames?position:frames;r[3]=active!=0;
}
static int pcm_span(u32 p,u32 n){return p>=LO && p<HI && n && !(p&3) && n<=HI-p;}
static int stock(u32 p,u32 frames)
{
    const u32 *s=STOCK_TABLE;u32 i;
    for(i=0;i<64;i++)if(s[2*i]==frames && s[2*i+1]==p)return 1;return 0;
}
static u32 slot_channels(u32 i){return CHANNELS[i]==2?2:1;}
static int busy(u32 p,u32 n)
{
    u32 i,f,q;for(i=0;i<128;i++){
        f=TABLE[2*i];q=TABLE[2*i+1];if(!f || q<LO || q>=HI)continue;
        if(p<q+2*slot_channels(i)*f && q<p+n)return 1;
    }return 0;
}
static void ack(u32 seq,u32 status)
{WIN[6]=(seq<<16)|status;DOORBELL=1;}
void audio_pool_receive(void)
{
    u32 seq,cmd,dst,len,sum,i,p,frames,n,ch;
    if(WIN[0]!=MAGIC)return;
    seq=WIN[1]>>16;cmd=WIN[1]&65535u;if(!seq)return;
    if(seq==last){ack(seq,answer);return;}
    dst=WIN[2];len=WIN[3];sum=MAGIC+WIN[1]+dst+len;
    n=cmd==2?len:cmd==5?4:0;
    if(n>992 || (n&3))goto reject;
    for(i=0;i<n/4;i++)sum+=WIN[8+i];
    if(sum!=WIN[4])return; /* incomplete publication: do not acknowledge */
    if(cmd==1){
        if(!pcm_span(dst,len) || (len&3) || busy(dst,len))goto reject;
        start=dst;extent=len;received=0;
    }else if(cmd==2){
        if(!start || !len || len>extent-received || dst!=start+received)goto reject;
        for(i=0;i<len/4;i++)((u32 *)dst)[i]=WIN[8+i];
        received+=len;
    }else if(cmd==3){
        frames=len;ch=dst&0x100u?2:1;dst&=~0x100u;
        if(dst>=128 || !frames || frames>(HI-LO)/(2*ch) || !start ||
           ((2*ch*frames+3)&~3u)!=extent || received!=extent)goto reject;
        TABLE[2*dst]=0;CHANNELS[dst]=(unsigned char)ch;TABLE[2*dst+1]=start;TABLE[2*dst]=frames;
        start=extent=received=0;
    }else if(cmd==4){
        if(dst>=128 || len)goto reject;
        TABLE[2*dst]=0;TABLE[2*dst+1]=0;CHANNELS[dst]=1;
    }else if(cmd==5){
        p=WIN[8];frames=len;ch=dst&0x100u?2:1;dst&=~0x100u;
        if(dst>=128 || !frames || frames>0x7fffffu)goto reject;
        if(ch==2 || !stock(p,frames)){
            if(!pcm_span(p,2*ch*frames))goto reject;
            for(i=0;i<128;i++)if(TABLE[2*i]==frames && TABLE[2*i+1]==p && slot_channels(i)==ch)break;
            if(i==128)goto reject;
        }
        TABLE[2*dst]=0;CHANNELS[dst]=(unsigned char)ch;TABLE[2*dst+1]=p;TABLE[2*dst]=frames;
    }else if(cmd==6){
        if(dst || len)goto reject;sum=seq;
        for(i=0;i<32;i++){WIN[8+i]=cursors[i];sum+=cursors[i];}WIN[7]=sum;
    }else goto reject;
    last=seq;answer=1;ack(seq,1);return;
reject:last=seq;answer=2;ack(seq,2);
}
