/* SPDX-License-Identifier: CC0-1.0
 * TRX-XT: self-contained drum synthesis.
 
 * Synfact: generated from mdkit/src/trx/xt by tools/md_port.py; do not edit.
 * The changes are listed there, and marked "Synfact" here.
 */
#include "mdkit_trx_xt.h"
/* Synfact: the approximate forms this voice takes (tools/md_port.py, APPROX) */
#define MD_AP_MID 1
#define MD_AP_SINE 1
#define MD_AP_FLOOR 1
/* Synfact: the control clock in integers (tools/md_port.py, port_clock) */
typedef uint32_t __attribute__((may_alias)) md_clk_u32;
#define MD_CLK(f) ((md_clk_u32 *)(uintptr_t)(f))
#include "md-acc.h"
#ifdef MD_ST_TABLES
#include "md-tables.h"
#endif

/* ---- Default controls --------------------------------------------------- */
/* Active controls are in descriptor order; unused clock slots stay at 64. */
static const unsigned char mdkit_trx_xt_defaults[MDKIT_MAX_CONTROLS] = {
    70, 25, 50, 30, 64, 32, 0, 64
};


#define MD_MASK24 UINT32_C(0xffffff)
#define MD_MASK48 UINT64_C(0xffffffffffff)
#define MD_MASK56 UINT64_C(0xffffffffffffff)

static inline int64_t xt_signed_bits(uint64_t word, unsigned bits)
{
    const uint64_t sign = UINT64_C(1) << (bits - 1);
    const uint64_t mask = (UINT64_C(1) << bits) - 1;
    word &= mask;
    return (word & sign) ? (int64_t)word - (int64_t)(UINT64_C(1) << bits)
                         : (int64_t)word;
}
static inline int64_t xt_s24(uint32_t w) { return md_s(w); /* Synfact: md-native.h */ }
static inline int64_t xt_s56(uint64_t a)
{ return md_acc_s56(a); }  /* Synfact: md-acc.h */
static inline uint32_t xt_mid(uint64_t a)
{ return md_acc_mid(a); }  /* Synfact: md-acc.h */
static inline uint32_t xt_low(uint64_t a)
{ return md_acc_low(a); }  /* Synfact: md-acc.h */
static inline uint64_t xt_from24(uint32_t w)
{ return md_acc_from24(w); }  /* Synfact: md-acc.h */
static inline uint64_t xt_from48(uint64_t w)
{ return md_acc_from48(w); }  /* Synfact: md-acc.h */

static inline uint64_t xt_setmid(uint64_t a, uint32_t w)
{ return md_acc_setmid(a, w); }  /* Synfact: md-acc.h */
static inline uint64_t xt_add(uint64_t a, uint64_t b)
{ return md_acc_add(a, b); }  /* Synfact: md-acc.h */
static inline uint64_t xt_sub(uint64_t a, uint64_t b)
{ return md_acc_sub(a, b); }  /* Synfact: md-acc.h */

static inline uint64_t xt_asr(uint64_t a, unsigned n)
{ return md_acc_asr(a, n); }  /* Synfact: md-acc.h */
static inline uint64_t xt_mpy(uint32_t a, uint32_t b)
{ return md_acc_mpy(a, b); }  /* Synfact: md-acc.h */







static inline uint64_t xt_and_mid(uint64_t a, uint32_t mask)
{ return md_acc_and_mid(a, mask); }  /* Synfact: md-acc.h */
static inline uint64_t xt_pack48(uint32_t hi, uint32_t lo)
{ return md_acc_pack48(hi, lo); }  /* Synfact: md-acc.h */
static inline uint32_t xt_acc_to_word(uint64_t a)
{ return md_acc_acc_to_word(a); }  /* Synfact: md-acc.h */

static inline void xt_control_clock_seed(mdkit_control_clock *c, unsigned count,
                                         const unsigned char *knobs)
{
    c->count = count;
    for (unsigned i = 0; i < count; ++i) {
        MD_CLK(c->target)[i] = knobs[i]; MD_CLK(c->raw)[i] = MD_CLK(c->current)[i] = MD_CLK(c->pending)[i] = (uint32_t)knobs[i] * 128u;
    }
    c->block = 0; c->next_update = 0; c->publish_block = 0; c->pending_valid = 0;
}


