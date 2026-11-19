/* SPDX-License-Identifier: GPL-2.0-or-later
 * Integer PCM engine. Frame/fraction cursors avoid the signed-Q12 duration
 * limit. All voice-private state lives in this machine bank, never core RAM.
 */
#include "sampler.h"
#include <syntakt/machines.h>
#include <syntakt/stereo.h>
/* A stereo transport test build adds stereo-test.h (SP_STEREO_TEST) to the
 * mod folder; the repository has none. */
#if defined(__has_include)
#if __has_include("stereo-test.h")
#include "stereo-test.h"
#endif
#endif
static struct sp_state voices[8];
sp_u32 sp_min(sp_u32 a,sp_u32 b){return a<b?a:b;}
sp_u32 sp_scale(sp_u32 n,sp_u32 raw)
{raw=sp_min(raw,30720);return (n/30720)*raw+((n%30720)*raw)/30720;}
static sp_u32 distance(sp_u32 a,sp_u32 b){return a>b?a-b:b-a;}
static sp_u32 width_of(const struct sp_sample *s){return s->channels==2?2:1;}
/* Zero crossings are searched on the first channel (L of a stereo sample). */
static sp_u32 search(const struct sp_sample *s,sp_u32 at,int direction)
{
    sp_u32 radius=sp_min(s->frames>>9,2040),j=at,n=0,c=width_of(s);
    if(!at && direction<0)return 0;
    if(at>=s->frames)return 0;
    if(at==s->frames-1 && direction>0)return at;
    do {
        j=direction<0?j-1:j+1;
        if(j>=s->frames)return at;
        if(!s->pcm[c*j] || (j+1<s->frames && (!s->pcm[c*j+c] || ((s->pcm[c*j]<0)!=(s->pcm[c*j+c]<0)))))return j;
        n++;
    }while(n<=radius);
    return at;
}
sp_u32 sp_point(const struct sp_sample *s,sp_u32 point)
{
    sp_u32 at,down,up;
    if(!point)return 0;if(point>=256)return s->frames;
    /* Pool frames are bounded by the physical PCM arena (< 2^24 frames). */
    at=(point*s->frames+128)>>8;down=search(s,at,-1);up=search(s,at,1);
    return distance(at,down)<distance(at,up)?down:up;
}
void sp_window(struct sp_state *s,sp_u32 start,sp_u32 end,sp_u32 loop,sp_u32 play,int restart)
{
    sp_u32 low;end=sp_min(end,s->source.frames);start=sp_min(start,end);
    if(!s->source.pcm || end<=start){s->active=0;s->start=s->end=0;return;}
    loop=sp_min(loop,end-1);play=sp_min(play,3);
    low=play==1?loop:start;
    if(restart || s->frame<low || s->frame>=end){
        s->frame=play<2?end-1:start;s->fraction=0;
        if(restart){s->active=1;s->wrapped=0;}
    }
    s->start=start;s->end=end;s->loop=loop;s->play=play;
}
/* Channel channel of frame, interpolated toward the next frame. */
static int interpolate(const struct sp_state *s,sp_u32 frame,sp_u32 fraction,sp_u32 channel)
{
    sp_u32 c=width_of(&s->source);
    int a=s->source.pcm[c*frame+channel],b=s->source.pcm[c*(frame+1<s->end?frame+1:frame)+channel];
    return a+((b-a)*(int)fraction>>12);
}
static void advance(struct sp_state *s,sp_u32 fade)
{
    sp_u32 whole=s->step>>12,frac=s->step&4095,low=s->play==1?s->loop:s->start;
    if(s->play>=2){
        frac+=s->fraction;whole+=frac>>12;s->fraction=frac&4095;s->frame+=whole;
        if(s->frame>=s->end){
            if(s->play==3){s->active=0;return;}
            low=s->loop+fade;
            if(low>=s->end){s->active=0;return;}
            s->frame=low+(s->frame-s->end)%(s->end-low);s->wrapped=1;
        }
    }else{
        if(frac>s->fraction)whole++;
        s->fraction=(s->fraction-frac)&4095;
        if(whole>s->frame-low){
            if(s->play==0){s->active=0;return;}
            if(fade>=s->end-low){s->active=0;return;}
            whole-=s->frame-low+1;
            s->frame=s->end-fade-1-whole%(s->end-low-fade);s->wrapped=1;
        }else s->frame-=whole;
    }
}
static int reduce_bits(int v,sp_u32 removed)
{
    sp_u32 magnitude;
    if(!removed)return v;
    /* Masking a two's-complement negative value rounds it away from zero:
     * at two bits, -1 becomes -16384 while +1 becomes 0. Quantize the
     * magnitude instead so silence stays silence and both signs match. */
    magnitude=(sp_u32)(v<0?-v:v);
    magnitude=(magnitude>>removed)<<removed;
    return v<0?-(int)magnitude:(int)magnitude;
}
static sp_u32 bit_reduction(sp_u32 br)
{
    /* The 0..127 control previously spread 14 bit-depth steps evenly:
     * even BR=32 only removed three LSBs. Give the lower half useful travel
     * while retaining full resolution at zero and the same 2-bit end point.
     * This is an approximation; the Digitakt's DSP curve is unpublished. */
    br=sp_min(br,127);
    return (br*(254-br)*14+8064)/16129;
}
static int overdrive(int v,sp_u32 gain)
{
    int x,absolute;
    if(gain==256)return v;
    /* The native SRC word +50 is an output gain, not a dry-to-drive control:
     * zeroing it silences the entire track. Apply a bounded soft clip to our
     * own PCM instead. The division stays 32-bit on MCF54415. */
    x=(v*(int)gain)>>8;absolute=x<0?-x:x;
    return (x*4096)/(4096+(absolute>>3));
}
#ifdef SP_STEREO_TEST
/* SP_STEREO_TEST (stereo transport test builds only): every track renders a
 * second channel, R, read from the same mono sample SP_HAAS frames behind L.
 * It costs what reading a stereo sample's second channel will cost, and the
 * delay makes a working transport audible as width. */
