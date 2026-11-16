/* Real bank callback driver; input fixtures only, no native routines or IO. */
typedef unsigned int u32;typedef int s32;
#define R(a) (*(volatile u32 *)(a))
#define CFG ((const volatile u32 *)0x405f0000u)
#define FIELD(s,k) CFG[2+25*(s)+(k)]
enum {CLOCK,ENABLED,SOURCE,STATE,POST,MON,PEAK,STEREO,TAKE_STEREO,FRAMES,
      TAKE_POST,PCM,TAKE_SOURCE,LIMIT,TRIGGER,PCM_PEAK,RET_CLOCK,RET,LAST_SOURCE,
      REPORT,PEAK_R,PLAY_LENGTH,VIEW,MANAGER,PLAY_POS};
extern void st_probe_mac_get(u32 *),st_probe_mac_set(const u32 *);
extern u32 st_probe_call(u32,u32,u32,u32,u32,u32);
static u32 mac_before[8],mac_after[8];
static u32 rng=1;
static u32 random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static s32 edge(u32 i)
{
    static const u32 values[]={0,1,0xffffffffu,0x7fffffffu,0x80000000u,
        32767,32768,0xffff8000u,0xffff7fffu,0x7fff8000u,0x80008000u};
    return i<sizeof(values)/sizeof(values[0])?(s32)values[i]:(s32)random32();
}
u32 st_capture_setup(u32 side,u32 scene)
{
    u32 i,options=scene;scene&=3u;
    for(i=0;i<0x20000;i+=4)R(0x80000000u+i)=0;
    for(i=0;i<25;i++)R(FIELD(side,i))=0;
    R(FIELD(side,ENABLED))=1;R(FIELD(side,SOURCE))=R(FIELD(side,TAKE_SOURCE))=5;
    R(FIELD(side,STATE))=(options&64u)?1:2;
    R(FIELD(side,STEREO))=R(FIELD(side,TAKE_STEREO))=(options&256u)?0:1;
    R(FIELD(side,TRIGGER))=(options&128u)?0xffffffffu:0;
    R(FIELD(side,POST))=R(FIELD(side,TAKE_POST))=scene==3?0:1;
    R(FIELD(side,PCM))=side?0x41200000u:0x41000000u;R(FIELD(side,LIMIT))=(options&32u)?47:480000;
    R(FIELD(side,VIEW))=0x40610000u;R(FIELD(side,MANAGER))=0x40610200u;
    *(volatile unsigned char *)0x40610030u=0;
    R(0x4061021cu)=0x40610400u;R(0x40610408u)=0x40610000u;
    for(i=0;i<128;i++)R(FIELD(side,RET)+4*i)=0;
    if(scene) {
        R(0x8000e1a8u)=R(0x8000e158u)=0xc0000000u;
        R(0x8000e1acu)=R(0x8000e15cu)=0xe0000000u;
        R(0x8000e180u)=R(0x8000e130u)=0xd0000000u;
        R(0x8000e184u)=R(0x8000e134u)=0xf0000000u;
    }
    for(i=0;i<32;i++) {
        R(0x80004760u+80*i)=((u32)((s32)i-16)*1000u<<16)+(i*12347u&65535u);
        R(0x80004764u+80*i)=((u32)(15-(s32)i)*700u<<16)+(i*733u&65535u);
    }
    if(scene!=2)for(i=0;i<64;i++) {
        R(0x8000dcd0u+4*i)=(u32)((s32)(i%13)-6)*1024u<<16;
        R(0x8000ddd0u+4*i)=(u32)((s32)(i%7)-3)*2048u<<16;
    }
    st_probe_mac_set((const u32 *)0x40600000u);
    return 0;
}
u32 st_capture_compare(u32 scene,u32 blocks)
{
    u32 s,i,k,fields[]={CLOCK,ENABLED,STATE,FRAMES,PCM_PEAK,RET_CLOCK,LAST_SOURCE,PEAK,PEAK_R};
    for(s=0;s<2;s++) {
        st_capture_setup(s,scene);st_probe_mac_get(mac_before);rng=0x673423u;
        for(i=0;i<blocks;i++) {
            if(scene&16u) {
                for(k=0;k<32;k++) {
                    R(0x80004760u+80*k)=edge(i+k);R(0x80004764u+80*k)=edge(i+k+1);
                    R(0x8000dcd0u+8*k)=edge(i+k+2);R(0x8000dcd4u+8*k)=edge(i+k+3);
                    R(0x8000ddd0u+8*k)=edge(i+k+4);R(0x8000ddd4u+8*k)=edge(i+k+5);
                }
                for(k=0;k<2;k++) {
                    u32 zero=(i/8)%3==0;
                    R(0x8000e1a8u+4*k)=R(0x8000e158u+4*k)=zero?0:random32();
                    R(0x8000e180u+4*k)=R(0x8000e130u+4*k)=zero?0:random32();
                }
            }
            ((void (*)(void))CFG[s])();
        }
        st_probe_mac_get(mac_after);
        for(i=0;i<8;i++)if(mac_before[i]!=mac_after[i])return 0x10000000u+8*s+i;
    }
    if(scene&8u)(*(volatile short *)0x41200000u)++;
    for(i=0;i<2*32*blocks;i++)
        if(((const volatile short *)0x41000000u)[i]!=((const volatile short *)0x41200000u)[i])return 0x20000000u+i;
    for(k=0;k<sizeof(fields)/sizeof(fields[0]);k++)
        if(R(FIELD(0,fields[k]))!=R(FIELD(1,fields[k])))return 0x30000000u+fields[k];
    for(i=0;i<128;i++)if(R(FIELD(0,RET)+4*i)!=R(FIELD(1,RET)+4*i))return 0x40000000u+i;
    return 0;
}

