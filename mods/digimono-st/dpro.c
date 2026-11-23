/* SPDX-License-Identifier: AGPL-3.0-or-later
 * DPRO-WAVE, DPRO-DDRW and DPRO-DENS from De-Pattern a0249fc
 * (dpro/provenance.json): an integer ColdFire port of wave.py, dec.py,
 * dpro.py, ddrw.py and dens.py, at the Monomachine's 44.1 kHz / 16 frames,
 * then FM+'s 16-tap converter to 48 kHz. The increments are the Monomachine's
 * own integer arithmetic (exact.py). DDRW/DENS read De-Pattern's Digibank
 * stand-in (32 ROM waves, nine mip levels) at DM_DPRO_OS times the block rate:
 * 1 is De-Pattern's shipped rate, 2 the Monomachine's with its 2:1 decimator.
 * DDRW's TIME and BR follow De-Pattern's host mapping (v0.56). The tables come
 * from the owner's Monomachine OS (dpro/import_tables.py); none is committed.
 * Syntakt's AMP/FLTR/FX remain downstream. No float or library calls.
 *
 * Sums of products run in the EMAC's accumulators (fractional, truncating):
 * each product enters as floor(p / 2^23), the read is floor(sum / 2^8). The
 * split build (host, DM_DP_SPLIT) computes exactly that, so both give the
 * same bits; tests/test_st_dpro_cfbench.py checks it on analog-emu.
 */
#include "dpro/voice.h"
#if !__has_include("dpro/tables.h")
#error "DigiPRO tables missing: python mods/digimono-st/dpro/import_tables.py <Monomachine OS 1.32B .syx>"
#endif
#include "dpro/tables.h"
#include "fmplus/tables.h"

#ifndef DM_DPRO_OS
#define DM_DPRO_OS 1
#endif
#if DM_DPRO_OS!=1 && DM_DPRO_OS!=2
#error "DM_DPRO_OS is 1 or 2"
#endif
/* The blocks run at 48 kHz by default: two 16-frame blocks a render, no
 * converter. Every rate-dependent quantity is scaled by 44100/48000 = 147/160
 * (increments, WAVE's sync period and WP sweep, DDRW's fade), so pitch and
 * time stay the Monomachine's; only the fixed filters' corners move up 8.8%.
 * DM_DPRO_44K keeps the Monomachine's 44.1 kHz blocks and FM+'s converter. */
#if defined(DM_DPRO_44K)
#define R48(x) (x)
#else
#define R48(x) (((x)/160u)*147u+(((x)%160u)*147u)/160u)
#endif
#define N2 (16*DM_DPRO_OS)
#define PHASE_SH (DM_DPRO_OS==2?3:2)
#define NBUF 37
#define ONE (1<<26)
#define TOP (ONE-8)
#define TOP23 0x7fffff
#define M24 0xffffffu

/* ColdFire has no 32x32->64 multiply: exact products from 16-bit limbs.
 * Host builds use the native product; both give the same bits. */
static inline int64_t smul(int32_t a,int32_t b)
{
#if !defined(__mcoldfire__)
    return (int64_t)a*b;
#else
    uint32_t x=(uint32_t)a,y=(uint32_t)b;
    uint32_t x0=x&65535u,x1=x>>16,y0=y&65535u,y1=y>>16;
    uint32_t low=x0*y0,mid=x1*y0+(low>>16),carry=x0*y1+(mid&65535u);
    uint32_t hi=x1*y1+(mid>>16)+(carry>>16);
    if(a<0)hi-=y;
    if(b<0)hi-=x;
    return (int64_t)(((uint64_t)hi<<32)|((carry<<16)|(low&65535u)));
#endif
}

#if defined(__mcoldfire__) && !defined(DM_DP_SPLIT)
#define DP_EMAC 1
/* ACC0 and ACC1 are ours inside a render: MACSR, both accumulators and
 * their extension words are saved and restored around it. */