#define SP_HAAS 480u
static sp_s32 sp_haas(const struct sp_state *s,sp_u32 removed,sp_u32 gain)
{
    sp_u32 at=s->frame>=s->start+SP_HAAS?s->frame-SP_HAAS:s->frame;
    return (sp_s32)overdrive(reduce_bits(interpolate(s,at,s->fraction,0),removed),gain)*65536;
}
#endif
/* One channel at the cursor, with the loop crossfade. */
static int sp_value(const struct sp_state *s,sp_u32 width,sp_u32 channel)
{
    sp_u32 at,weight;int v=interpolate(s,s->frame,s->fraction,channel),other;
    if(width && s->play==2 && s->end-s->frame<=width){
        at=width-(s->end-s->frame);other=interpolate(s,s->loop+at,s->fraction,channel);
        weight=((at<<12)+s->fraction)*8/width;
        v+=((other-v)*(int)sp_min(weight,32768))>>15;
    }else if(width && s->play==1 && s->frame-s->loop<width){
        at=s->frame-s->loop;other=interpolate(s,s->end-width+at,s->fraction,channel);
        weight=((width-at)<<12)-s->fraction;weight=sp_min(weight*8/width,32768);
        v+=((other-v)*(int)weight)>>15;
    }
    return v;
}
/* One sample of the per-sample model, which handles every case: the loop
 * crossfade, the last frame of the window, a wrap or the end in the advance,
 * BR and OVER. */
static void pcm_one(struct sp_state *s,sp_s32 *out,sp_s32 *right,sp_u32 width,sp_u32 removed,sp_u32 gain,sp_u32 stereo)
{
    *out=(sp_s32)overdrive(reduce_bits(sp_value(s,width,0),removed),gain)*65536;
    if(right)*right=stereo?(sp_s32)overdrive(reduce_bits(sp_value(s,width,1),removed),gain)*65536:
#ifdef SP_STEREO_TEST
                           sp_haas(s,removed,gain);
#else
                           *out;          /* a mono sample on a track still ending a stereo tail */
#endif
    advance(s,width);
}
#ifdef __mcoldfire__
/* pcm_run's plain loops (no BR, no OVER) by hand, the same integer operations
 * as the C below: for count frames, i = at >> 12, a and b the samples of
 * frames i and i + 1, out = (a + ((b - a) x (at & 4095) >> 12)) << 16, then
 * at += step (negative in reverse). Mono, or both channels of a stereo
 * sample. tests/test_st_stereo_natives_emu.py compares them with the
 * per-sample model in analog-emu. */
