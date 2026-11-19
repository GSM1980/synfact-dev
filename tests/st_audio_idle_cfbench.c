/* Test-only standalone ColdFire driver. The image/ROM is supplied locally;
 * native output instructions execute on analog-emu's physical EMAC model. */
#include <syntakt/host.h>
typedef unsigned int u32;typedef int s32;
#define R(a) (*(volatile u32 *)(a))
#define CFG ((const volatile u32 *)0x405f0000u)
#define V ((const volatile s32 *)0x405f0100u)
#define WORK 0x40620000u
extern u32 st_probe_call(u32,u32,u32,u32,u32,u32);
extern void st_probe_mac_get(u32 *);
extern void recorder_idle_mix(u32,u32,void *);
static u32 mode,clock_frames,fallback_seen;
static int ready(void){return 1;}
static const struct st_audio_idle_owner owner={ready,recorder_idle_mix};
static u32 reference_tx[256];
static u32 reference_mac[8];
void recorder_audio(void)
{
    u32 i;clock_frames+=32;
    if(mode&3u)for(i=0;i<64;i++)((volatile s32 *)0x8000dfd0u)[i]=V[i];
    if(mode&8u)R(0x8000ded0u)=65536; /* negative control: unwanted ADC leak */
}
static void (*const preview_list[])(void)={recorder_audio,0};
u32 st_idle_load(void)
{
    u32 i;for(i=0;i<CFG[3];i+=4)R(CFG[1]+i)=0;
    for(i=0;i<CFG[2];i+=4)R(CFG[1]+i)=R(CFG[0]+i);
    return 0;
}
static void setup(void)
{
    u32 i;
    for(i=0;i<65536;i+=4)R(0x80000000u+i)=0;
    for(i=0;i<4096;i+=4){R(0x40610000u+i)=0;R(WORK+i)=0;}
    R(0x800030b8u)=0x40610000u;R(0x43bd73bcu)=2;
    R(0x43bdc4d8u)=R(0x4553f6dcu)=R(0x43bd7684u)=R(0x43bd7688u)=0;
    R(0x43bdbf64u)=R(0x43bdbf68u)=0xc0000000u;
    *(volatile unsigned char *)0x8000313du=2;
    *(volatile unsigned char *)0x8000313cu=1;
    *(const struct st_audio_idle_owner *volatile *)CFG[5]=&owner;
    R(CFG[10]+2)=(u32)preview_list;
    clock_frames=0;
}
u32 st_idle_check(u32 which)
{
    u32 i;
    /* Independent native reference: execute the WHOLE original mixer with
     * the same preview event, not a hand-written replica of its output tail.
     * Native codec words need not be numerically zero in this startup fixture. */
    mode=which&~8u;setup();R(CFG[6])=0;
    if(st_probe_call(0x4008f1cau,0,0,WORK,0,0))return 0x10000000u;
    for(i=0;i<256;i++)reference_tx[i]=R(0x80000000u+4*i);
    st_probe_mac_get(reference_mac);
    mode=which;setup();
    if(!((u32 (*)(void))CFG[4])())return 0x10000001u;
    for(i=0;i<384;i++)R(0x8000dad0u+4*i)=0x12345678u;
    if(st_probe_call(CFG[7],0,0,WORK,0,0))return 0x10000002u;
    {u32 mac[8];st_probe_mac_get(mac);
     for(i=0;i<8;i++)if(mac[i]!=reference_mac[i])return 0x50000000u+i;}
    if(clock_frames!=32)return 0x10000003u;
    for(i=0;i<64;i++) {
        long long x=(mode&3u)?(long long)V[i]*8:0;
        s32 expected=x>2147483647ll?2147483647:x<-2147483648ll?(-2147483647-1):(s32)x;
        s32 actual=(s32)R(0x80004760u+80*(i/2)+4*(i&1));
        if(actual!=expected)return 0x20000000u+i;
    }
    for(i=0;i<256;i++)if(R(0x8000dad0u+4*i))return 0x30000000u+i;
    for(i=0;i<256;i++)if(R(0x80000000u+4*i)!=reference_tx[i])return 0x30000100u+i;
    /* Real core modulation/LFO adapters must return without entering their
     * native bodies or subscribers, retaining the complete C ABI. */
    if(st_probe_call(CFG[8],0,0,0,0,0))return 0x40000001u;
    if(st_probe_call(CFG[9],0,0,0,0,0))return 0x40000002u;
    R(0x43bd73bcu)=1;
    if(((u32 (*)(void))CFG[4])() || R(CFG[6]))return 0x40000003u;
    R(0x43bd73bcu)=2;
    *(const struct st_audio_idle_owner *volatile *)CFG[5]=0;
    if(((u32 (*)(void))CFG[4])() || R(CFG[6]))return 0x40000004u;
    return 0;
}
/* Used only in the fallback fixture: its generated image replaces the old
 * mixer entry with this argument observer, rather than inventing native FX
 * heap state. Active preview fixtures keep the native image unchanged. */
u32 st_idle_fallback_probe(u32 a,u32 b,u32 c)
{fallback_seen=a==0x1234u && b==0x5678u && c==WORK;return 0;}
u32 st_idle_fallback(void)
{
    setup();*(const struct st_audio_idle_owner *volatile *)CFG[5]=0;
    if(((u32 (*)(void))CFG[4])())return 1;
    if(st_probe_call(CFG[7],0x1234,0x5678,WORK,0,0))return 2;
    return fallback_seen?0:3;
}
u32 st_idle_prepare(u32 which,u32 active)
{
    mode=which;setup();R(CFG[6])=active;return 0;
}