struct mac_state {uint32_t sr,acc0,acc1,ext;};
static inline void mac_enter(struct mac_state *s)
{
    uint32_t clear;
    __asm__ volatile("move.l %%macsr,%0\n\tmove.l #0,%%macsr\n\tmove.l %%acc0,%1\n\tmove.l %%acc1,%2\n\t"
                     "move.l %%accext01,%3\n\tmove.l #0x20,%%macsr\n\tmovclr.l %%acc0,%4\n\tmovclr.l %%acc1,%4"
                     :"=&d"(s->sr),"=&d"(s->acc0),"=&d"(s->acc1),"=&d"(s->ext),"=&d"(clear)::"cc");
}
static inline void mac_leave(const struct mac_state *s)
{
    __asm__ volatile("move.l #0,%%macsr\n\tmove.l %0,%%acc0\n\tmove.l %1,%%acc1\n\t"
                     "move.l %2,%%accext01\n\tmove.l %3,%%macsr"
                     ::"d"(s->acc0),"d"(s->acc1),"d"(s->ext),"d"(s->sr):"cc");
}
#define MAC0(x,c) __asm__ volatile("mac.l %0,%1,%%acc0"::"r"((int32_t)(x)),"r"((int32_t)(c)):"cc")
#define MAC1(x,c) __asm__ volatile("mac.l %0,%1,%%acc1"::"r"((int32_t)(x)),"r"((int32_t)(c)):"cc")
static inline int32_t read0(void){int32_t r;__asm__ volatile("movclr.l %%acc0,%0":"=d"(r)::"cc");return r;}
static inline int32_t read1(void){int32_t r;__asm__ volatile("movclr.l %%acc1,%0":"=d"(r)::"cc");return r;}
#else
static int64_t dp_acc0,dp_acc1;
#define MAC0(x,c) (dp_acc0+=smul((int32_t)(x),(int32_t)(c))>>23)
#define MAC1(x,c) (dp_acc1+=smul((int32_t)(x),(int32_t)(c))>>23)
static inline int32_t read0(void){int32_t r=(int32_t)(dp_acc0>>8);dp_acc0=0;return r;}
static inline int32_t read1(void){int32_t r=(int32_t)(dp_acc1>>8);dp_acc1=0;return r;}
#endif
/* floor(a x b / 2^31), on ACC0 (which must be empty) */
static inline int32_t mul31(int32_t a,int32_t b){MAC0(a,b);return read0();}
/* a x c / 2^23, c a Q23 word under 1 (m23) or under 4 (m23w, |a| < 2^29). */
static inline int32_t m23(int32_t a,int32_t c){return mul31(a,c*256);}
static inline int32_t m23w(int32_t a,int32_t c){return mul31(a*4,c*64);}
static inline int32_t sat(int32_t a)
{return (uint32_t)(a+ONE)>(uint32_t)(TOP+ONE)?(a<0?-ONE:TOP):a;}   /* one compare */
static inline int32_t sat23(int32_t a){return a>TOP23?TOP23:a< -0x800000?-0x800000:a;}
static inline int32_t s24(uint32_t v){return (int32_t)(v<<8)>>8;}

/* -- the Monomachine's increments (exact.py), 56-bit words in int64 ------------- */
#define A47 ((int64_t)1<<47)
static inline int64_t acc(uint32_t w){return (int64_t)s24(w)*16777216;}
static inline uint32_t lim(int64_t a)
{return a>=A47?0x7fffffu:a< -A47?0x800000u:(uint32_t)(a>>24)&M24;}
static inline int64_t mpy(uint32_t x,uint32_t y){return smul(s24(x),s24(y))*2;}
/* The exp table's word for F, limited: (table[F & 0x7ff] >> 10) << (F >> 11). */
static uint32_t exp_lim(int32_t F)
{
    uint32_t e=dpt_exp[F&0x7ff],x0=(uint32_t)F>>11;
    if(x0>=10){e<<=x0-10;return e>0x7fffffu?0x7fffffu:e;}
    return e>>(10-x0);
}
/* A, the kernel's increment (P:0x2DD..0x2EA). Positive, under 2^47. */
static int64_t increment(int32_t F){return mpy(0x1d22a,exp_lim(F))>>3;}
static uint32_t sync_period(int32_t F)
{
    /* blocks of 16 frames at the block rate: 44100 (0xac44) or 48000 */
    uint32_t x0=lim(mpy(0x20b40,exp_lim(F))*4096);
#if defined(DM_DPRO_44K)
    return x0?(0xac44u<<12)/x0:0;
#else
    return x0?(0xbb80u<<12)/x0:0;
#endif
}
static uint32_t dmac_ss(uint32_t k,int64_t inc)
{
    uint32_t hi=lim(inc),lo=inc>=A47?M24:inc< -A47?0:(uint32_t)inc&M24;
    return (uint32_t)((smul(s24(k),(int32_t)lo)*2>>24)+mpy(k,hi));
}
static int64_t dmac_ss64(uint32_t k,uint32_t lo,uint32_t hi)
{return (smul(s24(k),(int32_t)lo)*2>>24)+mpy(k,hi);}
#define SUB 0x17c70u
#define SUB_B 0x1aa17u