static sp_s32 run_mono(const short *base,sp_s32 at,sp_s32 step,sp_u32 n,sp_s32 *out)
{
    sp_s32 i,a,b,f;sp_u32 shift=12;
    __asm__ volatile(
        "1:\n\tmove.l %[at],%[i]\n\tasr.l %[sh],%[i]\n\t"
        "move.l %[at],%[f]\n\tandi.l #4095,%[f]\n\t"
        "mvs.w (0,%[p],%[i].l*2),%[a]\n\tmvs.w (2,%[p],%[i].l*2),%[b]\n\t"
        "sub.l %[a],%[b]\n\tmuls.l %[f],%[b]\n\tasr.l %[sh],%[b]\n\tadd.l %[a],%[b]\n\t"
        "swap %[b]\n\tclr.w %[b]\n\tmove.l %[b],(%[o])+\n\t"
        "add.l %[s],%[at]\n\tsubq.l #1,%[n]\n\tbne.s 1b"
        :[at]"+d"(at),[o]"+a"(out),[n]"+d"(n),[i]"=&d"(i),[a]"=&d"(a),[b]"=&d"(b),[f]"=&d"(f)
        :[p]"a"(base),[s]"r"(step),[sh]"d"(shift):"cc","memory");
    return at;
}
static sp_s32 run_stereo(const short *base,sp_s32 at,sp_s32 step,sp_u32 n,sp_s32 *out,sp_s32 *right)
{
    sp_s32 i,a,b,f;sp_u32 shift=12;
    __asm__ volatile(
        "1:\n\tmove.l %[at],%[i]\n\tasr.l %[sh],%[i]\n\t"
        "move.l %[at],%[f]\n\tandi.l #4095,%[f]\n\t"
        "mvs.w (0,%[p],%[i].l*4),%[a]\n\tmvs.w (4,%[p],%[i].l*4),%[b]\n\t"
        "sub.l %[a],%[b]\n\tmuls.l %[f],%[b]\n\tasr.l %[sh],%[b]\n\tadd.l %[a],%[b]\n\t"
        "swap %[b]\n\tclr.w %[b]\n\tmove.l %[b],(%[o])+\n\t"
        "mvs.w (2,%[p],%[i].l*4),%[a]\n\tmvs.w (6,%[p],%[i].l*4),%[b]\n\t"
        "sub.l %[a],%[b]\n\tmuls.l %[f],%[b]\n\tasr.l %[sh],%[b]\n\tadd.l %[a],%[b]\n\t"
        "swap %[b]\n\tclr.w %[b]\n\tmove.l %[b],(%[r])+\n\t"
        "add.l %[s],%[at]\n\tsubq.l #1,%[n]\n\tbne.s 1b"
        :[at]"+d"(at),[o]"+a"(out),[r]"+a"(right),[n]"+d"(n),[i]"=&d"(i),[a]"=&d"(a),[b]"=&d"(b),[f]"=&d"(f)
        :[p]"a"(base),[s]"r"(step),[sh]"d"(shift):"cc","memory");
    return at;
}
#define SP_RUN_ASM(sign) do{if(!shaped && (stride==1 || both)){ \
        if(count)at=stride==1?run_mono(base,at,(sign)*(sp_s32)step,count,out): \
                              run_stereo(base,at,(sign)*(sp_s32)step,count,out,right); \
        k=count;goto ran;}}while(0)
#else
#define SP_RUN_ASM(sign) do{}while(0)
#endif
/* A run of samples where the per-sample model reduces to interpolation: the
 * cursor is outside the crossfade, the next frame is inside the window, and
 * the advance neither wraps nor ends. It computes what pcm_one would, sample
 * for sample, and returns how many it rendered (0 when the cursor is not in
 * such a run). stride: the sample's channels; both: R is its second channel;
 * shaped: BR or OVER is on; forward: PLAY is a forward mode. All are
 * constants at each call, so each combination is its own loop.
 *
 * The cursor (frame, fraction) moves by step, 12 fraction bits: as one
 * number, frame x 4096 + fraction gains or loses step per sample, which is
 * what advance() computes outside a wrap. A run keeps it relative to the
 * run's first frame (at most 32 x 257 frames away), so it fits 32 bits. */
