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

/* The expansion kernel has a separate exact oracle, including inactive
 * tracks and canaries around its complete output buffer. */
u32 check_widen(u32 cases,u32 start)
{
    static s32 guarded[258];u32 k,t,i,bad=0,mode,acc,ext,m2,a2,e2;
    seed=start;
    for(k=0;k<cases;k++){
        fake.mask=k&255u;
        for(t=0;t<8;t++){
            fake.shift[t]=(unsigned char)((k+t)%17u);
            for(i=0;i<32;i++)fake.d[t][i]=(short)rng();
        }
        for(i=0;i<258;i++)guarded[i]=(s32)0x5a5aa5a5u;
        mode=(k&3u)*0x20u;acc=rng();ext=rng();set_state(mode,acc,ext);
        widen_kernel(&fake,guarded+1);get_state(&m2,&a2,&e2);
        if(mode!=m2 || acc!=a2 || ext!=e2)bad++;
        for(t=0;t<8;t++)for(i=0;i<32;i++){
            u32 want=(fake.mask&(1u<<t))?(u32)(s32)fake.d[t][i]<<fake.shift[t]:0x5a5aa5a5u;
            if((u32)guarded[1+t*32+i]!=want)bad++;
        }
        if((u32)guarded[0]!=0x5a5aa5a5u || (u32)guarded[257]!=0x5a5aa5a5u)bad++;
    }
    return bad;
}

/* Placement is exercised with real executable SRAM in cfbench, not a host
 * memory stand-in. Modes: absent, Chorus, occupied state, occupied code. */
u32 check_placement(u32 mode)
{
    static unsigned char directory[80];
    static const unsigned char *published;
    static struct st_machine_host_api host;
    volatile u32 *p;u32 i,bad=0;
    st_stereo_state=&stereo_ddr;use=0;
    copy32=st_stereo_copy32;copy64=st_stereo_copy64;
    mix4_kernel=st_stereo_mix4;resolve_kernel=st_stereo_resolve;widen_kernel=st_stereo_widen;
    remember_kernel=st_stereo_remember;
    placement_done=placement_ticks=st_stereo_sram=0;
    for(p=(volatile u32 *)ST_SRAM_CHORUS;p<(volatile u32 *)ST_SRAM_TAIL0_END;p++)*p=0;
    for(p=(volatile u32 *)ST_SRAM_CHORUS_CODE;p<(volatile u32 *)ST_SRAM_CHORUS_CODE_END;p++)*p=0;
    for(i=0;i<sizeof(directory);i++)directory[i]=0;
    published=0;host.bank_info=&published;stereo_host=&host;
    placement_tick(0);if(placement_done || st_stereo_sram)bad++;
    directory[0]='x';
    if(mode==1){static const char chorus[]="chorus-st";for(i=0;i<sizeof(chorus);i++)directory[i]=chorus[i];}
    if(mode==2)*(volatile u32 *)ST_SRAM_CHORUS=0x12345678;
    if(mode==3)*(volatile u32 *)ST_SRAM_CHORUS_CODE=0x12345678;
    published=directory;
    placement_tick(0);
    if(st_stereo_sram!=(mode==0 || mode==4))bad++;
    if(mode==0 || mode==4){
        if((u32)st_stereo_state!=ST_SRAM_CHORUS || (u32)mix4_kernel!=ST_SRAM_CHORUS_CODE)bad++;
        bad+=check_mix(400,0x5eed);bad+=check_widen(400,0xdead);
        /* Check the resolver runs from SRAM too: untagged is a safe exit. */
        resolved=0;tagged=0;if(current()!=0 || !resolved)bad++;
        /* A genuine internal slot pointer must survive fallback. */
        slot[0].sequence=0x12345678;use=&slot[0];
        if(mode==4)*(volatile unsigned char *)(void *)remember_kernel^=1;
        else *(volatile unsigned char *)ST_SRAM_CHORUS_CODE^=1;
        for(i=0;i<60;i++)placement_tick(0);
        if(st_stereo_sram || st_stereo_state!=&stereo_ddr || use!=&slot[0] || slot[0].sequence!=0x12345678)bad++;
        if(remember_kernel!=st_stereo_remember)bad++;
        bad+=check_mix(400,0xfeed);bad+=check_widen(400,0xbeef);
    }else{
        if((mode==2 && *(volatile u32 *)ST_SRAM_CHORUS!=0x12345678) ||
           (mode==3 && *(volatile u32 *)ST_SRAM_CHORUS_CODE!=0x12345678))bad++;
        bad+=check_mix(400,0x5eed);bad+=check_widen(400,0xdead);
    }
    return bad;
}