int32_t dm_dp_word(unsigned type,int32_t pitch)
{
    /* FM+'s native pitch law: Syntakt's note 60 is 261.6 Hz. On the
     * Monomachine DPRO-WAVE sounds an octave under the others, so it is
     * played an octave up (De-Pattern's TRANSPOSE). */
    if(pitch<0)pitch=0;
    if(pitch>140*64)pitch=140*64;
    int32_t F=pitch*8/3+341+(type==DM_DPWAVE?2048:0);
    return F>0x5800?0x5800:F;
}
static void increments(struct dm_dp_voice *v,unsigned type,const uint16_t *p,int32_t F,int32_t pre)
{
    int64_t A=increment(F);
    uint32_t hi=(uint32_t)(A>>24)&M24,lo=(uint32_t)A&M24;
    if(type==DM_DPWAVE){
        /* A x 0x2f8e (MPYSU, DMACSS), samples << 21 */
        int64_t a=(smul(0x2f8e,(int32_t)lo)*2>>24)+mpy(0x2f8e,hi);
        uint32_t inc=(uint32_t)((((a>>24)&M24)<<21)+((a&M24)>>3));
        v->inc[0]=R48(inc);
        unsigned sync=p[4]>>8,sfrq=p[5]>>8;
        if(sync>127)sync=127;
        if(sfrq>127)sfrq=127;
        if((sync<<16)<0x2aaaaau)v->extra=0;
        else if((sync<<16)>=0x555555u)v->extra=sync_period(pre);
        else v->extra=sfrq?sync_period((int32_t)(52*sfrq+0x1000)):0;
    }else if(type==DM_DPDENS){
        uint32_t b=dmac_ss(SUB,A);
        v->inc[0]=R48(b);
        uint32_t mute=0;
        for(unsigned k=0;k<3;k++){
            unsigned knob=p[k]>>8;
            int64_t inc=dmac_ss64(dpt_pch[knob>127?127:knob],lo,hi)*4;
            uint32_t h=lim(inc);
            if(s24(h)>=0x4e20 || h==hi)mute|=2u<<k;      /* in Hz: rate-free */
            b=dmac_ss(SUB,inc);
            v->inc[k+1]=R48(b);
        }
        v->extra=mute;
    }else{
        unsigned wid=p[5]>>8,mix=p[1]>>8;
        if(wid>127)wid=127;
        if(mix>127)mix=127;
        uint32_t t1=dpt_wid[wid],t2=dpt_wid[128+wid];
        int64_t a=acc(mix<<16)*2+acc(0x800000u);
        if(a<0)a=-a;
        uint32_t x0=lim(acc(0x7fffffu)-a);
        uint32_t y0=lim(acc(t2)-acc(0x400000u)),y1=lim(acc(t1)-acc(0x400000u));
        uint32_t r2=lim(mpy(y0,x0)+acc(0x400000u)),r1=lim(mpy(x0,y1)+acc(0x400000u));
        for(unsigned n=0;n<2;n++){
            uint32_t r=n?r2:r1;
            int64_t inc=((smul(s24(r),(int32_t)lo)*2>>24)+smul(s24(r),(int32_t)hi)*2)*2;
            uint32_t ba=dmac_ss(SUB,inc),bb=dmac_ss(SUB_B,inc);
            v->inc[n]=R48(ba);
            v->inc_b[n]=R48(bb);
        }
    }
}
void dm_dp_increments(struct dm_dp_voice *v,unsigned type,const uint16_t *p,int32_t F,int32_t pre)
{
    /* Recomputed only when what they depend on changes: the pitch word, the
     * previous track's (WAVE's PRCH), and the knobs each machine reads. */
    uint32_t key[4];
    key[0]=(uint32_t)F|type<<16;
    key[1]=type==DM_DPWAVE?(uint32_t)pre:0;
    key[2]=type==DM_DPWAVE?(uint32_t)(p[4]>>8)<<8|(p[5]>>8):
           type==DM_DPDENS?(uint32_t)(p[0]>>8)<<8|(p[1]>>8):(uint32_t)(p[1]>>8)<<8|(p[5]>>8);
    key[3]=type==DM_DPDENS?(uint32_t)(p[2]>>8):0;
    if(v->key[0]==key[0] && v->key[1]==key[1] && v->key[2]==key[2] && v->key[3]==key[3])return;
    for(unsigned i=0;i<4;i++)v->key[i]=key[i];
    increments(v,type,p,F,pre);
}

