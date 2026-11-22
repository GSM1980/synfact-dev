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
#define N2 (16*DM_DPRO_OS)
#define PHASE_SH (DM_DPRO_OS==2?3:2)
#define NBUF 37
#define ONE (1<<26)
#define TOP (ONE-8)
#define TOP23 0x7fffff
#define M24 0xffffffu

#if defined(__mcoldfire__) && !defined(DM_DP_SPLIT)
#define DP_EMAC 1
struct mac_state {uint32_t sr,acc,ext;};
static inline void mac_enter(struct mac_state *s)
{
    uint32_t clear;
    __asm__ volatile("move.l %%macsr,%0\n\tmove.l #0,%%macsr\n\tmove.l %%acc0,%1\n\t"
                     "move.l %%accext01,%2\n\tmove.l #0x20,%%macsr\n\tmovclr.l %%acc0,%3"
                     :"=&d"(s->sr),"=&d"(s->acc),"=&d"(s->ext),"=&d"(clear)::"cc");
}
static inline void mac_leave(const struct mac_state *s)
{
    __asm__ volatile("move.l #0,%%macsr\n\tmove.l %0,%%acc0\n\tmove.l %1,%%accext01\n\tmove.l %2,%%macsr"
                     ::"d"(s->acc),"d"(s->ext),"d"(s->sr):"cc");
}
#endif

/* ColdFire has no 32x32->64 multiply: exact products from 16-bit limbs.
 * Host builds use the native product; both give the same bits. */
