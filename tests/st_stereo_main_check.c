/* SPDX-License-Identifier: GPL-2.0-or-later
 * Target-side check of the STEREO module's EMAC mix (mods/samplers-st/stereo/
 * main.c, samplers-st 1.4), run in analog-emu's cfbench
 * (tests/test_st_stereo_natives_emu.py). Built with the bank's compiler and
 * flags; never in a bank.
 *
 * add() must give, per frame, bus minus each track's g x D / 2^31 (D = 2h,
 * h = mantissa << shift, g ramping from the value it found), the tracks
 * summed four at a time in the 48-bit accumulator and saturated to 32 bits
 * at the end of each pass (MOVCLR with OMC), within the truncation of eight
 * products; leave each track's ramp
 * start at its gain; and leave the interrupted code's MACSR, ACC0 and
 * ACCext01 as they were. Returns (bad frames << 16) | (bad EMAC states).
 */
#include "../mods/samplers-st/stereo/main.c"
static u32 seed;
static u32 rng(void){seed=seed*1664525u+1013904223u;return seed;}
static struct block fake;
static s32 bus[64],before[64],gains[8];
/* (a x b) >> 30, floor, from 16-bit limbs (no 64-bit helpers here) */
static long long product30(s32 a,s32 b)
{
    u32 ua=a<0?0u-(u32)a:(u32)a,ub=b<0?0u-(u32)b:(u32)b,neg=(a<0)!=(b<0);
    u32 al=ua&0xffffu,ah=ua>>16,bl=ub&0xffffu,bh=ub>>16;
    unsigned long long r=((unsigned long long)(ah*bh)<<32)+((unsigned long long)(ah*bl)<<16)
                        +((unsigned long long)(al*bh)<<16)+(unsigned long long)(al*bl);
    long long v=neg?-(long long)r:(long long)r;
    return v>>30;
}
static void set_state(u32 macsr,u32 acc,u32 ext)
{
    __asm__ volatile("move.l #0,%%macsr\n\tmove.l %0,%%acc0\n\tmove.l %1,%%accext01\n\tmove.l %2,%%macsr"
                     ::"d"(acc),"d"(ext),"d"(macsr):"cc");
}
static void get_state(u32 *macsr,u32 *acc,u32 *ext)
{
    u32 m,a,e;
    __asm__ volatile("move.l %%macsr,%0\n\tmove.l #0,%%macsr\n\tmove.l %%acc0,%1\n\tmove.l %%accext01,%2\n\tmove.l %0,%%macsr"
                     :"=&d"(m),"=&d"(a),"=&d"(e)::"cc");
    *macsr=m;*acc=a;*ext=e;
}
u32 check_mix(u32 cases,u32 start)
{
    static const u32 modes[4]={0x20,0xa0,0x00,0x60};
    u32 k,t,i,bad=0,emac=0,mode,acc,ext,m2,a2,e2;
    seed=start;
    for(k=0;k<cases;k++){
        u32 bus_k=rng()&3,primed=rng()&1;s32 g[8],step[8];
        fake.mask=rng()&0xffu;
        for(t=0;t<8;t++){
            u32 big=(rng()&3)==0;
            fake.shift[t]=(unsigned char)(big?15+(rng()&1):rng()%17);
            for(i=0;i<32;i++){
                s32 m=(s32)(rng()%65535u)-32767;
                if(big && (rng()&1))m=(rng()&1)?32767:-32767;
                fake.d[t][i]=(short)m;
            }
            gains[t]=(rng()&7)==0?0:(s32)rng();
            ramp_start[bus_k][t]=(rng()&7)==0?0:(s32)rng();
        }
        if((k&7)==0)for(t=0;t<8;t++)gains[t]=ramp_start[bus_k][t]=-0x7fffffff;  /* full scale */
        ramp_primed[bus_k]=primed;
        for(i=0;i<64;i++)before[i]=bus[i]=(k&3)==3?(s32)(rng()|0x7ff00000u)^(s32)(rng()&0x80000000u):(s32)rng()>>(rng()&7);
        for(t=0;t<8;t++){g[t]=primed?ramp_start[bus_k][t]:gains[t];step[t]=(s32)((u32)gains[t]-(u32)g[t])>>5;}
        use=&fake;resolved=1;widen(&fake);
        mode=modes[k&3];acc=rng();ext=rng();set_state(mode,acc,ext);
        add(bus_k,bus,gains);
        get_state(&m2,&a2,&e2);
        if(m2!=mode || a2!=acc || e2!=ext)emac++;
        for(t=0;t<8;t++)if(ramp_start[bus_k][t]!=gains[t])emac++;
        for(i=0;i<32;i++){
            long long want=before[2*i+1];s32 got=bus[2*i+1];u32 counted=0;
            for(t=0;t<8;t++){
                s32 gi=(s32)((u32)g[t]+(u32)step[t]*i);
                if(!(fake.mask&(1u<<t)) || (!g[t] && !gains[t]))continue;
                want-=product30(gi,(s32)fake.d[t][i]<<fake.shift[t]);
                if(++counted==4){                                /* the end of a pass */
                    counted=0;
                    if(want>0x7fffffffLL)want=0x7fffffffLL;
                    if(want<-0x80000000LL)want=-0x80000000LL;
                }
            }
            if(want>0x7fffffffLL)want=0x7fffffffLL;
            if(want<-0x80000000LL)want=-0x80000000LL;
            {long long e=(long long)got-want;if(e<-8 || e>8)bad++;}
            if(bus[2*i]!=before[2*i])bad++;            /* left buses untouched */
        }
    }
    return bad<<16|emac;
}
/* One bus, eight tracks with D, for instruction counts. */
void bench_mix(void)
{
    u32 t;fake.mask=0xff;for(t=0;t<8;t++){gains[t]=-0x40000000;fake.shift[t]=8;}
    use=&fake;resolved=1;widen(&fake);add(2,bus,gains);
}
u32 debug_mix(u32 g,u32 h,u32 b)
{
    static s32 one[32],bb[64];const s32 *hp[4];s32 gg[4]={0,0,0,0},st[4]={0,0,0,0};struct emac_state e;
    one[0]=(s32)h;bb[1]=(s32)b;hp[0]=one;hp[1]=hp[2]=hp[3]=silent;gg[0]=(s32)g;
    emac_enter(&e);mix4(bb+1,hp,gg,st);emac_leave(&e);return (u32)bb[1];
}
