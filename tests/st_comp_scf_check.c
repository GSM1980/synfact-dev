/* SPDX-License-Identifier: GPL-2.0-or-later
 * COMP 0.3 detector loops (mods/comp-st/dsp.s: comp_peak, comp_hp) against
 * a 64-bit model, built for the target and run in analog-emu's cfbench, whose
 * EMAC follows the ColdFire Programmer's Reference Manual. The shim below sets
 * the loops' registers exactly as comp_kernel does.
 *
 * Model of MACSR A0h (fractional, saturating, truncating): ACC holds Q39;
 * MOVE Rx,ACC loads Rx<<8; MSAC subtracts the 64-bit product shifted to Q39
 * (low bits truncated); MOVE ACC,Rx / MOVCLR return ACC>>8. */
typedef unsigned int u32;typedef int s32;typedef long long s64;
/* dsp.s's externals: only the detector loops run here. */
u32 comp_hot,comp_peak_entry,comp_hp_entry,comp_curve_entry,comp_scale_entry;
u32 comp_current_config(u32 kit){return kit;}
extern void comp_peak(void),comp_hp(void);
extern const u32 comp_scf_check_coefficients[128];
/* state: lp L, lp R, a, peak out. adc: 32 L then 32 R. */
s32 call_hp(s32 *state,const s32 *adc);
s32 call_peak(const s32 *adc);
__asm__(
"        .text\n"
"        .globl call_hp,call_peak\n"
"call_hp:\n"
"        lea -32(%sp),%sp\n"
"        movem.l %d2-%d7/%a2,(%sp)\n"
"        movea.l 36(%sp),%a2\n"
"        movea.l 40(%sp),%a0\n"
"        moveq #0,%d3\n"
"        move.l 8(%a2),%d1\n"
"        move.l %macsr,%d4\n"
"        move.l #0xa0,%macsr\n"
"        movem.l (%a2),%d0/%d2\n"
"        move.l %d0,%acc0\n"
"        move.l %d2,%acc1\n"
"        moveq #31,%d7\n"
"        jsr comp_hp\n"
"        movclr.l %acc0,%d1\n"
"        movclr.l %acc1,%d2\n"
"        movem.l %d1-%d2,(%a2)\n"
"        move.l %d4,%macsr\n"
"        move.l %d0,12(%a2)\n"
"        movem.l (%sp),%d2-%d7/%a2\n"
"        lea 32(%sp),%sp\n"
"        rts\n"
"call_peak:\n"
"        lea -32(%sp),%sp\n"
"        movem.l %d2-%d7/%a2,(%sp)\n"
"        movea.l 36(%sp),%a0\n"
"        moveq #0,%d3\n"
"        moveq #15,%d7\n"
"        jsr comp_peak\n"
"        movem.l (%sp),%d2-%d7/%a2\n"
"        lea 32(%sp),%sp\n"
"        rts\n");

typedef unsigned long long u64;
/* 32x32 -> 64 without libgcc (the freestanding build has no __muldi3). */
static s64 mul64(s32 a,s32 b)
{
    u32 x=a<0?0u-(u32)a:(u32)a,y=b<0?0u-(u32)b:(u32)b;
    u64 r=(u64)((x&65535u)*(y&65535u))+((u64)((x>>16)*(y&65535u))<<16)
         +((u64)((x&65535u)*(y>>16))<<16)+((u64)((x>>16)*(y>>16))<<32);
    return (a<0)!=(b<0)?-(s64)r:(s64)r;
}
static u32 seed;
static u32 next(void){seed=seed*1664525u+1013904223u;return seed;}
static s32 sat(s64 v){return v>0x7fffffffLL?0x7fffffff:v<-0x80000000LL?(s32)0x80000000u:(s32)v;}
static s32 floor256(s32 p){return p<256?256:p;}
static s32 model_peak(const s32 *adc)
{s32 p=0,i,x;for(i=0;i<64;i++){x=adc[i];if(x<0)x=~x;if(x>p)p=x;}return floor256(p);}
static s32 model_hp(s32 *state,const s32 *adc)
{
    s64 acc[2];s32 p=0,i,c,y,a=state[2];
    for(c=0;c<2;c++)acc[c]=(s64)state[c]<<8;
    for(i=0;i<32;i++)for(c=0;c<2;c++){
        y=(s32)((u32)sat(acc[c]>>8)-(u32)adc[32*c+i]);
        acc[c]-=mul64(y,a)>>23;
        if(y<0)y=~y;
        if(y>p)p=y;
    }
    for(c=0;c<2;c++)state[c]=sat(acc[c]>>8);
    return floor256(p);
}
static s32 adc[64],hw[4],mod[4];
static void fill(u32 kind,u32 block)
{
    u32 i,c;
    for(c=0;c<2;c++)for(i=0;i<32;i++){
        s32 v;u32 n=block*32+i;
        switch(kind){
        case 0:v=(s32)next();break;                                   /* full-scale noise */
        case 1:v=(s32)next()>>3;break;                                /* quieter noise */
        case 2:v=(n/23)&1?0x6fffffff:-0x70000000;break;               /* square, ~1 kHz */
        case 3:v=(n/480)&1?0x60000000:-0x60000000;break;              /* square, 50 Hz */
        case 4:v=c?0x7fffffff:(s32)0x80000000u;break;                 /* rails */
        default:v=0;break;
        }
        adc[32*c+i]=v;
    }
}
/* -> differing blocks <<16 | blocks run */
u32 check_scf(u32 blocks,u32 start)
{
    u32 bad=0,b,i,k;seed=start;
    for(b=0;b<blocks;b++){
        u32 kind=b/64%6;
        if(b%64==0){k=1+b/64*37%127;for(i=0;i<2;i++)hw[i]=mod[i]=(s32)next()>>(kind==4?0:4);
                    hw[2]=mod[2]=(s32)comp_scf_check_coefficients[k];}
        fill(kind,b);
        hw[3]=call_hp(hw,adc);mod[3]=model_hp(mod,adc);
        if(hw[0]!=mod[0] || hw[1]!=mod[1] || hw[3]!=mod[3] || hw[2]!=mod[2])bad++;
        if(call_peak(adc)!=model_peak(adc))bad++;
    }
    return bad<<16|blocks;
}
/* Instruction counts: one block of each loop (cfbench prints them). */
u32 bench_peak(void){fill(1,0);return (u32)call_peak(adc);}
u32 bench_hp(u32 k){fill(1,0);hw[0]=hw[1]=0;hw[2]=(s32)comp_scf_check_coefficients[k];return (u32)call_hp(hw,adc);}
/* Steady block peak of a sine through position k (0 = the input's own
 * peak): w = 2 sin(pi f / 48000) in Q30, from the caller. Integer
 * oscillator by rotation (magic circle), amplitude about 2^29. */
u32 response(u32 k,u32 w)
{
    u32 b,i;s32 p,q=0,s=0,co=1<<29;
    hw[0]=hw[1]=0;hw[2]=(s32)comp_scf_check_coefficients[k];
    for(b=0;b<300;b++){
        for(i=0;i<32;i++){adc[i]=adc[32+i]=s;s+=(s32)(mul64(co,(s32)w)>>30);co-=(s32)(mul64(s,(s32)w)>>30);}
        p=call_hp(hw,adc);
        if(b>=200 && p>q)q=p;
    }
    return (u32)q;
}
