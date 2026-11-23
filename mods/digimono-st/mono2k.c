/* SPDX-License-Identifier: GPL-3.0-only
 * SID/ENS DSP adapted from MONO2K by Inigo Hernandez, pinned in mono2k/upstream.
 * FM+ and DigiPRO public IDs dispatch to De-Pattern's fmplus.c and dpro.c.
 * Only the oscillator/its internal envelopes run here; native Syntakt AMP,
 * FLTR, modulation and effects remain downstream. 48 kHz, 2 x 16 samples.
 */
#include "mono2k/voice.h"
#include "mono2k/tables.h"
#define M2K_SUB 16
#define P_SYN 0
#define M_SID DM_SID
#define M_FMS DM_FMSTAT
#define M_FMP DM_FMPAR
#define M_FMD DM_FMDYN
#define M_ENS DM_ENS
#define ENS_LEN 1024u
#define ENS_GAIN 7119
#define SID_A 24950
#define SID_MIX0 1024
#define SID_MIXA (SID_A * 15 / 32)
#define P8(t,id) ((int32_t)p[id])
#define P7(t,id) (P8(t,id)>>8)
/* Common Syntakt TUNE is already included in pitch, in 1/64 semitones. */
#define PB8(t,id) 0
/* ColdFire has no 32x32->64 integer multiply instruction. Split products into
 * 16-bit limbs, with explicit carries; no libgcc or FPU dependency. */
static inline uint64_t wide_mul(uint32_t a,uint32_t b)
{
    uint32_t a0=a&65535u,a1=a>>16,b0=b&65535u,b1=b>>16;
    uint32_t x=a0*b0,y=a1*b0+(x>>16),z=a0*b1+(y&65535u);
    uint32_t lo=(z<<16)|(x&65535u),hi=a1*b1+(y>>16)+(z>>16);
    return ((uint64_t)hi<<32)|lo;
}
static inline uint32_t mulu_hi(uint32_t a,uint32_t b,int sh) {return (uint32_t)(wide_mul(a,b)>>sh);}
static inline int32_t sat16(int32_t x) {return x>32767?32767:x< -32767? -32767:x;}
static inline int32_t sinp(uint32_t ph)
{
    uint32_t i=ph>>22,f=(ph>>6)&65535u;
    int32_t a=T_SIN[i],b=T_SIN[i+1];
    return a+(((b-a)*(int32_t)f)>>16);
}
static inline uint32_t inc_of(int32_t pq6)
{
    if(pq6<0)pq6=0;
    if(pq6>140*64)pq6=140*64;
    int32_t oct=pq6/768,r=pq6-oct*768,sh=10-oct;
    if(sh>=0)return T_INC_TOP[r]>>sh;
    uint32_t v=T_INC_TOP[r];
    return v>(0x7fffffffu>>(-sh-1))?0x7fffffffu:v<<-sh;
}
static inline int32_t tq_u16(const uint16_t *table,int32_t q8)
{
    int32_t a=table[q8>>8],f=q8&255;
    return f?a+(((int32_t)table[(q8>>8)+1]-a)*f>>8):a;
}
static inline int32_t blep_saw(uint32_t ph,uint32_t inc,uint32_t inv)
{
    int32_t s=(int32_t)(ph>>16)-32768;
    if(ph<inc){
        uint32_t t=(ph*inv)>>16;
        int32_t v=2*(int32_t)t-(int32_t)((t*t)>>16)-65536;
        s-=v>>1;
    }else if(~ph<inc){
        uint32_t u=(~ph*inv)>>16,w=65536-u;
        s-=(int32_t)((w*w)>>16)>>1;
    }
    return s;
}
/* Preserve PWAD's sweep speed in seconds at 48 kHz (upstream: 44.1 kHz).
 * Other PK uses are dimensionless pitch offsets and are unchanged. */
#define PK(t,id,k) ((id)==1 && (k)==64 ? (P8(t,id)*64*441+61440)/122880 : (P8(t,id)*(k))>>8)