void dm_dp_init(struct dm_dp_voice *v)
{
    unsigned char *b=(unsigned char *)v;
    for(unsigned i=0;i<sizeof(*v);i++)b[i]=0;
    v->cursor=16;v->need=1;v->fresh=1;
}
void dm_dp_trig(struct dm_dp_voice *v,unsigned type,const uint16_t *p)
{
    /* WAVE restarts its sync count, and its WP sweep with WPRS on; DDRW and
     * DENS keep running through a trig, as on the Monomachine. */
    if(type!=DM_DPWAVE)return;
    v->sync=0;
    if(p[3]>=0x4000)v->pos=0;
}

/* -- DPRO-WAVE: 64 samples a block, then the polyphase decimator ------------- */
/* dec.py's coefficients << 8 (Q31), negated where the program subtracts. */
#define NC0 (-0x3ce85f00)
#define C1  (-0x060eaa00)
#define NC2 0x6c66a800
#define C3  0x04d33e00
#define NC4 (-0x36f78900)
#define C5  0x11b45100
#define NC6 0x6fdc1400
#define C7  (-0x11b45100)
#define NC8 0x39ad5300
#define C9  0x0d1e2d00
#define GQ  0x00618600
#define G2Q 0x0030c300                                /* G / 2, exactly */
static void decimate(int32_t *xs,const int32_t *in,int32_t *out)
{
    /* dec.py's register program, two inner rounds (OS 4): x the five state
     * words, y1 the input. Each run of products into one register before its
     * limiter is one accumulator chain. A chain the program doubles (ASL)
     * before adding the input's G y1 sums the products and G/2 y1, then
     * doubles once: under one Q26 LSB from doubling each term. */
    int32_t x0=xs[0],x1=xs[1],x2=xs[2],x3=xs[3],x4=xs[4],a,b,f,y1;
    for(unsigned n=0;n<16;n++){
        MAC0(x0,NC0);MAC0(x1,NC2);x0=x1;y1=*in++;MAC0(y1,G2Q);a=read0()*2;
        for(unsigned r=0;r<2;r++){
            MAC0(x2,NC4);MAC0(x3,NC6);x2=x3;MAC0(y1,G2Q);b=read0()*2;x1=sat(a);
            f=b;MAC0(x4,NC8);MAC0(y1,G2Q);b=read0()*2;x3=sat(f);
            MAC0(x0,NC0);MAC0(x1,NC2);x0=x1;y1=*in++;MAC0(y1,G2Q);a=read0()*2;x4=sat(b);
        }
        MAC0(x2,NC4);MAC0(x3,NC6);x2=x3;MAC0(y1,G2Q);b=read0()*2;x1=sat(a);
        f=b;MAC0(x4,NC8);MAC0(y1,G2Q);b=read0()*2;x3=sat(f);
        MAC1(x0,C1);MAC0(x0,NC0);x4=sat(b);
        MAC0(x1,NC2);x0=x1;y1=*in++;MAC0(y1,G2Q);a=read0()*2;
        MAC1(x1,C3);
        f=a;MAC0(x2,NC4);x1=sat(f);
        MAC1(x2,C5);MAC0(x3,NC6);x2=x3;MAC0(y1,G2Q);a=read0()*2;
        MAC1(x3,C7);
        f=a;MAC0(x4,NC8);x3=sat(f);MAC0(y1,G2Q);a=read0()*2;
        MAC1(x4,C9);
        x4=sat(a);b=read1();
        *out++=b>=ONE/32?TOP:b< -ONE/32?-ONE:b*32;      /* ASL 5, limited */
    }
    xs[0]=x0;xs[1]=x1;xs[2]=x2;xs[3]=x3;xs[4]=x4;
}
static void wave_half(struct dm_dp_voice *v,const uint16_t *p,int32_t *out)
{
    int32_t buf[64];
    if((int32_t)v->extra>0 && ++v->sync>=(int32_t)v->extra){v->sync=0;v->u[0]=0;}
    int32_t x;
    /* pos: the sweep, 24 bits with 8 fraction bits, WPM << 6 a 44.1 kHz block */
#if defined(DM_DPRO_44K)
    v->pos+=(int32_t)(p[2]>>8)*16384;
#else
    v->pos+=(int32_t)(p[2]>>8)*15053;                  /* 16384 x 147 / 160 */
#endif
    x=(int32_t)((uint32_t)v->pos>>8)+(int32_t)(p[1]>>8)*65536;
    if(x>0x7fffff){x=0xffffff-x;if(x<0)x=-x;}
    uint32_t len=((uint32_t)x>>14)+0x200,edge=len&~1u,end=len<<21;
    const int32_t *w=dpt_wave+(p[0]>>10)*512u;
    uint32_t ph=v->u[0],inc=v->inc[0];
    for(int32_t *o=buf;o<buf+64;o++){
        /* T[i] and T[s1]: s1 is i + 1, back 512 samples at the window's end
         * ((i + 1) / 2 >= len / 2) */
        uint32_t i=ph>>21,j=i+1;
        if(j>=edge)j-=512;
        int32_t t0=w[i];
        *o=sat(t0*2+mul31((w[j]-t0)*2,(int32_t)((ph<<11)>>1)));
        ph+=inc;
        if(ph>=end)ph-=512u<<21;
    }
    v->u[0]=ph;
    decimate(v->dec,buf,out);
}