static int64_t smul(int32_t a,int32_t b)
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
/* floor(a x b / 2^31): EMAC fractional (MACSR 0x20, truncating) on target. */
static inline int32_t mul31(int32_t a,int32_t b)
{
#if defined(DP_EMAC)
    int32_t r;
    __asm__ volatile("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0":"=&d"(r):"r"(a),"r"(b):"cc");
    return r;
#else
    return (int32_t)(smul(a,b)>>31);
#endif
}
/* a x c / 2^23, c a Q23 word under 1 (m23) or under 4 (m23w, |a| < 2^29). */
static inline int32_t m23(int32_t a,int32_t c){return mul31(a,c*256);}
static inline int32_t m23w(int32_t a,int32_t c){return mul31(a*4,c*64);}
static inline int32_t sat(int32_t a){return a>TOP?TOP:a< -ONE?-ONE:a;}
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
    uint32_t x0=lim(mpy(0x20b40,exp_lim(F))*4096);
    return x0?(0xac44u<<12)/x0:0;
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
void dm_dp_increments(struct dm_dp_voice *v,unsigned type,const uint16_t *p,int32_t F,int32_t pre)
{
    int64_t A=increment(F);
    uint32_t hi=(uint32_t)(A>>24)&M24,lo=(uint32_t)A&M24;
    if(type==DM_DPWAVE){
        /* A x 0x2f8e (MPYSU, DMACSS), samples << 21 */
        int64_t a=(smul(0x2f8e,(int32_t)lo)*2>>24)+mpy(0x2f8e,hi);
        v->inc[0]=(uint32_t)((((a>>24)&M24)<<21)+((a&M24)>>3));
        unsigned sync=p[4]>>8,sfrq=p[5]>>8;
        if(sync>127)sync=127;
        if(sfrq>127)sfrq=127;
        if((sync<<16)<0x2aaaaau)v->extra=0;
        else if((sync<<16)>=0x555555u)v->extra=sync_period(pre);
        else v->extra=sfrq?sync_period((int32_t)(52*sfrq+0x1000)):0;
    }else if(type==DM_DPDENS){
        v->inc[0]=dmac_ss(SUB,A);
        uint32_t mute=0;
        for(unsigned k=0;k<3;k++){
            unsigned knob=p[k]>>8;
            int64_t inc=dmac_ss64(dpt_pch[knob>127?127:knob],lo,hi)*4;
            uint32_t h=lim(inc);
            if(s24(h)>=0x4e20 || h==hi)mute|=2u<<k;
            v->inc[k+1]=dmac_ss(SUB,inc);
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
            v->inc[n]=dmac_ss(SUB,inc);
            v->inc_b[n]=dmac_ss(SUB_B,inc);
        }
    }
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
static const int32_t wave_coef[10]={
    0x3ce85f,-0x060eaa,-0x6c66a8,0x04d33e,0x36f789,0x11b451,-0x6fdc14,-0x11b451,-0x39ad53,0x0d1e2d};
#define WAVE_GAIN 0x6186
static inline int32_t wave_sample(uint32_t i)
{
    uint32_t w=dpt_waves[i>>1];
    return i&1?(int32_t)(w<<20)>>8:s24(w);           /* Q23: the high sample, or the low << 12 */
}
static void decimate(int32_t *x,const int32_t *in,int32_t *out)
{
    /* dec.py's register program: x the five state words; a, b the two
     * accumulators; y0 the coefficient, y1 the input; two inner rounds (OS 4). */
    const int32_t *c=wave_coef;
    for(unsigned n=0;n<16;n++){
        int32_t a,b,f,y1;
        a=-m23(x[0],c[0]);a-=m23(x[1],c[2]);x[0]=x[1];y1=*in++;
        a=a*2+m23(y1,WAVE_GAIN);
        for(unsigned r=0;r<2;r++){
            b=-m23(x[2],c[4]);b-=m23(x[3],c[6]);x[2]=x[3];
            b*=2;x[1]=sat(a);b+=m23(y1,WAVE_GAIN);
            f=b;b=-m23(x[4],c[8]);x[3]=sat(f);
            b=b*2+m23(y1,WAVE_GAIN);
            a=-m23(x[0],c[0]);a-=m23(x[1],c[2]);x[0]=x[1];y1=*in++;
            a*=2;x[4]=sat(b);a+=m23(y1,WAVE_GAIN);
        }
        b=-m23(x[2],c[4]);b-=m23(x[3],c[6]);x[2]=x[3];
        b*=2;x[1]=sat(a);b+=m23(y1,WAVE_GAIN);
        f=b;b=-m23(x[4],c[8]);x[3]=sat(f);
        b=b*2+m23(y1,WAVE_GAIN);
        a=-m23(x[0],c[0]);x[4]=sat(b);
        b=m23(x[0],c[1]);
        a-=m23(x[1],c[2]);x[0]=x[1];
        a*=2;y1=*in++;
        a+=m23(y1,WAVE_GAIN);
        b+=m23(x[1],c[3]);
        f=a;a=-m23(x[2],c[4]);x[1]=sat(f);
        b+=m23(x[2],c[5]);
        a-=m23(x[3],c[6]);x[2]=x[3];
        a*=2;a+=m23(y1,WAVE_GAIN);
        b+=m23(x[3],c[7]);
        f=a;a=-m23(x[4],c[8]);x[3]=sat(f);
        a*=2;a+=m23(y1,WAVE_GAIN);
        b+=m23(x[4],c[9]);
        x[4]=sat(a);
        *out++=b>=ONE/32?TOP:b< -ONE/32?-ONE:b*32;      /* ASL 5, limited */
    }
}
static void wave_half(struct dm_dp_voice *v,const uint16_t *p,int32_t *out)
{
    int32_t buf[64];
    if((int32_t)v->extra>0 && ++v->sync>=(int32_t)v->extra){v->sync=0;v->u[0]=0;}
    int32_t x;
    v->pos=(v->pos+(int32_t)(p[2]>>8)*64)&(int32_t)M24;
    x=v->pos+(int32_t)(p[1]>>8)*65536;
    if(x>0x7fffff){x=0xffffff-x;if(x<0)x=-x;}
    uint32_t len=((uint32_t)x>>14)+0x200,hlf=len>>1,end=len<<21;
    uint32_t base=(p[0]>>10)*512u,ph=v->u[0],inc=v->inc[0];
    for(unsigned n=0;n<64;n++){
        uint32_t i=ph>>21,h=i&1,nb=(i>>1)+h;
        if(nb>=hlf)nb-=256;
        int32_t t0=wave_sample(base+i),t1=wave_sample(base+2*nb+1-h);
        int32_t s=t0*8+mul31((t1-t0)*8,(int32_t)(ph&0x1fffffu)<<10);
        buf[n]=sat(s*2);
        ph+=inc;
        if(ph>=end)ph-=512u<<21;
    }
    v->u[0]=ph;
    decimate(v->dec,buf,out);
}

/* -- DDRW and DENS: a Digibank level read at N2, the kernel's interpolator ---- */
struct level {uint32_t sh,msk,boff,il,fr,i0;int32_t wx;};
static void start(struct level *s,uint32_t bs,uint32_t b,uint32_t u,uint32_t hf)
{
    /* dpro._start: the mip level from the 2x increment's exponent, its
     * crossfade over each octave's top 1/16, and the read's start. */
    uint32_t x=bs>>8,n=0,m;
    int32_t sh;
    if(x){uint32_t t=x;while(t>>=1)n++;}
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
}
static void interp(const int32_t *buf,const struct level *s,int32_t g,int32_t *out,int add)
{
    uint32_t P=s->fr;
    for(unsigned k=0;k<N2;k++){
        uint32_t r=(P&M24)+0x800000u;
        if(r>=1u<<24)r-=1u<<24;
        const int32_t *c=dpt_interp+10*(r>>19),*x=buf+(P>>24);
        int32_t res=(int32_t)(r&0x7ffffu)<<12,sum=0;
        for(unsigned j=0;j<5;j++)sum+=m23w(x[j],c[2*j]+mul31(c[2*j+1],res));
        sum=m23(sat(sum),g);
        out[k]=add?out[k]+sum:sum;
        P+=s->il;
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
    const uint32_t *wave=dpt_digibank+(((p[3]>>9)&31u)<<11);
    for(unsigned k=0;k<4;k++){
        uint32_t bs=v->inc[k],b=scaled(bs);
        if(!(v->extra&(1u<<k))){
            struct level s;
            start(&s,bs,b,v->u[k],0x800000u);
            int32_t y1=s.sh?s.wx:0,w=TOP23-y1;
            const uint32_t *cb=wave+s.boff,*fb=s.sh?cb-2*(s.msk+1):cb;
            uint32_t i0=s.i0;
            if(!y1){
                /* 15/16 of every octave: the level alone (x TOP: under one Q23 LSB) */
                for(unsigned j=0;j<NBUF;j++)buf[j]=s24(cb[i0++&s.msk])*8;
            }else for(unsigned j=0;j<NBUF;j++){
                uint32_t idx=i0++&s.msk;
                int32_t level=s24(cb[idx])*8;
                int32_t partner=(int32_t)(((fb[2*idx]&255u)<<24)|((fb[2*idx+1]&255u)<<16))>>5;
                buf[j]=sat(m23(level,w)+m23(partner,y1));
            }
            interp(buf,&s,DENS_GAIN,mix,k!=0);
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
    uint32_t rate=dpt_rate[(p[3]>>8)>127?127:p[3]>>8];
    if(v->fresh){
        for(unsigned n=0;n<2;n++){v->now[n]=v->old[n]=p[2*n]>>9;v->fade[n]=TOP23;}
        v->mix=m23w((int32_t)p[1]*2048,0x810204);
    }
    for(unsigned n=0;n<2;n++){
        uint32_t br=p[4+2*n]>>8;
        if(br>127)br=127;
        if(br)br=64+((63*br+63)>>7);                    /* De-Pattern's BR mapping */
        int32_t x48=(int32_t)dpt_br[2*br],x49=(int32_t)dpt_br[2*br+1];
        int32_t wav=p[2*n]>>9;
        uint32_t b2=v->inc[n],b=scaled(b2);
        if(wav!=v->now[n] && v->fade[n]>=0x7fff00){v->old[n]=v->now[n];v->now[n]=wav;v->fade[n]=0;}
        for(unsigned m=0;m<2;m++){
            struct level s;
            start(&s,m?v->inc_b[n]:b2,b,v->u[n],m?0x880000u:0x800000u);
            if(m && !s.wx)continue;                    /* level B adds exactly 0 here */
            const uint32_t *cb=dpt_digibank+(((uint32_t)v->now[n]&31u)<<11)+s.boff;
            const uint32_t *fb=dpt_digibank+(((uint32_t)v->old[n]&31u)<<11)+s.boff;
            uint32_t step=fade_step(rate,s.il),i0=s.i0;
            int32_t f=v->fade[n];
            if(f>=TOP23)for(unsigned j=0;j<NBUF;j++){
                /* the fade is over: the new wave alone (x TOP: under one Q23 LSB) */
                int32_t t24=mul31((s24(cb[i0++&s.msk])*256)&~255,x49);
                buf[j]=sat(t24*x48*4);
            }else for(unsigned j=0;j<NBUF;j++){
                uint32_t idx=i0++&s.msk;
                int32_t now=s24(cb[idx])*8,old=s24(fb[idx])*8,fd=sat23(f);
                f+=(int32_t)step;
                int32_t t=sat(old+m23(now-old,fd));
                /* the reducer: floor(t24 x X49 / 2^23), then x X48 / 2^24 */
                int32_t t24=mul31((t*32)&~255,x49);
                buf[j]=sat(t24*x48*4);
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
        int32_t d=m23w(w12[1][j]-w12[0][j],w>>3);
        w+=step;
        mix[j]=sat((m23(w12[0][j],0x547ae1)+d)*4);
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
    /* FM+'s converter (fmplus.c), the same taps: Q26 blocks become Q17
     * history; 147 source samples for each 160 at 48 kHz. */
    int32_t F=dm_dp_word(type,pitch);
    int32_t pre=previous_pitch<0?0:dm_dp_word(DM_DPDDRW,previous_pitch);
#if defined(DP_EMAC)
    struct mac_state s;mac_enter(&s);
    int32_t ring[32];
    for(unsigned j=0;j<16;j++)ring[15-j]=ring[31-j]=v->history[j]*8192;
#endif
    dm_dp_increments(v,type,p,F,pre);
    for(unsigned i=0;i<32;i++){
        if(v->need){
            if(v->cursor==16){half(v,type,p,v->pending);v->cursor=0;}
            v->write=(v->write+1)&15u;v->history[v->write]=v->pending[v->cursor++]>>9;
#if defined(DP_EMAC)
            ring[15-v->write]=ring[31-v->write]=v->history[v->write]*8192;
#endif
        }
#if defined(DP_EMAC)
        int32_t sample=!v->frac?v->history[(v->write-7)&15u]>>2:
                       resample(ring+15-v->write,dmf_resample_q30+16*v->frac);
#else
        const int32_t *co=dmf_resample+16*v->frac;
        int64_t sum=0;
        for(unsigned j=0;j<16;j++)sum+=smul(v->history[(v->write-j)&15u],co[j]);
        int32_t sample=(int32_t)(sum>>17);
#endif
        out[i]=(int16_t)(sample>32767?32767:sample< -32768?-32768:sample);
        v->frac+=147;v->need=v->frac>=160;if(v->need)v->frac-=160;
    }
#if defined(DP_EMAC)
    mac_leave(&s);
#endif
}