void dm_m2k_init(struct dm_m2k_voice *v)
{
    dm_fm_init(&v->fm);
    dm_dp_init(&v->dp);
    unsigned i;
    for(i=0;i<4;i++)v->ph[i]=0;
    for(i=0;i<6;i++)v->ms[i]=0;
    for(i=0;i<3;i++)v->menv[i]=1u<<30;
    v->lfsr=0x7ffff8u;
}
void dm_m2k_trig(struct dm_m2k_voice *v,unsigned type,const uint16_t *p)
{
    if(DM_IS_DPRO(type)){dm_dp_trig(&v->dp,type,p);return;}
    if(type>=DM_FMSTAT){dm_fm_trig(&v->fm,type,p);return;}
    if(type==DM_SID){if(p[2]>=64u*256u)v->ms[4]=0;return;}
    dm_m2k_init(v);
    v->ph[0]=1u<<31;
}
static void dm_m2k_block(struct dm_m2k_voice *t,unsigned type,const uint16_t *p,
                         int32_t pitch,int32_t previous_pitch,int16_t *o)
{
    switch(type){
#include "mono2k/machines.inc"
    default:for(unsigned i=0;i<M2K_SUB;i++)o[i]=0;break;
    }
}
void dm_m2k_render(struct dm_m2k_voice *v,unsigned type,const uint16_t *p,
                   int32_t pitch,int32_t previous_pitch,int16_t *out)
{
    if(DM_IS_DPRO(type)){dm_dp_render(&v->dp,type,p,pitch,previous_pitch,out);return;}
    if(type>=DM_FMSTAT){dm_fm_render(&v->fm,type,p,pitch,out);return;}
    dm_m2k_block(v,type,p,pitch,previous_pitch,out);
    dm_m2k_block(v,type,p,pitch,previous_pitch,out+M2K_SUB);
}

static inline int32_t add_sh15(int32_t a,int32_t b)
{return (a>>15)+(b>>15)+(((a&32767)+(b&32767))>>15);}

void dm_ens_init(struct dm_ens_voice *v)
{
    unsigned i;
    for(i=0;i<6;i++){v->ph[i]=0;v->ms[i]=0;}
    for(i=0;i<ENS_LEN;i++)v->line[i]=0;
}
void dm_ens_trig(struct dm_ens_voice *v)
{
    /* The four voices restart together. The chorus clock/line keep running
     * through a retrigger, as upstream mach_trig; native Syntakt declick follows. */
    for(unsigned i=0;i<4;i++)v->ph[i]=0;
}
static void dm_ens_block(struct dm_ens_voice *t,const uint16_t *p,int32_t pitch,
                         int16_t *o,int16_t *side)
{
    switch(DM_ENS){
#include "mono2k/ens.inc"
    }
}
void dm_ens_render(struct dm_ens_voice *v,const uint16_t *p,int32_t pitch,int16_t *left,int16_t *right)
{
    int16_t mid[32],side[32];
    dm_ens_block(v,p,pitch,mid,side);
    dm_ens_block(v,p,pitch,mid+M2K_SUB,side+M2K_SUB);
    for(unsigned i=0;i<32;i++){
        left[i]=(int16_t)sat16((int32_t)mid[i]+side[i]);
        right[i]=(int16_t)sat16((int32_t)mid[i]-side[i]);
    }
}

#if defined(DM_DENS_CHORUS_FULL)
static void chorus_full(struct dm_ens_voice *t,const uint16_t *p,int16_t *left,int16_t *right)
{
    /* ens.inc's chorus after DPRO-DENS (CHRL p[5], CHRW p[6]): the
     * Monomachine's DENS has the same CHRL/CHRW chorus after the machine.
     * The same line, taps, mix and spread; the 0.2 Hz LFO's delays are
     * computed at the block's first and last samples and taken linearly
     * between them (they move under 0.2 of a line sample a block). ENS keeps
     * ens.inc's per-sample LFO. */
    int32_t chrl = P8(t, P_SYN + 5), chrw = P8(t, P_SYN + 6);
    int32_t dry = 32768 - (chrl * 27197) / (127 * 256), wet = (chrl * 27853) / (127 * 256);
    wet = (wet * tq_u16(T_ENS_G, chrw)) >> 15;
    int32_t wside = (wet * tq_u16(T_ENS_W, chrw)) >> 15;
    int16_t *cm = t->line;
    uint32_t ph = t->ph[4];
    t->ph[4] = ph + 32 * ENS_CINC;
    int32_t d[2], step[2];
    for (int c = 0; c < 2; c++) {
        int32_t a = (2 << 8) + ENS_CDEP + ((ENS_CDEP * sinp(ph + ENS_CINC + ((uint32_t)c << 30))) >> 15);
        int32_t b = (2 << 8) + ENS_CDEP + ((ENS_CDEP * sinp(ph + 32 * ENS_CINC + ((uint32_t)c << 30))) >> 15);
        d[c] = a << 16;                        /* 16 fraction bits */
        step[c] = ((b - a) << 16) / 31;
    }
    for (unsigned i = 0; i < 32; i++) {
        int32_t s = left[i], side = 0;
        t->ms[0] += s;
        if (i & 1) { cm[t->ph[5]++ & (ENS_LEN - 1)] = (int16_t)sat16(t->ms[0] >> 1); t->ms[0] = 0; }
        if (chrl) {
            int32_t y[2];
            uint32_t at = (t->ph[5] << 8) + ((uint32_t)(i & 1) << 7);
            for (int c = 0; c < 2; c++) {
                uint32_t pos = at - (uint32_t)(d[c] >> 16);
                uint32_t j = pos >> 8;
                int32_t f = (int32_t)(pos & 255), y0 = cm[j & (ENS_LEN - 1)];
                y[c] = y0 + (((cm[(j + 1) & (ENS_LEN - 1)] - y0) * f) >> 8);
                d[c] += step[c];
            }
            side = sat16(((y[0] - y[1]) * wside) >> 16);
            /* add_sh15's sum without its split: s dry and the wet term are each
             * under 2^30 (|s| < 2^15, dry <= 2^15, wet < 27853), so the sum fits */
            s = (s * dry + ((y[0] + y[1]) >> 1) * wet) >> 15;
        }
        s = sat16(s);
        left[i] = (int16_t)sat16(s + side);
        right[i] = (int16_t)sat16(s - side);
    }
}
#endif
/* DENS's chorus: the same line, taps, mix and spread as ens.inc's, with the
 * taps and their wet terms computed once a pair of samples, at the even one;
 * the odd sample takes the mean of its two even neighbours (the line itself
 * runs at half rate). ens.inc reads the odd sample's taps 1.5 line samples
 * after its even one; this reads them halfway, so the wet path moves by one
 * line sample on the odd samples. DM_DENS_CHORUS_FULL keeps every sample's
 * taps (chorus_full). The 0.2 Hz LFO's delays are computed at the block's
 * first and last samples and taken linearly between them. ENS keeps ens.inc. */