/* -- DDRW and DENS: a Digibank level read at N2, the kernel's interpolator ---- */
struct level {uint32_t sh,msk,boff,il,fr,i0,n;int32_t wx;};
static inline void start(struct level *s,uint32_t bs,uint32_t b,uint32_t u,uint32_t hf)
{
    /* dpro._start: the mip level from the 2x increment's exponent, its
     * crossfade over each octave's top 1/16, and the read's start. */
    uint32_t x=bs>>8,n,m;
    int32_t sh;
    n=x?31u-(uint32_t)__builtin_clz(x):0;              /* ff1 */
    sh=x?(int32_t)n-15:0;
    if(sh<0)sh=0;
    if(sh>8)sh=8;
    s->sh=(uint32_t)sh;
    uint32_t size=1024u>>sh;
    s->msk=size-1;s->boff=2048-2*size;
    s->il=b>>sh;                                       /* Q24 */
    m=x?x<<(23-n):0x800000u;                           /* the mantissa, Q24 */
    s->wx=m>=0xf00000u?(int32_t)(m-0xf00000u)*8:0;
    if(s->wx>TOP23)s->wx=TOP23;
    uint32_t whole=u>>(22+sh),frac=u&((1u<<(22+sh))-1);
    frac=sh>=2?frac>>(sh-2):frac<<(2-sh);              /* Q24 */
    int32_t fr=(int32_t)frac-(int32_t)hf;
    if(fr<0){fr+=1<<24;whole--;}
    s->fr=(uint32_t)fr;
    s->i0=(whole-1)&s->msk;
    /* the samples the interpolator reaches: its last start, plus five taps */
    s->n=((s->fr+(N2-1)*s->il)>>24)+5;
    if(s->n>NBUF)s->n=NBUF;
}
static void interp(const int32_t *buf,const struct level *s,int32_t g,int32_t *out,int add)
{
    /* The kernel's 5-tap, 32-phase filter with linear interpolation between
     * phases: sum x (B + A res) = sum x B + res sum x A, two accumulators. */
    uint32_t P=s->fr,il=s->il;          /* locals: the asm clobbers memory */
    for(unsigned k=0;k<N2;k++){
        uint32_t r=(P&M24)+0x800000u;
        if(r>=1u<<24)r-=1u<<24;
        const int32_t *c=dpt_interp+10*(r>>19),*x=buf+(P>>24);
#if defined(DP_EMAC)
        /* MAC with load: each tap's B and A ride on the products */
        int32_t sum,slope,xv,cv;
#define DP_KTAP "move.l (%[x])+,%[xv]\n\t" \
                "mac.l %[xv],%[cv],(%[c])+,%[cv],%%acc0\n\t" \
                "mac.l %[xv],%[cv],(%[c])+,%[cv],%%acc1\n\t"
        __asm__ volatile("move.l (%[c])+,%[cv]\n\t"
                         DP_KTAP DP_KTAP DP_KTAP DP_KTAP
                         "move.l (%[x])+,%[xv]\n\t"
                         "mac.l %[xv],%[cv],(%[c])+,%[cv],%%acc0\n\t"
                         "mac.l %[xv],%[cv],%%acc1\n\t"
                         "movclr.l %%acc0,%[s]\n\tmovclr.l %%acc1,%[t]"
                         :[x]"+a"(x),[c]"+a"(c),[xv]"=&d"(xv),[cv]"=&d"(cv),[s]"=d"(sum),[t]"=d"(slope)
                         ::"cc","memory");
#undef DP_KTAP
#else
        MAC0(x[0],c[0]);MAC1(x[0],c[1]);MAC0(x[1],c[2]);MAC1(x[1],c[3]);
        MAC0(x[2],c[4]);MAC1(x[2],c[5]);MAC0(x[3],c[6]);MAC1(x[3],c[7]);
        MAC0(x[4],c[8]);MAC1(x[4],c[9]);
        int32_t sum=read0(),slope=read1();
#endif
        sum=m23(sat(sum+mul31(slope,(int32_t)(r&0x7ffffu)<<12)),g);
        out[k]=add?out[k]+sum:sum;
        P+=il;
    }
}
/* DENS's light interpolator: the kernel's 5 taps at 256 fixed phases (each of
 * its 32 phases' eight sub-steps at their middle), one dot product, no slope
 * term: five products instead of ten. The coefficients move by at most 1/16 of
 * a phase step (about -60 dB). DM_DENS_KERNEL keeps the kernel. */
