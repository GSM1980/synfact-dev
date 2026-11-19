/* OS 1.42 real mixer/output differential on the physical EMAC model. */
typedef unsigned int u32;
#define R(a) (*(volatile u32 *)(a))
#define CFG ((const volatile u32 *)0x405f0000u)
#define WORK 0x40620000u
extern u32 st_probe_call(u32,u32,u32,u32,u32,u32);
extern void st_probe_mac_get(u32 *);
static u32 reference_tx[256],reference_mac[8];
static const u32 empty_list[1]={0};
u32 project_load(void)
{
    u32 i;for(i=0;i<CFG[3];i+=4)R(CFG[1]+i)=0;
    for(i=0;i<CFG[2];i+=4)R(CFG[1]+i)=R(CFG[0]+i);
    return 0;
}
static void setup(void)
{
    u32 i;for(i=0;i<65536;i+=4)R(0x80000000u+i)=0;
    for(i=0;i<4096;i+=4){R(0x40610000u+i)=0;R(WORK+i)=0;}
    R(0x800030b8u)=0x40610000u;R(0x43bd83bcu)=2;
    R(0x43bdd4d8u)=R(0x455406dcu)=R(0x43bd8684u)=R(0x43bd8688u)=0;
    R(0x43bdcf64u)=R(0x43bdcf68u)=0xc0000000u;
    *(volatile unsigned char *)0x8000313du=2;
    *(volatile unsigned char *)0x8000313cu=1;
    R(CFG[5])=0;R(CFG[6])=0;R(CFG[11])=0;
    R(CFG[10]+2)=(u32)empty_list;
}
u32 project_check(u32 which)
{
    u32 i;setup();
    if(st_probe_call(0x4008f242u,0,0,WORK,0,0))return 1;
    for(i=0;i<256;i++)reference_tx[i]=R(0x80000000u+4*i);
    st_probe_mac_get(reference_mac);
    setup();R(CFG[11])=1;
    if(((u32 (*)(void))CFG[4])()!=2)return 2;
    for(i=0;i<384;i++)R(0x8000dad0u+4*i)=0x12345678u;
    if(which==1)R(CFG[11])=0; /* completion after render-in */
    if(st_probe_call(CFG[7],0,0,WORK,0,0))return 3;
    {u32 mac[8];st_probe_mac_get(mac);
     for(i=0;i<8;i++)if(mac[i]!=reference_mac[i])return 0x50000000u+i;}
    for(i=0;i<64;i++)if(R(0x80004760u+80*(i/2)+4*(i&1)))return 0x20000000u+i;
    for(i=0;i<384;i++)if(R(0x8000dad0u+4*i))return 0x30000000u+i;
    for(i=0;i<256;i++)if(R(0x80000000u+4*i)!=reference_tx[i])return 0x40000000u+i;
    if(st_probe_call(CFG[8],0,0,0,0,0))return 4;
    if(st_probe_call(CFG[9],0,0,0,0,0))return 5;
    R(CFG[11])=0;
    if(((u32 (*)(void))CFG[4])())return 6;
    return 0;
}
u32 project_prepare(u32 active)
{setup();R(CFG[11])=active;((u32 (*)(void))CFG[4])();return 0;}