static __inline__ __attribute__((always_inline)) sp_u32 pcm_run(struct sp_state *s,sp_s32 *out,sp_s32 *right,sp_u32 n,
    sp_u32 width,sp_u32 removed,sp_u32 gain,const int stride,const int both,const int shaped,const int forward)
{
    const short *base=s->source.pcm+stride*s->frame,*p;
    sp_u32 f=s->frame,step=s->step,whole=step>>12,reach=n*(whole+1),limit,low,k,count;
    sp_s32 at=(sp_s32)s->fraction,bound;int a,b,v;
#define SP_RUN_SAMPLE(to,ch) do{a=p[ch];b=p[stride+(ch)];v=a+(((b-a)*(int)(at&4095))>>12); \
        (to)[k]=shaped?(sp_s32)overdrive(reduce_bits(v,removed),gain)*65536:(sp_s32)v*65536;}while(0)
    if(forward){
        /* f + 1 < end, no crossfade (end - f > width), no wrap (f + whole + 1 < end) */
        if(s->end<=whole+1)return 0;
        limit=s->end-whole-1;
        if(width && s->play==2)limit=sp_min(limit,s->end>width?s->end-width:0);
        if(f>=limit)return 0;
        /* samples k = 0.. while at + k x step < bound */
        bound=(sp_s32)(sp_min(limit-f,reach+1)<<12);
        count=step?((sp_u32)(bound-at)-1)/step+1:n;if(count>n)count=n;
        SP_RUN_ASM(1);
        for(k=0;k<count;k++){
            p=base+stride*(at>>12);SP_RUN_SAMPLE(out,0);if(both)SP_RUN_SAMPLE(right,1);
            at+=(sp_s32)step;
        }
    }else{
        /* f + 1 < end, no crossfade (f - loop >= width), no wrap (f - low > whole) */
        if(f+1>=s->end)return 0;
        low=s->play==1?s->loop:s->start;limit=low+whole+1;
        if(width && s->play==1 && limit<s->loop+width)limit=s->loop+width;
        if(f<limit)return 0;
        /* samples k = 0.. while at - k x step >= bound */
        bound=-(sp_s32)(sp_min(f-limit,reach)<<12);
        count=step?(sp_u32)(at-bound)/step+1:n;if(count>n)count=n;
        SP_RUN_ASM(-1);
        for(k=0;k<count;k++){
            p=base+stride*(at>>12);SP_RUN_SAMPLE(out,0);if(both)SP_RUN_SAMPLE(right,1);
            at-=(sp_s32)step;
        }
    }
#ifdef __mcoldfire__
ran:
#endif
#undef SP_RUN_SAMPLE
    s->frame=f+(sp_u32)(at>>12);s->fraction=(sp_u32)at&4095;return k;
}
/* right: R, the second channel of a stereo sample; for a mono sample R = L
 * (or, in a transport test build, its Haas copy). Null renders L only.
 * The output is the per-sample model's (pcm_one) for every sample: runs
 * take the frames where it is plain interpolation, pcm_one the others. */