static void interp_fixed(const int32_t *buf,const struct level *s,int32_t g,int32_t *out,int add)
{
    uint32_t P=s->fr,il=s->il;
    for(unsigned k=0;k<N2;k++){
        uint32_t r=(P&M24)+0x800000u;
        if(r>=1u<<24)r-=1u<<24;
        const int32_t *c=dpt_interp256+((r>>16)<<3),*x=buf+(P>>24);
        int32_t sum;
#if defined(DP_EMAC)
        int32_t xv,cv;
#define DP_FTAP "move.l (%[x])+,%[xv]\n\tmac.l %[xv],%[cv],(%[c])+,%[cv],%%acc0\n\t"
        __asm__ volatile("move.l (%[c])+,%[cv]\n\t"
                         DP_FTAP DP_FTAP DP_FTAP DP_FTAP DP_FTAP
                         "movclr.l %%acc0,%[s]"
                         :[x]"+a"(x),[c]"+a"(c),[xv]"=&d"(xv),[cv]"=&d"(cv),[s]"=d"(sum)::"cc","memory");
#undef DP_FTAP
#else
        MAC0(x[0],c[0]);MAC0(x[1],c[1]);MAC0(x[2],c[2]);MAC0(x[3],c[3]);MAC0(x[4],c[4]);
        sum=read0();
#endif
        sum=m23(sat(sum),g);
        out[k]=add?out[k]+sum:sum;
        P+=il;
    }
}
static inline uint32_t scaled(uint32_t b)
{
#if DM_DPRO_OS==1
    /* dpro.b_scaled: the 2x increment doubled, held under 2^32 */
    return (int32_t)b<0?0xfffffffeu:b*2;
#else
    return b;
#endif
}
#if DM_DPRO_OS==2
static void dec2(int32_t *st,int32_t *mix,int32_t *out)
{
    /* the kernel's 2:1 allpass (P:0x458): nine first-order stages, four on
     * the even samples, five on the odd ones */
    static const int32_t c[9]={0x1583e8,0x4092ff,0x66e24c,0,0x05da6b,0x2a8f65,0x54e3ce,0,0x778898};
    for(unsigned k=0;k<9;k++){
        int32_t *x=mix+(k<=3?0:1),last=st[2*k],y=st[2*k+1];
        for(unsigned j=0;j<16;j++){
            int32_t in=sat(x[2*j]),o=last+m23(in-y,c[k]);
            x[2*j]=o;y=sat(o);last=in;
        }
        st[2*k]=last;st[2*k+1]=y;
    }
    for(unsigned j=0;j<16;j++)out[j]=sat((mix[2*j]+mix[2*j+1])/2);
}
#endif
static void finish(struct dm_dp_voice *v,int32_t *mix,int32_t *out)
{
#if DM_DPRO_OS==2
    dec2(v->dec,mix,out);
#else
    (void)v;
    for(unsigned j=0;j<16;j++)out[j]=sat(mix[j]);
#endif
}

