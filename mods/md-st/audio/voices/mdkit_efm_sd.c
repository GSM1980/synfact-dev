/* SPDX-License-Identifier: CC0-1.0
 * mdkit_efm_sd.c - EFM-SD standalone voice
 
 * Synfact: generated from mdkit/src/efm/sd by tools/md_port.py; do not edit.
 * The changes are listed there, and marked "Synfact" here.
 */
#include "md-types.h"
#include "md-types.h"
#include "mdkit_efm_sd.h"
/* Synfact: the approximate forms this voice takes (tools/md_port.py, APPROX) */
#define MD_AP_MID 1
#define MD_AP_FM 1
#include "md-acc.h"
#ifdef MD_ST_TABLES
#include "md-tables.h"
#endif


/* ---- Fixed-point helpers ------------------------------------------------
 *
 * 24-bit signed words with 56-bit accumulators. Products enter the high half
 * of the accumulator; rounded operations round the complete sum.
 */
#define MDKIT_MASK24 UINT32_C(0xffffff)
#define MDKIT_MASK48 UINT64_C(0xffffffffffff)
#define MDKIT_MASK56 UINT64_C(0xffffffffffffff)
#define MDKIT_TWO_PI 6.283185307179586476925286766559
#define MDKIT_PI     3.14159265358979323846