static void pcm_block(struct sp_state *s,sp_s32 *out,sp_s32 *right,int fast)
{
    sp_u32 i=0,n,width=0,span,removed=bit_reduction(s->br),stereo=s->source.channels==2;
    sp_u32 gain=256+(sp_min(s->control[7]>>8,127)*1792)/127;
    int shaped=removed || gain!=256,both=right && stereo,forward=s->play>=2;
    if(s->play==1 || s->play==2){
        span=s->end>s->loop?s->end-s->loop:0;
        width=sp_min(span/10,(240u*sp_min(s->step,0x100000u))>>12);
        if(width<2)width=0;
    }
#ifdef SP_STEREO_TEST
    if(right && !stereo)fast=0;          /* the Haas copy is per sample */
#endif
    while(i<32){
        if(!s->active || !s->source.pcm || s->frame>=s->end)break;
        if(fast){
            sp_s32 *o=out+i,*r=right?right+i:0;n=32-i;
#define SP_RUNS(stride,both) (forward?(shaped?pcm_run(s,o,r,n,width,removed,gain,stride,both,1,1):pcm_run(s,o,r,n,width,removed,gain,stride,both,0,1)): \
                                      (shaped?pcm_run(s,o,r,n,width,removed,gain,stride,both,1,0):pcm_run(s,o,r,n,width,removed,gain,stride,both,0,0)))
            n=!stereo?SP_RUNS(1,0):both?SP_RUNS(2,1):SP_RUNS(2,0);
#undef SP_RUNS
            if(right && !stereo)for(;n;n--,i++)right[i]=out[i];
            else i+=n;
            if(i==32)break;
        }
        pcm_one(s,out+i,right?right+i:0,width,removed,gain,stereo);i++;
    }
    for(;i<32;i++){out[i]=0;if(right)right[i]=0;}
}
void sp_pcm(struct sp_state *s,sp_s32 *out,sp_s32 *right){pcm_block(s,out,right,1);}
#ifdef SP_PCM_REFERENCE
/* Host tests: the per-sample model alone, to compare sp_pcm with. */
void sp_pcm_reference(struct sp_state *s,sp_s32 *out,sp_s32 *right){pcm_block(s,out,right,0);}
#endif
static int track(const void *v)
{sp_u32 at=(sp_u32)v-0x80000000u,t=at/0x708u;return t<8 && at==t*0x708u?(int)t:-1;}
void sp_update(sp_u32 kind,sp_u32 pitch,void *voice,const unsigned short *params)
{
    static const sp_u32 slots[8]={0x12,0x13,0x14,0x15,0x16,0x17,0x1a,0x19};
    struct sp_state *s;const struct st_pool_sample *sample;unsigned short native[71];
    sp_u32 i,trigger,note=pitch,tempo=*(const volatile sp_u32 *)0x80008184u;
    sp_u32 *v=voice;int t=track(voice);if(t<0)return;s=voices+t;trigger=v[0x38/4]!=0;
    if(!s->initialized || s->kind!=kind){
        unsigned char *b=(void *)s;for(i=0;i<sizeof(*s);i++)b[i]=0;
        s->initialized=1;s->kind=kind;trigger=1;
    }
    for(i=0;i<8;i++)s->control[i]=params[slots[i]];
    if(trigger){
        s->slot=sp_min(s->control[3]>>8,127);sample=st_machine_sample(s->slot);
        s->source.frames=sample?sample->frames:0;s->source.pcm=sample?sample->pcm:0;
        s->source.channels=sample?st_machine_sample_channels(s->slot):1;
        s->active=s->source.frames && s->source.pcm;s->wrapped=0;
    }else{
        sample=st_machine_sample(s->slot);
        if(!sample || sample->frames!=s->source.frames || sample->pcm!=s->source.pcm ||
           st_machine_sample_channels(s->slot)!=width_of(&s->source)){
            s->active=0;s->source.frames=0;s->source.pcm=0;
        }
    }
    for(i=0;i<71;i++)native[i]=params[i];
    native[0x12]=s->control[0];native[0x13]=0x4000;native[0x14]=0x4000;
    native[0x15]=0;native[0x16]=0x4000;native[0x17]=0x4000;native[0x18]=0;
    /* The common AUDIO word +50 is output gain. Keep it at unity; OVER is
     * shaped in sp_pcm so its zero setting leaves the sample audible. */
    native[0x19]=0x7f00;
    if(kind==SP_SLICE && !(s->control[4]>>8))note=60u<<16;
    ((void (*)(sp_u32,void *,const void *))0x400097dcu)(note,voice,native);
    s->step=sp_min(v[0x6c8/4],0x100000u);s->br=sp_min(s->control[2]>>8,127);
    if((int)tempo<=0)tempo=14400;tempo=sp_min(tempo,36000);
    switch(kind){
    case SP_ONE:sp_one_window(s,pitch,tempo,trigger);break;
    case SP_WERP:sp_werp_window(s,pitch,tempo,trigger);break;
    case SP_REPITCH:sp_repitch_window(s,pitch,tempo,trigger);break;
    default:sp_slice_window(s,pitch,tempo,trigger);break;
    }
    /* Retain the native filter/amp path, with one mono PCM reader. */
    v[2]=0x3fffffffu;v[3]=0;v[0x6e8/4]=0;
    if(trigger){v[6]=0x3fffffffu;v[7]=0;}
    v[0x6d4/4]=(sp_u32)s->source.pcm;v[0x6d8/4]=s->source.frames;
    v[0x6c4/4]=s->active;
}
/* Whether a track needs R this block (sp_render's comment has the reasons):
 * l and r, the previous block's output after the chain; pcm_l and pcm_r,
 * this block's PCM (pcm_r null: a mono sample, which only finishes a stereo
 * tail); trig, the voice is triggered this block; heard, whether the PCM
 * sounded last block (updated). */