/* Independent integer oracle exercises the linked assembly, including odd
 * products, rounded +32768, clipping, PRE/POST and arbitrary physical EMAC. */
u32 st_capture_kernels(u32 cases,u32 negative)
{
    s32 ret[64],expected[64],g[4],d,r,c,v;short pcm[64];u32 peaks[2],p[2],i,k,mode,flags,m,actual;
    u32 state[8];rng=0xe8127501u;
    for(i=0;i<cases;i++) {
        for(k=0;k<8;k++)state[k]=random32();state[0]&=0xfffu;
        st_probe_mac_set(state);st_probe_mac_get(mac_before);
        for(k=0;k<4;k++)g[k]=(s32)(random32()%65537u)-32768;
        if(i<16)for(k=0;k<4;k++)g[k]=(i&(1u<<k))?32768:-32768;
        mode=i&1;
        for(k=0;k<64;k++) {
            d=edge(i+k);r=edge(i+k+3);R(0x8000dcd0u+4*k)=(u32)d;R(0x8000ddd0u+4*k)=(u32)r;
            d=((d>>15)+1)>>1;r=((r>>15)+1)>>1;
            c=-(((g[2*(k&1)]*d)>>1)+((g[2*(k&1)+1]*r)>>1));
            expected[k]=mode?(c>>13)*5:c>>11;
        }
        ((void (*)(s32 *,s32,s32,s32,s32,u32))CFG[52])(ret,g[0],g[1],g[2],g[3],mode);
        st_probe_mac_get(mac_after);
        for(k=0;k<8;k++)if(mac_before[k]!=mac_after[k])return 0x51000000u+i*8+k;
        if(negative && !i)ret[0]^=1;
        for(k=0;k<64;k++)if(ret[k]!=expected[k])return 0x52000000u+i*64+k;
        flags=i%3;p[0]=p[1]=0;
        for(k=0;k<64;k++) {
            v=edge(i+k);R(0x80004760u+80*(k/2)+4*(k&1))=(u32)v;
            ret[k]=flags==2?0:(s32)(random32()%1310721u)-655360;
            v=(v>>16)+(flags==1?ret[k]:-ret[k]);
            if(v>32767)v=32767;if(v< -32768)v=-32768;
            expected[k]=v;m=v<0?0u-(u32)v:(u32)v;if(m>p[k&1])p[k&1]=m;
        }
        actual=((u32 (*)(short *,s32 *,u32,u32 *))CFG[53])(pcm,ret,flags,peaks);
        if(actual!=(p[0]>p[1]?p[0]:p[1]) || peaks[0]!=p[0] || peaks[1]!=p[1])return 0x53000000u+i;
        for(k=0;k<64;k++)if(pcm[k]!=expected[k])return 0x54000000u+i*64+k;
        if(st_probe_call(CFG[53],(u32)pcm,(u32)ret,flags,(u32)peaks,0))return 0x55000000u+i;
        st_probe_mac_get(mac_after);
        for(k=0;k<8;k++)if(mac_before[k]!=mac_after[k])return 0x56000000u+i*8+k;
    }
    return 0;
}