static inline __attribute__((unused)) int64_t mdkit_signed_bits(uint64_t word,unsigned bits)
{
    const uint64_t sign=UINT64_C(1)<<(bits-1);
    const uint64_t mask=(UINT64_C(1)<<bits)-1;
    word&=mask;
    return (word&sign)?(int64_t)word-(int64_t)(UINT64_C(1)<<bits):(int64_t)word;
}
static inline __attribute__((unused)) int64_t mdkit_s24(uint32_t w)
{ return md_s(w); /* Synfact: md-native.h */ }
static inline __attribute__((unused)) int64_t mdkit_s56(uint64_t a)
{ return md_acc_s56(a); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint32_t mdkit_mid(uint64_t a)
{ return md_acc_mid(a); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint32_t mdkit_low(uint64_t a)
{ return md_acc_low(a); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint64_t mdkit_from24(uint32_t w)
{ return md_acc_from24(w); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint64_t mdkit_from48(uint64_t w)
{ return md_acc_from48(w); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint64_t mdkit_to48(uint64_t a)
{ return md_acc_to48(a); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint64_t mdkit_setmid(uint64_t a,uint32_t w)
{ return md_acc_setmid(a, w); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint64_t mdkit_add(uint64_t a,uint64_t b)
{ return md_acc_add(a, b); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint64_t mdkit_sub(uint64_t a,uint64_t b)
{ return md_acc_sub(a, b); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint64_t mdkit_asl(uint64_t a,unsigned n)
{ return md_acc_asl(a, n); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint64_t mdkit_asr(uint64_t a,unsigned n)
{ return md_acc_asr(a, n); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint64_t mdkit_mpy(uint32_t a,uint32_t b)
{ return md_acc_mpy(a, b); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint64_t mdkit_mpy_su(uint32_t s,uint32_t u)
{ return md_acc_mpy_su(s, u); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint64_t mdkit_mpyr(uint32_t a,uint32_t b)
{ return md_acc_mpyr(a, b); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint64_t mdkit_mac(uint64_t acc,uint32_t x,uint32_t y)
{ return md_acc_mac(acc, x, y); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint64_t mdkit_macr(uint64_t acc,uint32_t x,uint32_t y)
{ return md_acc_macr(acc, x, y); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint64_t mdkit_and_mid(uint64_t a,uint32_t mask)
{ return md_acc_and_mid(a, mask); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint64_t mdkit_pack48(uint32_t hi,uint32_t lo)
{ return md_acc_pack48(hi, lo); }  /* Synfact: md-acc.h */
static inline __attribute__((unused)) uint32_t mdkit_acc_to_word(uint64_t a)
{ return md_acc_acc_to_word(a); }  /* Synfact: md-acc.h */

/* ---- Local coefficient construction ------------------------------------ */

static __attribute__((unused)) double mdkit_exp2(double x)
{
    long long n=(long long)x;
    double f=x-(double)n;
    if(f<0.0){f+=1.0;--n;}
    const double y=f*0.69314718055994530942;
    double term=1.0,sum=1.0;
    for(unsigned k=1;k<=12;++k){term*=y/(double)k;sum+=term;}
    double scale=1.0;
    while(n>0){scale*=2.0;--n;}
    while(n<0){scale*=0.5;++n;}
    return sum*scale;
}

static __attribute__((unused)) double mdkit_sin(double x)
{
    x-=MDKIT_TWO_PI*(double)(long long)(x/MDKIT_TWO_PI+(x<0.0?-0.5:0.5));
    if(x> MDKIT_PI/2.0) x= MDKIT_PI-x;
    else if(x<-MDKIT_PI/2.0) x=-MDKIT_PI-x;
    const double x2=x*x;
    return x*(1.0-x2*(1.0/6.0-x2*(1.0/120.0-x2*(1.0/5040.0
        -x2*(1.0/362880.0-x2*(1.0/39916800.0))))));
}

static __attribute__((unused)) uint32_t mdkit_logpoly(const double *c,unsigned degree,double x)
{
    double y=c[degree];
    for(unsigned i=degree;i-->0;) y=y*x+c[i];
    const double v=mdkit_exp2(y);
    return (uint32_t)(long long)(v+0.5)&MDKIT_MASK24;
}

static const double mdkit_decay_lo[4] = {
    16.2587772550628955,-1.65240088787418893,
    -0.00404324436830615259,0.000986419366542184073};
static const double mdkit_decay_mid[4] = {
    14.5695368124188676,-5.1434733494674223,
    -0.00676881681461822659,0.00248508759982288534};
static const double mdkit_decay_hi[4] = {
    9.38745457862932398,-4.26718908320820933,
    0.00385140734226158653,-0.00582524258210295829};
static const double mdkit_decay_end[4] = {
    5.08388492206188403,-1.11841448721353331,
    0.0978243040805674563,-0.0695259770934334581};

#ifdef MD_ST_TABLES
/* Synfact: computed on the host by this same code (md-tables.c) */
static __attribute__((unused)) uint32_t mdkit_decay_index(unsigned index)
{
    MD_ST_DOMAIN((index) < 1024u, "efm_sd_decay", index);
    return (index) < 1024u ? md_efm_sd_decay[index] : 0u;
}
#else
static __attribute__((unused)) uint32_t mdkit_decay_index(unsigned index)
{
    if(index<=196u)
        return (UINT32_C(0x800000)-mdkit_logpoly(mdkit_decay_lo,3,((double)index-147.0)/49.0))&MDKIT_MASK24;
    if(index<=349u)
        return (UINT32_C(0x800000)-mdkit_logpoly(mdkit_decay_mid,3,(double)(index-197u)/152.0))&MDKIT_MASK24;
    if(index<=476u)
        return (UINT32_C(0x800000)-mdkit_logpoly(mdkit_decay_hi,3,(double)(index-350u)/126.0))&MDKIT_MASK24;
    return (UINT32_C(0x800000)-mdkit_logpoly(mdkit_decay_end,3,(double)(index-477u)/32.0))&MDKIT_MASK24;
}
#endif
static __attribute__((unused)) uint32_t mdkit_decay_rate(unsigned knob,unsigned rate)
{ return mdkit_decay_index(147u+(((uint32_t)knob*128u*rate)>>12)); }

#ifdef MD_ST_TABLES
/* Synfact: computed on the host by this same code (md-tables.c) */
static __attribute__((unused)) uint32_t mdkit_pitch_poly(const double *c,unsigned degree,unsigned knob)
{
    MD_ST_DOMAIN((knob) < 128u, "efm_sd_pitch", knob);
    return (knob) < 128u ? md_efm_sd_pitch[knob] : 0u;
}
#else
static __attribute__((unused)) uint32_t mdkit_pitch_poly(const double *c,unsigned degree,unsigned knob)
{ return mdkit_logpoly(c,degree,(double)knob/127.0); }
#endif
static __attribute__((unused)) uint32_t mdkit_affine(unsigned knob,unsigned shift)
{ return ((uint32_t)knob<<shift)&MDKIT_MASK24; }
static __attribute__((unused)) uint32_t mdkit_affine_min(unsigned knob,unsigned shift,uint32_t minimum)
{
    const uint32_t v=((uint32_t)knob<<shift)&MDKIT_MASK24;
    return v<minimum?minimum:v;
}
static __attribute__((unused)) uint32_t mdkit_inverse(unsigned knob,unsigned shift)
{ return (UINT32_C(0x7fffff)-((uint32_t)knob<<shift))&MDKIT_MASK24; }
static __attribute__((unused)) uint32_t mdkit_affine_offset(unsigned knob,int64_t step,int64_t offset)
{ return (uint32_t)(((int64_t)knob*step+offset)&0xffffff); }


/* ---- Control clock ------------------------------------------------------ */

static void efm_clock_seed(mdkit_control_clock *c,unsigned count,
                           const unsigned char *knobs)
{
    c->count=count;
    for(unsigned i=0;i<count;++i) {
        const float v=(float)knobs[i];
        c->target[i]=c->raw[i]=c->current[i]=c->pending[i]=v;
    }
    c->block=0; c->next_update=0; c->publish_block=0; c->pending_valid=0;
}

static void efm_clock_target(mdkit_control_clock *c,unsigned index,unsigned value)
{
    if(index<c->count) c->target[index]=(float)(value>127u?127u:value);
}

#ifndef MD_ST_NO_NATIVE
/* Synfact: efm_clock_boundary in plain 32-bit integers, the same words */
#include "../native/efm_clock.c"
#else
static void efm_clock_boundary(mdkit_control_clock *c)
{
    if(c->pending_valid && c->block>=c->publish_block) {
        for(unsigned i=0;i<c->count;++i) c->current[i]=c->pending[i];
        c->pending_valid=0;
    }
    if(c->block==c->next_update) {
        for(unsigned i=0;i<c->count;++i) {
            c->pending[i]=c->raw[i];
            c->raw[i]=(3.0f*c->raw[i]+c->target[i])*0.25f;
        }
        c->pending_valid=1;
        c->publish_block=c->block+1;
        c->next_update+=12;
    }
    c->block+=1;
}
#endif

/* The implementation reads its sine table at the current 24-bit phase word's low
 * 15 bits; the table is the same waveform the player uses. */
/* Accurate sine at the ABI boundary: same range reduction as mdkit_sin, one
 * more Taylor term, so the quantized 24-bit sample matches the implementation
 * mathematical sine on every phase-grid point (no fitted constants; the
 * coefficients are the exact 1/(2k+1)! series terms). */
#ifdef MD_ST_TABLES
/* Synfact: computed on the host by this same code (md-tables.c) */
static uint32_t sd_sine(uint16_t index)
{
    return md_sine_word(&md_efm_sd_sine, index);
}
#else
static uint32_t sd_sine(uint16_t index)
{
    double x = (MDKIT_TWO_PI * (double)(index & 0x7fffu)) / 32768.0;
    x -= MDKIT_TWO_PI * (double)(long long)(x / MDKIT_TWO_PI
                                               + (x < 0.0 ? -0.5 : 0.5));
    if (x >  MDKIT_PI / 2.0) x =  MDKIT_PI - x;
    else if (x < -MDKIT_PI / 2.0) x = -MDKIT_PI - x;
    const double x2 = x * x;
    const double s = x * (1.0 - x2 * (1.0 / 6.0 - x2 * (1.0 / 120.0
        - x2 * (1.0 / 5040.0 - x2 * (1.0 / 362880.0 - x2 * (1.0 / 39916800.0
        - x2 * (1.0 / 6227020800.0 - x2 * (1.0 / 1307674368000.0))))))));
    const double scaled = 8388608.0 * s;
    long long sample = (long long)(scaled < 0.0 ? scaled - 0.5 : scaled + 0.5);
    if (sample > 0x7fffff) sample = 0x7fffff;
    if (sample < -0x800000) sample = -0x800000;
    return (uint32_t)sample & MDKIT_MASK24;
}
#endif

/* The shared DSP two-pole resonator (unrounded MAC, as SD uses). */
static void sd_resonance(const uint32_t in[32], uint32_t out[32], uint32_t coefficient,
                         uint32_t *history, uint32_t *delay)
{
    const uint32_t gain = mdkit_mid(mdkit_add(mdkit_asr(mdkit_from24(coefficient), 1),
                                        mdkit_from24(0x400000)));
    uint32_t hist = *history, previous = *delay;
    for (unsigned i = 0; i < 32; ++i) {
        uint64_t a = mdkit_mpy(in[i], gain);
        const uint32_t old = hist;
        hist = mdkit_mid(a);
        a = mdkit_sub(a, mdkit_from24(old));
        a = mdkit_mac(a, previous, coefficient);
        out[i] = mdkit_acc_to_word(a);
        previous = out[i];
    }
    *history = hist;
    *delay = previous;
}

/* ---- Voice control law ------------------------------------------------- */

static const double mdkit_sd_pitch[4] = { 14.500590089999303, 2.4796963378948127, -0.00549031602082840093, 0.00724890179121155491 };
static void sd_build_controls(mdkit_efm_sd *s)
{
    const float *k=s->clock.current;
    s->op1_rate=mdkit_pitch_poly(mdkit_sd_pitch,3,(unsigned)k[MDKIT_EFM_SD_PTCH]);
    s->op1_decay=mdkit_decay_rate((unsigned)k[MDKIT_EFM_SD_DEC],70u);
    s->mod_rate=mdkit_affine((unsigned)k[MDKIT_EFM_SD_MFRQ],16u);
    if(s->mod_rate<0x10000u) s->mod_rate=0x10000u;
    s->mod_amount=mdkit_affine((unsigned)k[MDKIT_EFM_SD_MOD],16u);
    s->fb_decay=mdkit_decay_rate((unsigned)k[MDKIT_EFM_SD_MDEC],82u);
    s->noise_amount=mdkit_affine((unsigned)k[MDKIT_EFM_SD_NOIS],15u);
    s->op2_decay=mdkit_decay_rate((unsigned)k[MDKIT_EFM_SD_NDEC],50u);
    s->tail_coef=mdkit_inverse((unsigned)k[MDKIT_EFM_SD_HPF],15u);
}


/* ---- Default controls --------------------------------------------------- */

static const unsigned char mdkit_efm_sd_defaults[MDKIT_EFM_SD_CONTROLS] = {
    27, 60, 14, 79, 0, 5, 127, 29
};

static void mdkit_efm_sd_init(void *state)
{
    mdkit_efm_sd *s = (mdkit_efm_sd *)state;
    efm_clock_seed(&s->clock, MDKIT_EFM_SD_CONTROLS, mdkit_efm_sd_defaults);
    s->pos = 0;
    s->mod_index  = 0x7a1200u;
    s->noise_coef = 0x44aa20u;
    sd_build_controls(s);
    s->l_b = s->l_d = s->l_12 = 0;
    s->fb = s->op1_out = s->op2_out = 0;
    s->noise14 = 0x789abcu;   /* DSP init seeds Y14 with 0x789abc */
    s->noise15 = 0;
    s->ring1_hist = s->ring1_delay = 0;
    s->ring2_hist = s->ring2_delay = 0;
    s->have_block = 0;
}

static void mdkit_efm_sd_set_control(void *state, unsigned i, unsigned v)
{
    efm_clock_target(&((mdkit_efm_sd *)state)->clock, i, v);
}

static void mdkit_efm_sd_trigger(void *state)
{
    /*  (trigger path): phase accumulators seed 0x1f4, the modulator
     * contribution ADDS to the retained operator-1 output, operator-2 and the
     * feedback register latch full scale, both resonators reset. Control slots
     * are the published knob words (rebuilt lazily). */
    mdkit_efm_sd *s = (mdkit_efm_sd *)state;
    s->l_b  = mdkit_pack48(0x1f4u, 0);
    s->l_d  = mdkit_pack48(0x1f4u, 0);
    s->l_12 = 0;
    s->op1_out = mdkit_acc_to_word(mdkit_add(mdkit_from24(0x50c335), mdkit_from24(s->op1_out)));
    s->fb      = 0x7fffffu;
    s->op2_out = 0x7fffffu;
    s->ring1_hist = s->ring1_delay = 0;
    s->ring2_hist = s->ring2_delay = 0;
    s->have_block = 0;
}

#ifndef MD_ST_TARGET
static void mdkit_efm_sd_render(void *state, float *out, unsigned frames)
{
    mdkit_efm_sd *s = (mdkit_efm_sd *)state;
    while (frames) {
        if (s->pos == 0u) {
            efm_clock_boundary(&s->clock);
            sd_build_controls(s);
            s->have_block = 0;
        }
        if (!s->have_block) {
            uint32_t nring[32], ring1[32], ring2[32];
            uint64_t lb = s->l_b, ld = s->l_d, l12 = s->l_12;
            uint32_t fb = s->fb, op1 = s->op1_out, op2 = s->op2_out;

            /* P:102fad..102fb9: additive two-word noise sequence. The parallel
             * stores read the old X1 before the accumulator update. */
            uint32_t old = s->noise15, next = (s->noise14 + old) & MDKIT_MASK24;
            for (unsigned i = 0; i < 31; ++i) {
                nring[i] = next;
                const uint32_t sum = (old + next) & MDKIT_MASK24;
                old = next;
                next = sum;
            }
            nring[31] = next;
            s->noise14 = old;
            s->noise15 = next;
            sd_resonance(nring, ring1, s->noise_coef, &s->ring1_hist, &s->ring1_delay);

            /* Implementation detail. */
            for (unsigned i = 0; i < 32; ++i) {
                uint32_t y0 = s->mod_rate;      /* x3  MFRQ */
                uint32_t y1 = s->op1_rate;      /* x1  PTCH */
                uint64_t a = mdkit_mpy(y1, y0);
                const uint64_t xa = l12;
                a = mdkit_asr(a, 5);
                uint64_t b = mdkit_add(lb, xa);
                b = mdkit_and_mid(b, 0x7fff);
                const uint16_t index1 = (uint16_t)mdkit_mid(b);
                b = mdkit_sub(b, xa);
                b = mdkit_add(b, a);
                y0 = s->mod_index;              /* x6  fixed modulator index */
                uint32_t x0 = sd_sine(index1) & MDKIT_MASK24;
                lb = mdkit_pack48(mdkit_mid(b), mdkit_low(b));
                a = mdkit_mpy(y0, x0);
                y0 = fb;                        /* x0f feedback register */
                a = mdkit_asr(a, 8);
                uint32_t x1 = s->fb_decay;      /* x5  MDEC */
                l12 = mdkit_to48(a);
                b = mdkit_mpy(x1, y0);
                a = mdkit_mpyr(y0, x0);
                fb = mdkit_mid(b);
                x0 = mdkit_mid(a);
                b = mdkit_mpyr(x0, y1);
                y0 = s->mod_amount;             /* x4  MOD */
                x1 = mdkit_mid(b);
                b = mdkit_mpy(x1, y0);
                b = mdkit_asl(b, 3);
                b = mdkit_add(b, mdkit_from24(y1));
                a = ld;
                b = mdkit_asr(b, 8);
                const uint32_t idx2 = mdkit_mid(a);
                a = mdkit_add(a, b);
                a = mdkit_and_mid(a, 0x7fff);
                x0 = sd_sine(idx2) & MDKIT_MASK24;
                ld = mdkit_pack48(mdkit_mid(a), mdkit_low(a));
                y0 = op1;                       /* x10 operator-1 out */
                y1 = s->op1_decay;              /* x2  DEC */
                b = mdkit_mpy(y1, y0);
                y1 = s->op2_decay;              /* x8  NDEC */
                a = mdkit_mpyr(y0, x0);
                y0 = op2;                       /* x11 operator-2 out */
                op1 = mdkit_mid(b);
                x0 = ring1[i];
                x1 = mdkit_mid(a);
                b = mdkit_mpy(y1, y0);
                a = mdkit_mpyr(y0, x0);
                op2 = mdkit_mid(b);
                y1 = mdkit_mid(a);
                y0 = s->noise_amount;           /* x7  NOIS */
                a = mdkit_mpyr(y1, y0);
                a = mdkit_add(a, mdkit_from24(x1));
                ring2[i] = mdkit_acc_to_word(a);
            }

            sd_resonance(ring2, s->block, s->tail_coef, &s->ring2_hist, &s->ring2_delay);

            s->l_b = lb; s->l_d = ld; s->l_12 = l12;
            s->fb = fb; s->op1_out = op1; s->op2_out = op2;
            s->have_block = 1;
        }
        unsigned n = 32u - s->pos;
        if (n > frames) n = frames;
        for (unsigned i = 0; i < n; ++i) {
            const uint32_t w = s->block[s->pos + i] & MDKIT_MASK24;
            const int32_t v = (w & 0x800000u) ? (int32_t)w - 0x1000000 : (int32_t)w;
            out[i] = (float)v / 8388608.0f;
        }
        out += n; frames -= n; s->pos += n;
        if (s->pos >= 32u) s->pos = 0;
    }
}
#endif
/* Synfact: the same render, the words themselves */
#ifndef MD_ST_NO_NATIVE
/* Synfact: md_st_efm_sd_render_words in plain 32-bit integers, the same words */
#include "../native/efm_sd.c"
#else
void md_st_efm_sd_render_words(void *state, int32_t *out, unsigned frames)
{
    mdkit_efm_sd *s = (mdkit_efm_sd *)state;
    while (frames) {
        if (s->pos == 0u) {
            efm_clock_boundary(&s->clock);
            sd_build_controls(s);
            s->have_block = 0;
        }
        if (!s->have_block) {
            uint32_t nring[32], ring1[32], ring2[32];
            uint64_t lb = s->l_b, ld = s->l_d, l12 = s->l_12;
            uint32_t fb = s->fb, op1 = s->op1_out, op2 = s->op2_out;

            /* P:102fad..102fb9: additive two-word noise sequence. The parallel
             * stores read the old X1 before the accumulator update. */
            uint32_t old = s->noise15, next = (s->noise14 + old) & MDKIT_MASK24;
            for (unsigned i = 0; i < 31; ++i) {
                nring[i] = next;
                const uint32_t sum = (old + next) & MDKIT_MASK24;
                old = next;
                next = sum;
            }
            nring[31] = next;
            s->noise14 = old;
            s->noise15 = next;
            sd_resonance(nring, ring1, s->noise_coef, &s->ring1_hist, &s->ring1_delay);

            /* Implementation detail. */
            for (unsigned i = 0; i < 32; ++i) {
                uint32_t y0 = s->mod_rate;      /* x3  MFRQ */
                uint32_t y1 = s->op1_rate;      /* x1  PTCH */
                uint64_t a = mdkit_mpy(y1, y0);
                const uint64_t xa = l12;
                a = mdkit_asr(a, 5);
                uint64_t b = mdkit_add(lb, xa);
                b = mdkit_and_mid(b, 0x7fff);
                const uint16_t index1 = (uint16_t)mdkit_mid(b);
                b = mdkit_sub(b, xa);
                b = mdkit_add(b, a);
                y0 = s->mod_index;              /* x6  fixed modulator index */
                uint32_t x0 = sd_sine(index1) & MDKIT_MASK24;
                lb = mdkit_pack48(mdkit_mid(b), mdkit_low(b));
                a = mdkit_mpy(y0, x0);
                y0 = fb;                        /* x0f feedback register */
                a = mdkit_asr(a, 8);
                uint32_t x1 = s->fb_decay;      /* x5  MDEC */
                l12 = mdkit_to48(a);
                b = mdkit_mpy(x1, y0);
                a = mdkit_mpyr(y0, x0);
                fb = mdkit_mid(b);
                x0 = mdkit_mid(a);
                b = mdkit_mpyr(x0, y1);
                y0 = s->mod_amount;             /* x4  MOD */
                x1 = mdkit_mid(b);
                b = mdkit_mpy(x1, y0);
                b = mdkit_asl(b, 3);
                b = mdkit_add(b, mdkit_from24(y1));
                a = ld;
                b = mdkit_asr(b, 8);
                const uint32_t idx2 = mdkit_mid(a);
                a = mdkit_add(a, b);
                a = mdkit_and_mid(a, 0x7fff);
                x0 = sd_sine(idx2) & MDKIT_MASK24;
                ld = mdkit_pack48(mdkit_mid(a), mdkit_low(a));
                y0 = op1;                       /* x10 operator-1 out */
                y1 = s->op1_decay;              /* x2  DEC */
                b = mdkit_mpy(y1, y0);
                y1 = s->op2_decay;              /* x8  NDEC */
                a = mdkit_mpyr(y0, x0);
                y0 = op2;                       /* x11 operator-2 out */
                op1 = mdkit_mid(b);
                x0 = ring1[i];
                x1 = mdkit_mid(a);
                b = mdkit_mpy(y1, y0);
                a = mdkit_mpyr(y0, x0);
                op2 = mdkit_mid(b);
                y1 = mdkit_mid(a);
                y0 = s->noise_amount;           /* x7  NOIS */
                a = mdkit_mpyr(y1, y0);
                a = mdkit_add(a, mdkit_from24(x1));
                ring2[i] = mdkit_acc_to_word(a);
            }

            sd_resonance(ring2, s->block, s->tail_coef, &s->ring2_hist, &s->ring2_delay);

            s->l_b = lb; s->l_d = ld; s->l_12 = l12;
            s->fb = fb; s->op1_out = op1; s->op2_out = op2;
            s->have_block = 1;
        }
        unsigned n = 32u - s->pos;
        if (n > frames) n = frames;
        for (unsigned i = 0; i < n; ++i) {
            const uint32_t w = s->block[s->pos + i] & MDKIT_MASK24;
            const int32_t v = (w & 0x800000u) ? (int32_t)w - 0x1000000 : (int32_t)w;
            out[i] = (int32_t)v;
        }
        out += n; frames -= n; s->pos += n;
        if (s->pos >= 32u) s->pos = 0;
    }
}
#endif

static const char *const mdkit_efm_sd_control_names[MDKIT_EFM_SD_CONTROLS] = {
    "PTCH", "DEC", "NOIS", "NDEC", "MOD", "MFRQ", "MDEC", "HPF"
};

const mdkit_voice mdkit_voice_efm_sd = {
    "EFM-SD", 33u, MDKIT_EFM_SD_CONTROLS, mdkit_efm_sd_control_names,
    sizeof(mdkit_efm_sd),
    mdkit_efm_sd_init, mdkit_efm_sd_set_control, mdkit_efm_sd_trigger, MD_ST_RENDER(mdkit_efm_sd_render), mdkit_efm_sd_defaults
};
