/* SPDX-License-Identifier: AGPL-3.0-or-later
 * FM+ STAT/PAR/DYN from De-Pattern a0249fc (fmplus/provenance.json).
 * Integer ColdFire port of fm.py; native 44.1 kHz / 16 frames. Balanced:
 * ramped operators/carrier at 1x, sensitive self-FM operator at original 2x.
 * DM_FM_HQ retains the full 2x reference and original allpass decimator.
 * Both use the original sine/ratios/TONE/envelope tables.
 * Syntakt's AMP/FLTR/FX remain downstream. No float/library calls on target.
 */
#include "fmplus/voice.h"
#include "fmplus/tables.h"
#define ONE (1<<27)
#define TOP (ONE-16)
#define SIGNAL (1<<29)
#define SIGNAL_TOP (SIGNAL-64)
#if defined(DM_FM_HQ)
#define FM_STEPS 32
#else
#define FM_STEPS 16
#endif

#if defined(__mcoldfire__) && !defined(DM_FM_SPLIT)
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

/* Exact signed 32x32 product, explicitly split for a CPU without MUL.L hi:lo.
 * Host oracle builds can select the native wide operation independently. */
static inline int64_t product(int32_t a,int32_t b)
{
#if defined(__mcoldfire__) && !defined(DM_FM_SPLIT)
    /* MACSR=0x20: signed fractional, truncating. ACC0 supplies p>>31;
     * the integer multiplier supplies p's low word. Together these retain
     * every bit of the original 64-bit product. FM operands stay below the
     * fractional unit's +/-2^62 product limit. Half-block guard owns ACC0. */
    int32_t top;
    __asm__ volatile("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0"
                     :"=&d"(top):"r"(a),"r"(b):"cc");
    return (int64_t)(((uint64_t)(uint32_t)(top>>1)<<32)|((uint32_t)a*(uint32_t)b));
#elif defined(DM_FM_NATIVE_WIDE)
    return (int64_t)a*b;
#else
    uint32_t x=(uint32_t)a,y=(uint32_t)b;
    uint32_t x0=x&65535u,x1=x>>16,y0=y&65535u,y1=y>>16;
    uint32_t low=x0*y0,mid=x1*y0+(low>>16),carry=x0*y1+(mid&65535u);
    uint32_t hi=x1*y1+(mid>>16)+(carry>>16);
    if(a<0)hi-=y;if(b<0)hi-=x;
    return (int64_t)(((uint64_t)hi<<32)|((carry<<16)|(low&65535u)));
#endif
}
static inline int32_t mul(int32_t a,int32_t b,unsigned shift)
{return (int32_t)(product(a,b)>>shift);}
/* Hot bounded coefficients can be aligned to Q31 before multiplying.
 * EMAC then returns the complete required result in two instructions:
 * there is no discarded guard bit and no low-word reconstruction. */