sp_u32 sp_stereo_wanted(const sp_s32 *l,const sp_s32 *r,const sp_s32 *pcm_l,const sp_s32 *pcm_r,sp_u32 trig,sp_u32 *heard)
{
    sp_u32 now,want=(sp_u32)st_stereo_audible(r);
    if(!pcm_r)return want;
    now=(sp_u32)(st_stereo_audible(pcm_l) || st_stereo_audible(pcm_r));
    want=want || trig || st_stereo_audible(l) || (now && !*heard);
    *heard=now;
    return want;
}
void sp_render(void *out,void *voice)
{
    sp_s32 *a=(void *)0x80008c60u,*b=(void *)0x80008d84u;
    sp_u32 i,*v=voice;int locals[2]={0,1},t=track(voice);if(t<0)return;
    {const struct st_pool_sample *sample=st_machine_sample(voices[t].slot);
     if(!sample || sample->pcm!=voices[t].source.pcm || sample->frames!=voices[t].source.frames ||
        st_machine_sample_channels(voices[t].slot)!=(voices[t].source.channels==2?2u:1u))
         voices[t].active=0;}
    sp_s32 *ra=0,*rout=0;sp_u32 *rf=0,ramp,start[2],clock=0;
    /* The track is stereo while its output can sound, judged on the signal:
     * out and ST_STEREO_R(t) still hold the previous block after the chain
     * (the engine zeroes R of a track it did not run), so "sounding" is
     * either side at or above ST_STEREO_QUIET (-96 dBFS; the multimode's
     * residual never reaches 0). A stereo sample also claims the block it is
     * triggered in (voice + 0x38) and a block where its PCM starts after a
     * silent one, so no sound starts mono. Not "while the sample plays": in
     * STOP a looping sample plays on under a closed AMP envelope, silent, and
     * kept the R pass running. A tail that began stereo stays stereo to its
     * end, even after a mono sample takes over the track. A stereo sample's R
     * PCM is read before the decision (it is the cheap part); a transport
     * test build makes every sample stereo. */
    {sp_u32 sounding,stereo=voices[t].source.channels==2;const sp_s32 *l=out,*r=ST_STEREO_R(t);
#ifdef SP_STEREO_TEST
     stereo=1;
#endif
     sp_pcm(voices+t,a,stereo?ST_STEREO_SCRATCH:0);
     sounding=sp_stereo_wanted(l,r,a,stereo?ST_STEREO_SCRATCH:0,v[0x38/4]!=0,&voices[t].heard);
     if(sounding && (rout=st_stereo_claim((sp_u32)t))!=0){
        ra=ST_STEREO_SCRATCH;rf=ST_STEREO_FILTER(t);
        /* a mono sample under a stereo tail: R's PCM is L's */
        if(!stereo)for(i=0;i<32;i++)ra[i]=a[i];
#ifdef SP_STEREO_TEST
        clock=st_stereo_clock();
#endif
    }}
    st_machine_cursor_publish((sp_u32)t,(sp_u32)voices[t].source.pcm,voices[t].source.frames,voices[t].frame,voices[t].active);
#ifdef __mcoldfire__
    /* Stereo: L and R through the same native tail in one loop each
     * (voice-stereo.c), the same output word for word. */
    if(ra){
        rf[0]=v[0x6f8/4];rf[1]=v[0x6fc/4];
        st_voice_filter2(a,ra,(sp_u32 *)((unsigned char *)voice+0x6f8),rf);
        ((void (*)(void *))0x40003d42u)(voice);
        st_voice_gains2(a,ra,out,rout,voice);
        (void)ramp;(void)start;(void)locals;
    }else
#endif
    {
    for(i=0;i<32;i++)b[i]=0;
    ((void (*)(void *,void *,void *))0x400068eau)(a,a,(unsigned char *)voice+0x6f8);
    /* R runs the same four calls: the render filter with L's coefficients and
     * its own memory, and the two gain ramps from the values L started from. */
    if(ra){rf[0]=v[0x6f8/4];rf[1]=v[0x6fc/4];((void (*)(void *,void *,void *))0x400068eau)(ra,ra,rf);}
    ((void (*)(void *))0x40003d42u)(voice);
    ramp=v[0x3d4/4];
    ((void (*)(void *,void *))0x400045a0u)(a,voice);
    if(ra){v[0x3d4/4]=ramp;((void (*)(void *,void *))0x400045a0u)(ra,voice);}
    start[0]=v[0x18/4];start[1]=v[0x1c/4];
    ((void (*)(void *,void *,void *,void *,void *))0x400049f4u)(a,b,out,locals,voice);
    if(ra){v[0x18/4]=start[0];v[0x1c/4]=start[1];
        ((void (*)(void *,void *,void *,void *,void *))0x400049f4u)(ra,b,rout,locals,voice);}
    }
#ifdef SP_STEREO_TEST
    /* The whole stereo render is counted, L included: compare with mono. */
    if(ra)st_stereo_render_spent(clock);
#else
    (void)clock;
#endif
    v[0x6c4/4]=voices[t].active;
}