static double xt_local_sin(double x)
{
    x -= 6.283185307179586476925286766559 * (double)(long long)(x / 6.283185307179586476925286766559
                                            + (x < 0.0 ? -0.5 : 0.5));
    if (x > 3.14159265358979323846 / 2.0) x = 3.14159265358979323846 - x;
    else if (x < -3.14159265358979323846 / 2.0) x = -3.14159265358979323846 - x;
    const double x2 = x * x;
    return x * (1.0 - x2 * (1.0 / 6.0 - x2 * (1.0 / 120.0 - x2 * (1.0 / 5040.0
        - x2 * (1.0 / 362880.0 - x2 * (1.0 / 39916800.0))))));
}

static double xt_local_exp2(double x)
{

    long long n = (long long)x;
    const double f = x - (double)n;
    const double y = f * 0.69314718055994530942;
    double term = 1.0, sum = 1.0;
    for (unsigned k = 1; k <= 14; ++k) {
        term *= y / (double)k;
        sum += term;
    }
    double scale = 1.0;
    while (n > 0) { scale *= 2.0; --n; }
    while(n < 0) { scale *= 0.5; ++n; }
    return sum * scale;
}


#ifdef MD_ST_TABLES
/* Synfact: computed on the host by this same code (md-tables.c) */
static uint32_t xt_local_sine(uint16_t index)
{
    return md_sine_word(&md_trx_xt_sine, index);
}
#else
static uint32_t xt_local_sine(uint16_t index){double y=8388608.0*xt_local_sin(6.283185307179586476925286766559*(double)(index&32767u)/32768.0);int64_t v=(int64_t)(y+(y<0?-0.5:0.5));if(v>8388607)v=8388607;if(v< -8388608)v=-8388608;return (uint32_t)v&MD_MASK24;}
#endif
enum { MDKIT_TRX_PUBLISH=1, MDKIT_TRX_TRIGGER=2, MDKIT_TRX_FIRST=4 };
static inline void xt_trx_events_init(mdkit_control_clock *c,xt_trx_events *e)
{

    xt_control_clock_seed(c,8,mdkit_trx_xt_defaults); c->next_update=2;
    e->targets=e->trigger=e->triggered=0;
    for(unsigned i=0;i<8;++i){e->queued[i]=mdkit_trx_xt_defaults[i];e->last_raw[i]=(unsigned)mdkit_trx_xt_defaults[i] * 128u;}
}
static inline void xt_trx_events_target(xt_trx_events *e,unsigned count,unsigned i,unsigned value)
{ if(i<count){e->queued[i]=value>127?127:value;e->targets|=1u<<i;} }
static inline unsigned xt_trx_events_boundary(mdkit_control_clock *c,xt_trx_events *e,int trigger_only)
{
    unsigned result=0;
    if(c->pending_valid && c->block>=c->publish_block){
        for(unsigned i=0;i<8;++i)c->current[i]=c->pending[i];
        c->pending_valid=0;result=MDKIT_TRX_PUBLISH;
    }
    if(c->block==c->next_update){
        int changed=0;
        for(unsigned i=0;i<8;++i){
            uint32_t raw=MD_CLK(c->raw)[i];
            c->pending[i]=c->raw[i];changed|=raw!=e->last_raw[i];e->last_raw[i]=raw;
            MD_CLK(c->raw)[i]=(3*raw+128*MD_CLK(c->target)[i])/4;
        }
        c->pending_valid=changed&&!trigger_only;c->publish_block=c->block+1;c->next_update+=12;
    }
    for(unsigned i=0;i<8;++i)if(e->targets&(1u<<i))MD_CLK(c->target)[i]=e->queued[i];
    e->targets=0;
    if(e->trigger){
        for(unsigned i=0;i<8;++i)c->current[i]=c->raw[i];
        result=MDKIT_TRX_PUBLISH|MDKIT_TRX_TRIGGER|(e->triggered?0:MDKIT_TRX_FIRST);
        e->trigger=0;e->triggered=1;
    }
    ++c->block;return result;
}
#ifdef MD_ST_TABLES
/* Synfact: computed on the host by this same code (md-tables.c) */
static uint32_t xt_decay_curve(unsigned i)
{
    MD_ST_DOMAIN((i) < 220u, "trx_xt_decay", i);
    return (i) < 220u ? md_trx_xt_decay[i] : 0u;
}
#else
static uint32_t xt_decay_curve(unsigned i){ const double x=(double)i/219.0; static const double c[6]={14.467910447906096,-7.4066087405781262,-0.052968758469959625,0.13694595389014988,-0.17787137866263228,0.085317261378326167}; double y=c[5]; for(unsigned j=5;j>0;--j)y=y*x+c[j-1]; y=8388608.0-xt_local_exp2(y); return (uint32_t)(y+0.5)&MD_MASK24;}
#endif
#ifdef MD_ST_TABLES
/* Synfact: computed on the host by this same code (md-tables.c) */
static uint32_t xt_pitch_curve(unsigned i)
{
    MD_ST_DOMAIN((i) < 256u, "trx_xt_pitch", i);
    return (i) < 256u ? md_trx_xt_pitch[i] : 0u;
}
#else
static uint32_t xt_pitch_curve(unsigned i){ const double x=(double)i/255.0; static const double c[9]={3451.8388775740218,4767.739934752587,3832.3497931722413,-10864.032250908889,72220.033763494779,-183399.61066252604,236597.95525707773,-150740.20234588569,37864.477974669062}; double y=c[8]; for(unsigned j=8;j>0;--j)y=y*x+c[j-1]; return (uint32_t)(y+0.5)&MD_MASK24;}
#endif
static void trx_xt_packet(const float k[8],uint32_t o[9]){unsigned r[8];for(unsigned j=0;j<8;++j)r[j]=MD_CLK(k)[j];o[0]=xt_decay_curve((r[1]*220u)>>14);o[1]=xt_pitch_curve(r[0]>>6);o[2]=(r[2]+3u)>>6;o[3]=(8388607u-384u*r[5])&MD_MASK24;o[4]=(8388609u+384u*r[5])&MD_MASK24;o[5]=(r[6]*r[6]>>14)*2u;o[6]=8134709u+15u*r[3];o[7]=8388607u-256u*r[4];o[8]=4194304u+256u*r[4];}
#ifdef MD_ST_TABLES
/* Synfact: computed on the host by this same code (md-tables.c) */
static uint32_t xt_attack_shape(unsigned i)
{
    MD_ST_DOMAIN((i) < 64u, "trx_xt_attack", i);
    return (i) < 64u ? md_trx_xt_attack[i] : 0u;
}
#else
static uint32_t xt_attack_shape(unsigned i){double y=0.0;
if(i>=0u && i<=6u){const double x=2.0*(double)(i-0u)/6.0-1.0; static const double c[5]={0.0017012558974228886,-0.0041945321219307944,-0.014148018576882082,0.014694213867187488,0.023370916193181802};y=c[4];for(unsigned j=4;j>0;--j)y=y*x+c[j-1];}
if(i>=7u && i<=20u){const double x=2.0*(double)(i-7u)/13.0-1.0; static const double c[6]={0.66287943720817577,0.23311627795083575,-0.16599457308570337,0.031680977565955351,-0.033257104974378879,0.029794606834003201};y=c[5];for(unsigned j=5;j>0;--j)y=y*x+c[j-1];}
if(i>=21u && i<=42u){const double x=2.0*(double)(i-21u)/21.0-1.0; static const double c[6]={0.60274863826645009,-0.14332686830375449,0.016926911678009728,-0.015464608944364773,-0.017866523306929595,0.006407685569027508};y=c[5];for(unsigned j=5;j>0;--j)y=y*x+c[j-1];}
if(i>=43u && i<=63u){const double x=2.0*(double)(i-43u)/20.0-1.0; static const double c[5]={0.20440087120452702,-0.20162783189895792,0.014287060998893153,-0.0058353943332422435,0.004169423639191829};y=c[4];for(unsigned j=4;j>0;--j)y=y*x+c[j-1];}
int64_t w=(int64_t)(y*8388608.0+(y<0?-0.5:0.5));return (uint32_t)w&MD_MASK24;}
#endif