static inline void chorus_taps(const int16_t *cm,uint32_t at,const int32_t *d,int32_t wet,
                               int32_t wside,int32_t *mid,int32_t *side)
{
    int32_t y[2];
    for (int c = 0; c < 2; c++) {
        uint32_t pos = at - (uint32_t)(d[c] >> 16);
        uint32_t j = pos >> 8;
        int32_t f = (int32_t)(pos & 255), y0 = cm[j & (ENS_LEN - 1)];
        y[c] = y0 + (((cm[(j + 1) & (ENS_LEN - 1)] - y0) * f) >> 8);
    }
    *side = sat16(((y[0] - y[1]) * wside) >> 16);
    *mid = ((y[0] + y[1]) >> 1) * wet;                 /* under 2^30 */
}
void dm_ens_chorus(struct dm_ens_voice *t,const uint16_t *p,int16_t *left,int16_t *right)
{
#if defined(DM_DENS_CHORUS_FULL)
    chorus_full(t,p,left,right);
#else
    int32_t chrl = P8(t, P_SYN + 5), chrw = P8(t, P_SYN + 6);
    int32_t dry = 32768 - (chrl * 27197) / (127 * 256), wet = (chrl * 27853) / (127 * 256);
    wet = (wet * tq_u16(T_ENS_G, chrw)) >> 15;
    int32_t wside = (wet * tq_u16(T_ENS_W, chrw)) >> 15;
    int16_t *cm = t->line;
    uint32_t ph = t->ph[4];
    t->ph[4] = ph + 32 * ENS_CINC;
    int32_t d[2], step[2], m0 = 0, s0 = 0, m1, s1;
    for (int c = 0; c < 2; c++) {
        int32_t a = (2 << 8) + ENS_CDEP + ((ENS_CDEP * sinp(ph + ENS_CINC + ((uint32_t)c << 30))) >> 15);
        int32_t b = (2 << 8) + ENS_CDEP + ((ENS_CDEP * sinp(ph + 32 * ENS_CINC + ((uint32_t)c << 30))) >> 15);
        d[c] = a << 16;                        /* 16 fraction bits */
        step[c] = ((b - a) << 16) / 31 * 2;    /* a pair's step */
    }
    if (chrl) chorus_taps(cm, t->ph[5] << 8, d, wet, wside, &m0, &s0);
    for (unsigned i = 0; i < 32; i += 2) {
        int32_t s = left[i], x;
        t->ms[0] += s;
        if (chrl) s = (s * dry + m0) >> 15;
        s = sat16(s);
        left[i] = (int16_t)sat16(s + s0);
        right[i] = (int16_t)sat16(s - s0);
        x = left[i + 1];
        t->ms[0] += x;
        cm[t->ph[5]++ & (ENS_LEN - 1)] = (int16_t)sat16(t->ms[0] >> 1); t->ms[0] = 0;
        if (chrl) {
            d[0] += step[0]; d[1] += step[1];
            chorus_taps(cm, t->ph[5] << 8, d, wet, wside, &m1, &s1);
            x = (x * dry + ((m0 >> 1) + (m1 >> 1))) >> 15;
            int32_t side = (s0 + s1) >> 1;
            m0 = m1; s0 = s1;
            x = sat16(x);
            left[i + 1] = (int16_t)sat16(x + side);
            right[i + 1] = (int16_t)sat16(x - side);
        } else {
            x = sat16(x);
            left[i + 1] = (int16_t)x;
            right[i + 1] = (int16_t)x;
        }
    }
#endif
}