#define DENS_GAIN 0x510625
static void dens_half(struct dm_dp_voice *v,const uint16_t *p,int32_t *out)
{
    int32_t buf[NBUF],mix[N2];
    const int32_t *wave=dpt_digibank+(((p[3]>>9)&31u)<<11);
    for(unsigned k=0;k<4;k++){
        uint32_t bs=v->inc[k],b=scaled(bs);
        if(!(v->extra&(1u<<k))){
            struct level s;
            start(&s,bs,b,v->u[k],0x800000u);
            int32_t y1=s.sh?s.wx:0,w=TOP23-y1;
            const int32_t *cb=wave+s.boff,*x=buf;
            uint32_t i0=s.i0;
            if(!y1){
                /* 15/16 of every octave: the level alone (x TOP: under one
                 * Q23 LSB), read in place unless the window wraps */
                if(i0+s.n<=s.msk+1)x=cb+i0;
                else for(unsigned j=0;j<s.n;j++)buf[j]=cb[i0++&s.msk];
            }else{
                /* the top 1/16: toward the partner, the finer level's low
                 * bytes (precomputed at this level's positions) */
                const int32_t *pt=dpt_partner+(cb-dpt_digibank);
                int32_t wq=w*256,yq=y1*256;
                for(unsigned j=0;j<s.n;j++){
                    uint32_t idx=i0++&s.msk;
                    MAC0(cb[idx],wq);MAC0(pt[idx],yq);
                    buf[j]=sat(read0());
                }
            }
#if defined(DM_DENS_KERNEL)
            interp(x,&s,DENS_GAIN,mix,k!=0);
#else
            interp_fixed(x,&s,DENS_GAIN,mix,k!=0);
#endif
        }
        v->u[k]+=b<<PHASE_SH;
    }
    finish(v,mix,out);
}

static uint32_t fade_step(uint32_t rate,uint32_t il)
{
    /* rate / (N2 min(il, 1)), at most 1 (Q23; il Q24) */
    uint32_t d=(N2*(il>M24?M24:il))>>12;
    if(!d)return TOP23;
    uint32_t q=(rate<<8)/d;
    return q>=1u<<19?TOP23:q<<4;
}
static void ddrw_half(struct dm_dp_voice *v,const uint16_t *p,int32_t *out)
{
    int32_t buf[NBUF],w12[2][N2],mix[N2];
    uint32_t rate=R48(dpt_rate[(p[3]>>8)>127?127:p[3]>>8]);
    if(v->fresh){
        for(unsigned n=0;n<2;n++){v->now[n]=v->old[n]=p[2*n]>>9;v->fade[n]=TOP23;}
        v->mix=m23w((int32_t)p[1]*2048,0x810204);
    }
    for(unsigned n=0;n<2;n++){
        uint32_t br=p[4+2*n]>>8;
        if(br>127)br=127;
        if(br)br=64+((63*br+63)>>7);                    /* De-Pattern's BR mapping */
        int32_t x48=(int32_t)dpt_br[2*br]*4,x49=(int32_t)dpt_br[2*br+1];
        int32_t wav=p[2*n]>>9;
        uint32_t b2=v->inc[n],b=scaled(b2);
        if(wav!=v->now[n] && v->fade[n]>=0x7fff00){v->old[n]=v->now[n];v->now[n]=wav;v->fade[n]=0;}
        for(unsigned m=0;m<2;m++){
            struct level s;
            start(&s,m?v->inc_b[n]:b2,b,v->u[n],m?0x880000u:0x800000u);
            if(m && !s.wx)continue;                    /* level B adds exactly 0 here */
            const int32_t *cb=dpt_digibank+(((uint32_t)v->now[n]&31u)<<11)+s.boff;
            const int32_t *fb=dpt_digibank+(((uint32_t)v->old[n]&31u)<<11)+s.boff;
            uint32_t i0=s.i0,msk=s.msk;
            int32_t f=v->fade[n];
            /* the reducer: floor(t24 x X49 / 2^23), then x X48 / 2^24 */
            if(f>=TOP23)for(unsigned j=0;j<s.n;j++){
                /* the fade is over: the new wave alone (x TOP: under one Q23 LSB) */
                buf[j]=sat(mul31(cb[i0++&msk]*32,x49)*x48);
            }else{
                uint32_t step=fade_step(rate,s.il);
                for(unsigned j=0;j<s.n;j++){
                    uint32_t idx=i0++&msk;
                    int32_t now=cb[idx],old=fb[idx],fd=sat23(f);
                    f+=(int32_t)step;
                    int32_t t=sat(old+m23(now-old,fd));
                    buf[j]=sat(mul31((t*32)&~255,x49)*x48);
                }
            }
            interp(buf,&s,m?s.wx:TOP23-s.wx,w12[n],m);
        }
        v->fade[n]=sat23(v->fade[n]+(int32_t)rate);
        v->u[n]+=b<<PHASE_SH;
    }
    /* MIX ramped over the block: 4 (0.66 W1 + m (W2 - W1)) */
    int32_t target=m23((int32_t)p[1]*2048,0x55252c),w=v->mix,step=(target-w)/(int32_t)N2;
    v->mix=target;
    for(unsigned j=0;j<N2;j++){
        MAC0(w12[0][j],0x547ae100);MAC0((w12[1][j]-w12[0][j])*4,(w>>3)*64);
        w+=step;
        mix[j]=sat(read0()*4);
    }
    finish(v,mix,out);
}