static uint32_t xt_sine(uint16_t index){return xt_local_sine(index);}


static void xt_build_controls(mdkit_trx_xt *s, int full)
{
    uint32_t packet[9];trx_xt_packet(s->clock.current,packet);
    s->decay=packet[0];s->pitch=packet[1];s->ramp_amount=packet[2];
    s->fold_upper=packet[3];s->fold_lower=packet[4];s->fold_step=packet[5];s->ramp_decay=packet[6];
    if(full){s->damp_seed=packet[7];s->amplitude=packet[7];s->attack_gain=packet[8];}
}

static void xt_trx_xt_init(void *state)
{
    mdkit_trx_xt *s = (mdkit_trx_xt *)state;
    xt_trx_events_init(&s->clock,&s->events);
    s->pos = 0;
    s->amplitude = s->attack_gain = s->ramp = s->stage = 0;
    s->damp_seed = 0;
    s->phase_hi = s->phase_lo = 0;
    s->out_prev = s->dmp_a = s->dmp_b = 0;
    for (unsigned i = 0; i < 32; ++i) s->delay[i] = 0;
    s->have_block = 0;
    xt_build_controls(s,1);
}

static void xt_trx_xt_set_control(void *state, unsigned i, unsigned v)
{
    xt_trx_events_target(&((mdkit_trx_xt *)state)->events,7,i,v);
}