static inline int32_t mul31(int32_t a,int32_t b)
{
#if defined(__mcoldfire__) && !defined(DM_FM_SPLIT)
    int32_t result;
    __asm__ volatile("mac.l %1,%2,%%acc0\n\tmovclr.l %%acc0,%0"
                     :"=&d"(result):"r"(a),"r"(b):"cc");
    return result;
#else
    return (int32_t)(product(a,b)>>31);
#endif
}
static inline int32_t q23(int32_t a,int32_t coefficient)
{
    if(__builtin_constant_p(coefficient) && coefficient==0x400000)return a>>1;
    return mul31(a,coefficient*256);
}
static inline int32_t q27(int32_t a,int32_t gain){return mul31(a,gain*16);}
static inline int32_t sat(int32_t a){return a>TOP?TOP:a< -ONE?-ONE:a;}
static inline int32_t clip(int32_t a){return a>SIGNAL_TOP?SIGNAL_TOP:a< -SIGNAL?-SIGNAL:a;}
static inline int32_t clip64(int64_t a){return a>SIGNAL_TOP?SIGNAL_TOP:a< -SIGNAL?-SIGNAL:(int32_t)a;}
static inline int32_t amp_mix(int32_t prior,int32_t signal,int32_t fb)
{
#if defined(__mcoldfire__) && !defined(DM_FM_SPLIT)
    int32_t top=mul31(signal,fb);
    if(top>=SIGNAL/8)return SIGNAL_TOP;
    if(top< -SIGNAL/8)return -SIGNAL;
    uint32_t low=(uint32_t)signal*(uint32_t)fb;
    return clip(prior+top*16+(int32_t)((low>>27)&15u));
#else
    return clip64(prior+(product(signal,fb)>>27));
#endif
}
static inline int32_t feedback_offset(int32_t signal,int32_t amount)
{
#if defined(__mcoldfire__) && !defined(DM_FM_SPLIT)
    int32_t top=mul31(signal,amount*65536);
    uint32_t low=(uint32_t)signal*(uint32_t)amount;
    int32_t value=top*4+(int32_t)((low>>13)&3u);
    return value+(signal<0 && (low&8191u));
#else
    int64_t off=product(signal,amount);
    return (int32_t)(off>=0?off>>13:-((-off)>>13));
#endif
}
static inline int32_t floor23(int32_t a){return (a>>6)*64;}
static inline int32_t sin_at(uint32_t phase)
{
    unsigned i=phase>>19;
    /* Adjacent original sine words differ by less than 8192, so <<18 fits. */
#if defined(__mcoldfire__) && !defined(DM_FM_SPLIT)
    return dmf_sine_q29[i]+mul31((dmf_sine_q29[i+1]-dmf_sine_q29[i])*4096,(int32_t)(phase&0x7ffffu));
#else
    return dmf_sine[i]*64+mul31((dmf_sine[i+1]-dmf_sine[i])*262144,(int32_t)(phase&0x7ffffu));
#endif
}
static inline int32_t square_level(int32_t x)
{return x>=ONE/2?TOP:mul31(x*8,x*8);}
static inline int32_t knob(const uint16_t *p,unsigned k){return p[k]*4096;}
static int32_t ratio(const uint16_t *p,unsigned k)
{return dmf_ratios[(6u*(p[k]>>8)+3u)>>5];}
static int32_t dyn_frq(const uint16_t *p,unsigned k)
{return p[k]>=0x7f00?TOP:knob(p,k);}
static int32_t envt(const uint16_t *p,unsigned k)
{int v=(p[k]>>8)-64;if(v<0)v=-v;return dmf_envt[128-2*v];}
static int32_t ampfb(const uint16_t *p,unsigned k)
{int32_t v=knob(p,k)-ONE/2;return v>0?v*16:0;}
static int32_t level(struct dm_fm_voice *v,const uint16_t *p,unsigned k,unsigned env)
{
    unsigned n=p[k]>>8;
    if(n>=64){unsigned e=n-64,e2=e*e;int32_t d=0x7fffff-(int32_t)((e2*e2+31u)>>5);
        v->env[env]=q23(v->env[env],d);}
    int32_t x=knob(p,k);
    return q23(square_level(x),v->env[env]);
}
void dm_fm_init(struct dm_fm_voice *v)
{
    /* One caller-owned voice: no global mutable oscillator state. */
    unsigned char *b=(unsigned char *)v;
    for(unsigned i=0;i<sizeof(*v);i++)b[i]=0;
    v->cursor=16;v->need=1;
}
void dm_fm_trig(struct dm_fm_voice *v,unsigned type,const uint16_t *p)
{
    /* Original trig resets envelopes only. Oscillator/filter phases continue. */
    for(unsigned i=0;i<3;i++)v->env[i]=0x7fffff;
    if(type==11){
#if defined(__mcoldfire__) && !defined(DM_FM_SPLIT)
        struct mac_state s;mac_enter(&s);
#endif
        int32_t b=knob(p,3)-ONE/2;
        if(b<0)b=mul(b,knob(p,2),27);
        v->env[1]=sat(b*4)>>4;
        b=knob(p,1)-ONE/2;if(b<0)b>>=1;
        v->env[2]=mul(b,b<0?-b:b,27)>>4;
#if defined(__mcoldfire__) && !defined(DM_FM_SPLIT)
        mac_leave(&s);
#endif
    }
}
static void ramped(struct dm_fm_op *o,int32_t inc,int32_t c,int32_t fb,int32_t *out)
{
    int32_t a=o->ramp,step=(int32_t)((uint32_t)inc-(uint32_t)a)>>5;
    uint32_t phase=o->phase;
    int32_t previous=o->prior,lp=o->lp,delayed=o->fb;
    o->ramp=inc;
    for(unsigned i=0;i<FM_STEPS;i++){
        a=(int32_t)((uint32_t)a+(uint32_t)step);
        int32_t s=sin_at(phase);phase+=(uint32_t)a;
#if !defined(DM_FM_HQ)
        /* Keep the original pair of phase increments and filter substeps,
         * approximating their two input differences by the mean slope. */
        a=(int32_t)((uint32_t)a+(uint32_t)step);phase+=(uint32_t)a;
        int32_t d=clip((s-previous)>>1);previous=s;
#else
        int32_t d=clip(s-previous);previous=s;
#endif
        int32_t prior=lp;lp+=q23(d-lp,c);
        out[i]=fb?amp_mix(prior,delayed,fb):prior;
#if !defined(DM_FM_HQ)
        delayed=lp;lp+=q23(d-lp,c);
#else
        delayed=prior;
#endif
    }
    o->phase=phase;o->prior=previous;o->lp=floor23(lp);o->fb=delayed;
}
static void feedback(struct dm_fm_op *o,int32_t inc,int32_t c,int32_t amount,int32_t *out)
{
    /* Self-FM is sensitive to its one-subsample feedback delay: retain all
     * 32 original samples, even when the carrier is evaluated at 1x. */
    uint32_t phase=o->phase;
    int32_t previous=o->prior,lp=o->lp,delayed=o->fb;
#if defined(__mcoldfire__) && !defined(DM_FM_SPLIT)
#pragma GCC unroll 4
#endif
    for(unsigned i=0;i<32;i++){
        phase+=(uint32_t)inc;
#if defined(__mcoldfire__) && !defined(DM_FM_SPLIT)
        int32_t s=dmf_sine_q29[(phase+(uint32_t)delayed)>>19];
#else
        int32_t s=dmf_sine[(phase+(uint32_t)delayed)>>19]*64;
#endif
        /* Truncate toward zero, as the SHARC TRUNC used for the offset. */
        delayed=feedback_offset(s,amount);
        int32_t d=clip(s-previous);previous=s;
        out[i]=lp;lp+=q23(d-lp,c);
    }
    o->phase=phase;o->prior=previous;o->lp=floor23(lp);o->fb=delayed;
}
static void carrier(struct dm_fm_voice *v,int32_t inc,int32_t c,
                    int32_t ops[3][32],const int32_t *g,unsigned n,int32_t *out)
{
    int32_t a=v->ramp,step=(int32_t)((uint32_t)inc-(uint32_t)a)>>5;v->ramp=inc;
    uint32_t phase=v->phase;int32_t lp=v->lp;
    for(unsigned i=0;i<FM_STEPS;i++){
#if defined(DM_FM_HQ)
        int32_t mix=clip(q27(ops[0][i],g[0])+q27(ops[1][i],g[1]));
#else
        int32_t first=q27(ops[0][i],g[0]);
        int32_t mix=clip(first+q27(ops[1][n==2?2*i:i],g[1]));
#endif
        if(n==3)mix=clip(mix+q27(ops[2][i],g[2]));
        int32_t prior=lp;lp+=q23(mix-lp,c);
        a=(int32_t)((uint32_t)a+(uint32_t)step);
        out[i]=sin_at(phase)>>2;
        phase+=(uint32_t)a+(uint32_t)prior*2u;
#if !defined(DM_FM_HQ)
        /* Integrate both original self-FM samples; holding only its even
         * sample changed the high-feedback spectrum substantially. */
        if(n==2)mix=clip(first+q27(ops[1][2*i+1],g[1]));
        prior=lp;lp+=q23(mix-lp,c);
        a=(int32_t)((uint32_t)a+(uint32_t)step);
        phase+=(uint32_t)a+(uint32_t)prior*2u;
#endif
    }
    v->phase=phase;v->lp=floor23(lp);
}
#if defined(DM_FM_HQ)
static void decimate(struct dm_fm_voice *v,const int32_t *in,int32_t *out)
{
    static const int32_t co[4]={0x2bc9ca,0x0cfce3,0x6f0f12,0x4e63df};
    int32_t *s=v->dec;
    for(unsigned i=0;i<16;i++){
        int32_t e=in[2*i],o=in[2*i+1];
        int32_t a=clip(s[0]+q23(e-s[2],co[0]));s[0]=e;
        int32_t b=clip(s[1]+q23(o-s[3],co[1]));s[1]=o;
        s[4]=clip(s[2]+q23(a-s[4],co[2]));s[2]=a;
        s[5]=clip(s[3]+q23(b-s[5],co[3]));s[3]=b;
        out[i]=(s[4]+s[5])>>2;
    }
}
#endif
static void half(struct dm_fm_voice *v,unsigned type,const uint16_t *p,int32_t pitch,int32_t *out)
{
    if(pitch<0)pitch=0;if(pitch>140*64)pitch=140*64;
    int32_t inc=dmf_inc[pitch],c=type==11?0x400000:dmf_tone[p[6]>>8];
    int32_t ops[3][32],raw[FM_STEPS],g[3];unsigned n=2;
    if(type==9){
        int32_t i2=mul(inc,ratio(p,4),20);
        ramped(&v->op[0],i2,c,ampfb(p,5),ops[0]);
        int32_t fine=ONE/2+(knob(p,1)-ONE/2)/4;
        int32_t i1=mul(inc,sat(mul(fine,ratio(p,0),22)),24);
        feedback(&v->op[1],i1,c,p[3],ops[1]);
        g[0]=square_level(knob(p,5));g[1]=level(v,p,2,0);
    }else if(type==10){
        n=3;
        for(unsigned j=0;j<3;j++){
            ramped(&v->op[j],mul(inc,ratio(p,2*j),20),c,ampfb(p,2*j+1),ops[j]);
            g[j]=level(v,p,2*j+1,j);
        }
    }else{
        /* DYN's signed volume/frequency envelopes decay in the original Q23. */
        int32_t f=q23(v->env[2]*16,envt(p,1));v->env[2]=f>>4;
        int32_t delta=(int32_t)((uint32_t)inc+(uint32_t)f*8u);
        int32_t i1=mul(delta,dyn_frq(p,0),26);
        int32_t ve=q23(v->env[1]*16,envt(p,3));v->env[1]=ve>>4;
        int32_t fb=sat(knob(p,2)+ve-ONE/2);fb=fb>0?fb*16:0;
        ramped(&v->op[0],i1,0x400000,fb,ops[0]);
        int32_t r=dyn_frq(p,4);int32_t i2=mul(inc,mul(r,r,27),25);
        feedback(&v->op[1],i2,0x400000,p[6],ops[1]);
        g[0]=sat((mul(knob(p,2),knob(p,2),27)+v->env[1]*16)*4);
        g[1]=level(v,p,5,0);
    }
    if(type==11)carrier(v,inc,0x400000,ops,g,n,raw);
    else carrier(v,inc,c,ops,g,n,raw);
#if defined(DM_FM_HQ)
    decimate(v,raw,out);
#else
    /* Full decimation has DC gain one half relative to raw[]. Keep that
     * gain at 1x; the 16-tap 44.1->48 kHz converter remains downstream. */
    for(unsigned i=0;i<16;i++)out[i]=raw[i]>>1;
#endif
}
void dm_fm_half(struct dm_fm_voice *v,unsigned type,const uint16_t *p,int32_t pitch,int32_t *out)
{
#if defined(__mcoldfire__) && !defined(DM_FM_SPLIT)
    struct mac_state s;mac_enter(&s);
    half(v,type,p,pitch,out);
    mac_leave(&s);
#else
    half(v,type,p,pitch,out);
#endif
}
#if defined(__mcoldfire__) && !defined(DM_FM_SPLIT)
static inline int32_t resample(const int32_t *history,const int32_t *co)
{
    int32_t x,c,value;
    /* A fixed 16-tap dot product: contiguous, prealigned operands avoid
     * ring-index arithmetic and repeated shifts in the sample loop. */
#define FM_TAP "move.l (%0)+,%3\n\tmac.l %2,%3,(%1)+,%2,%%acc0\n\t"
    __asm__ volatile("move.l (%1)+,%2\n\t"
                     FM_TAP FM_TAP FM_TAP FM_TAP FM_TAP FM_TAP FM_TAP FM_TAP
                     FM_TAP FM_TAP FM_TAP FM_TAP FM_TAP FM_TAP FM_TAP
                     "move.l (%0)+,%3\n\tmac.l %2,%3,%%acc0\n\tmovclr.l %%acc0,%4"
                     :"+a"(co),"+a"(history),"=&d"(x),"=&d"(c),"=d"(value)::"cc","memory");
#undef FM_TAP
    return value>>14;
}
#endif
void dm_fm_render(struct dm_fm_voice *v,unsigned type,const uint16_t *p,int32_t pitch,int16_t *out)
{
#if defined(__mcoldfire__) && !defined(DM_FM_SPLIT)
    struct mac_state s;mac_enter(&s);
    int32_t ring[32];
    for(unsigned j=0;j<16;j++)ring[15-j]=ring[31-j]=v->history[j]*8192;
#endif
    for(unsigned i=0;i<32;i++){
        if(v->need){
            if(v->cursor==16){half(v,type,p,pitch,v->pending);v->cursor=0;}
            /* Decimator output is bounded by 0.5. Q17 history x Q15 taps
             * fits signed 32-bit, avoiding 512 wide multiplies per render.
             * Two fractional guard bits remain above the final Q15 output. */
            v->write=(v->write+1)&15u;v->history[v->write]=v->pending[v->cursor++]>>12;
#if defined(__mcoldfire__) && !defined(DM_FM_SPLIT)
            ring[15-v->write]=ring[31-v->write]=v->history[v->write]*8192;
#endif
        }
#if defined(__mcoldfire__) && !defined(DM_FM_SPLIT)
        int32_t value;
        if(!v->frac)value=v->history[(v->write-7)&15u]>>2;
        else{
            /* Q17 history <<13, Q15 taps <<15: every product is aligned
             * by 28 bits. The EMAC's 48-bit accumulator therefore retains
             * the exact sum; its read is sum>>3, then >>14 is Q15 PCM.
             * The scale also represents +32768 taps without overflow. */
            value=resample(ring+15-v->write,dmf_resample_q30+16*v->frac);
        }
        int32_t sample=value;
#else
        const int32_t *co=dmf_resample+16*v->frac;
        int64_t sum=0;
        for(unsigned j=0;j<16;j++)sum+=(int64_t)(v->history[(v->write-j)&15u]*co[j]);
        int32_t sample=(int32_t)(sum>>17);
#endif
        out[i]=(int16_t)(sample>32767?32767:sample< -32768?-32768:sample);
        v->frac+=147;v->need=v->frac>=160;if(v->need)v->frac-=160;
    }
#if defined(__mcoldfire__) && !defined(DM_FM_SPLIT)
    mac_leave(&s);
#endif
}