static void half(struct dm_dp_voice *v,unsigned type,const uint16_t *p,int32_t *out)
{
    if(type==DM_DPWAVE)wave_half(v,p,out);
    else if(type==DM_DPDDRW)ddrw_half(v,p,out);
    else dens_half(v,p,out);
    v->fresh=0;
}
void dm_dp_half(struct dm_dp_voice *v,unsigned type,const uint16_t *p,int32_t *out)
{
#if defined(DP_EMAC)
    struct mac_state s;mac_enter(&s);
    half(v,type,p,out);
    mac_leave(&s);
#else
    half(v,type,p,out);
#endif
}

#if defined(DP_EMAC)
static inline int32_t resample(const int32_t *history,const int32_t *co)
{
    int32_t x,c,value;
#define DP_TAP "move.l (%0)+,%3\n\tmac.l %2,%3,(%1)+,%2,%%acc0\n\t"
    __asm__ volatile("move.l (%1)+,%2\n\t"
                     DP_TAP DP_TAP DP_TAP DP_TAP DP_TAP DP_TAP DP_TAP DP_TAP
                     DP_TAP DP_TAP DP_TAP DP_TAP DP_TAP DP_TAP DP_TAP
                     "move.l (%0)+,%3\n\tmac.l %2,%3,%%acc0\n\tmovclr.l %%acc0,%4"
                     :"+a"(co),"+a"(history),"=&d"(x),"=&d"(c),"=d"(value)::"cc","memory");
#undef DP_TAP
    return value>>14;
}
#endif
void dm_dp_render(struct dm_dp_voice *v,unsigned type,const uint16_t *p,int32_t pitch,
                  int32_t previous_pitch,int16_t *out)
{
    int32_t F=dm_dp_word(type,pitch);
    int32_t pre=previous_pitch<0?0:dm_dp_word(DM_DPDDRW,previous_pitch);
#if defined(DP_EMAC)
    struct mac_state s;mac_enter(&s);
#endif
    dm_dp_increments(v,type,p,F,pre);
#if !defined(DM_DPRO_44K)
    /* two blocks at 48 kHz, Q26 to Q15 */
    int32_t pcm[32];
    half(v,type,p,pcm);half(v,type,p,pcm+16);
    for(unsigned i=0;i<32;i++){
        int32_t sample=pcm[i]>>11;
        out[i]=(int16_t)(sample>32767?32767:sample);
    }
#else
    /* FM+'s converter (fmplus.c), the same taps: Q26 blocks become Q17
     * samples, kept << 13 in a doubled ring; 147 source samples for each
     * 160 at 48 kHz. */
    int32_t *ring=v->ring;
    for(unsigned i=0;i<32;i++){
        if(v->need){
            if(v->cursor==16){half(v,type,p,v->pending);v->cursor=0;}
            v->write=(v->write+1)&15u;
            ring[15-v->write]=ring[31-v->write]=(v->pending[v->cursor++]>>9)*8192;
        }
        int32_t sample;
        if(!v->frac)sample=ring[15-((v->write-7)&15u)]>>15;
        else{
#if defined(DP_EMAC)
            sample=resample(ring+15-v->write,dmf_resample_q30+16*v->frac);
#else
            const int32_t *co=dmf_resample+16*v->frac,*h=ring+15-v->write;
            int64_t sum=0;
            for(unsigned j=0;j<16;j++)sum+=smul(h[j]>>13,co[j]);
            sample=(int32_t)(sum>>17);
#endif
        }
        out[i]=(int16_t)(sample>32767?32767:sample< -32768?-32768:sample);
        v->frac+=147;v->need=v->frac>=160;if(v->need)v->frac-=160;
    }
#endif
#if defined(DP_EMAC)
    mac_leave(&s);
#endif
}