static void xt_apply_trigger(void *state)
{
    mdkit_trx_xt *s = (mdkit_trx_xt *)state;

    s->stage = 3;
    s->ramp  = 0x7ffcb9u;
    s->amplitude    = s->damp_seed;
    s->phase_hi = 0; s->phase_lo = 0;
    s->dmp_a = s->fold_upper;
    s->dmp_b = s->fold_lower;
    s->have_block = 0;
}
static void xt_trx_xt_trigger(void *state)
{ ((mdkit_trx_xt *)state)->events.trigger=1; }



static void xt_ring32(mdkit_trx_xt *s, uint32_t out[32])
{
    uint32_t x1 = s->out_prev;
    for (unsigned i = 0; i < 32; ++i) {
        const uint32_t a = xt_mid(xt_mpy(x1, 0x733333u));
        out[i] = a;
        x1 = a;
    }
}


static void xt_attack_window(mdkit_trx_xt *s, uint32_t out[32], unsigned first)
{
    const uint32_t scale = s->attack_gain;
    for (unsigned k = 0; k < 16; ++k) {
        const uint32_t w0 = xt_attack_shape(first + 2 * k);
        const uint32_t w1 = xt_attack_shape(first + 2 * k + 1);
        const uint32_t a = xt_mid(xt_mpy(scale, w0));
        const uint32_t b = xt_mid(xt_mpy(scale, w1));
        out[2 * k] = a;
        out[2 * k + 1] = b;
    }
}


static void xt_main(mdkit_trx_xt *s, uint32_t out[32])
{
    uint32_t ring[32];


    uint64_t a = xt_asr(xt_from24(s->pitch), 6);
    const uint32_t ring_sum = s->ramp;
    a = xt_add(a, xt_mpy(ring_sum, s->ramp_amount));
    s->ramp = xt_mid(xt_mpy(ring_sum, s->ramp_decay));


    uint32_t x1 = xt_mid(a);
    uint32_t x0 = xt_low(a);
    uint64_t phase = xt_from48(xt_pack48(s->phase_hi, s->phase_lo));
    for (unsigned i = 0; i < 32; ++i) {
        const uint16_t index = (uint16_t)(xt_mid(phase) & 0x7fffu);
        phase = xt_and_mid(phase,0x7fff);
        phase = xt_add(phase, xt_from48(xt_pack48(x1, x0)));
        x0 = xt_sine(index);
        ring[i] = x0;
    }
    s->phase_hi = xt_mid(phase);
    s->phase_lo = xt_low(phase);


    {
        uint32_t damping = s->amplitude;
        for (unsigned i = 0; i < 32; ++i) {
            const uint32_t filtered = xt_mid(xt_mpy(damping, ring[i]));
            damping = xt_mid(xt_mpy(damping, s->decay));
            ring[i] = filtered;
        }
        s->amplitude = damping;
    }


    {
        uint64_t lo = xt_from24(s->dmp_a);
        uint64_t hi = xt_from24(s->dmp_b);
        const uint64_t dist_a = xt_from24(s->fold_upper);
        const uint64_t dist_b = xt_from24(s->fold_lower);
        const uint64_t step_dt = xt_from24(s->fold_step);
        for (unsigned i = 0; i < 32; ++i) {
            uint64_t b = xt_sub(0, xt_from24(ring[i]));

            if (xt_s56(b) < xt_s56(dist_a)) {

                if (!(xt_s56(b) > xt_s56(dist_b))) {
                    b = xt_add(hi, step_dt);
                    lo = dist_a;
                    hi = xt_from24(xt_acc_to_word(b));
                }
            } else {
                b = xt_sub(lo, step_dt);
                hi = dist_b;
                lo = xt_from24(xt_acc_to_word(b));
            }
            out[i] = xt_acc_to_word(b);
        }
        s->dmp_a = xt_mid(lo);
        s->dmp_b = xt_mid(hi);
    }
    s->out_prev = out[31];
}

