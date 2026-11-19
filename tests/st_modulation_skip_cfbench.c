/* Independent unsigned-16 stock zero-depth projection and ColdFire ABI/EMAC. */
typedef unsigned int u32;typedef unsigned short u16;
extern u32 st_probe_call(u32,u32,u32,u32,u32,u32);
extern void st_probe_mac_get(u32 *),st_probe_mac_set(const u32 *);
static volatile u16 cells[314];static u32 addresses[312];
u32 check_projection(u32 fn)
{
    u32 base,i,n,mac[8]={0xa0,0x12345678,0x1b000001,7,3,2,1,0},a[8],b[8];
    cells[0]=0xface;cells[313]=0xbeef;
    for(base=0;base<65536;base+=312){
        n=65536-base;if(n>312)n=312;
        for(i=0;i<312;i++){cells[i+1]=(u16)(base+i);addresses[i]=(u32)&cells[i+1];}
        st_probe_mac_set(mac);st_probe_mac_get(a);
        if(st_probe_call(fn,(u32)addresses,n,0,0,0))return 0x10000000u+base;
        st_probe_mac_get(b);
        for(i=0;i<8;i++)if(a[i]!=b[i])return 0x20000000u+i;
        for(i=0;i<312;i++){
            u32 input=(base+i)&65535u;
            u32 expected=i>=n?input:input<0x8000u?input:input<0xc000u?0x7fffu:0u;
            if(cells[i+1]!=expected)return 1+base+i;
        }
        if(cells[0]!=0xface || cells[313]!=0xbeef)return 0x30000000u;
    }
    /* Empty and duplicate lists, including exact boundary values. */
    cells[1]=0x8000;addresses[0]=addresses[1]=(u32)&cells[1];
    if(st_probe_call(fn,(u32)addresses,0,0,0,0) || cells[1]!=0x8000)return 0x40000000u;
    if(st_probe_call(fn,(u32)addresses,2,0,0,0) || cells[1]!=0x7fff)return 0x40000001u;
    return 0;
}