#ifndef MD_ST_TARGET
static void xt_trx_xt_render(void *state, float *out, unsigned frames)
{
    mdkit_trx_xt *s = (mdkit_trx_xt *)state;
    while (frames) {
        if (s->pos == 0u) {
            unsigned event=xt_trx_events_boundary(&s->clock,&s->events,0);
            if(event&MDKIT_TRX_PUBLISH)xt_build_controls(s,(event&MDKIT_TRX_TRIGGER)!=0);
            if(event&MDKIT_TRX_TRIGGER)xt_apply_trigger(s);
                    s->have_block = 0;
        }
        if (!s->events.triggered) {
            for(unsigned i=0;i<32;++i)s->block[i]=0;
            s->have_block=1;
        }
        if (!s->have_block) {

            const uint32_t st = s->stage;
            if (st == 0u) {
                xt_main(s, s->block);
            } else {
                uint32_t a = st - 1u;
                s->stage = a;
                if (a == 0u)      xt_attack_window(s, s->block, 32);
                else if (a - 1u == 0u) xt_attack_window(s, s->block, 0);
                else              xt_ring32(s, s->block);
            }
            s->have_block = 1;
        }
        unsigned n = 32u - s->pos;
        if (n > frames) n = frames;
        for (unsigned i = 0; i < n; ++i) {
            const uint32_t w = s->block[s->pos + i] & MD_MASK24;
            out[i] = (float)(int32_t)((w & 0x800000u) ? (int32_t)w - 0x1000000 : (int32_t)w) / 8388608.0f;
        }
        out += n; frames -= n; s->pos += n;
        if (s->pos >= 32u) s->pos = 0;
    }
}
#endif
/* Synfact: the same render, the words themselves */
void md_st_trx_xt_render_words(void *state, int32_t *out, unsigned frames)
{
    mdkit_trx_xt *s = (mdkit_trx_xt *)state;
    while (frames) {
        if (s->pos == 0u) {
            unsigned event=xt_trx_events_boundary(&s->clock,&s->events,0);
            if(event&MDKIT_TRX_PUBLISH)xt_build_controls(s,(event&MDKIT_TRX_TRIGGER)!=0);
            if(event&MDKIT_TRX_TRIGGER)xt_apply_trigger(s);
                    s->have_block = 0;
        }
        if (!s->events.triggered) {
            for(unsigned i=0;i<32;++i)s->block[i]=0;
            s->have_block=1;
        }
        if (!s->have_block) {

            const uint32_t st = s->stage;
            if (st == 0u) {
                xt_main(s, s->block);
            } else {
                uint32_t a = st - 1u;
                s->stage = a;
                if (a == 0u)      xt_attack_window(s, s->block, 32);
                else if (a - 1u == 0u) xt_attack_window(s, s->block, 0);
                else              xt_ring32(s, s->block);
            }
            s->have_block = 1;
        }
        unsigned n = 32u - s->pos;
        if (n > frames) n = frames;
        for (unsigned i = 0; i < n; ++i) {
            const uint32_t w = s->block[s->pos + i] & MD_MASK24;
            out[i] = (int32_t)(int32_t)((w & 0x800000u) ? (int32_t)w - 0x1000000 : (int32_t)w);
        }
        out += n; frames -= n; s->pos += n;
        if (s->pos >= 32u) s->pos = 0;
    }
}

static const char *const xt_trx_xt_control_names[MDKIT_TRX_XT_CONTROLS] = {
    "PTCH", "DEC", "RAMP", "RDEC", "DAMP", "DIST", "DTYP"
};

const mdkit_voice mdkit_voice_trx_xt = {
    "TRX-XT", 18u, MDKIT_TRX_XT_CONTROLS, xt_trx_xt_control_names,
    sizeof(mdkit_trx_xt),
    xt_trx_xt_init, xt_trx_xt_set_control, xt_trx_xt_trigger, MD_ST_RENDER(xt_trx_xt_render), mdkit_trx_xt_defaults
};
