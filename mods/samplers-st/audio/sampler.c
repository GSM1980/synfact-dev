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
/* right: R, the second channel of a stereo sample; for a mono sample R = L
 * (or, in a transport test build, its Haas copy). Null renders L only. */
void sp_pcm(struct sp_state *s,sp_s32 *out,sp_s32 *right)
{
    sp_u32 i,width=0,span,removed=bit_reduction(s->br),stereo=s->source.channels==2;
    sp_u32 gain=256+(sp_min(s->control[7]>>8,127)*1792)/127;
    if(s->play==1 || s->play==2){
        span=s->end>s->loop?s->end-s->loop:0;
        width=sp_min(span/10,(240u*sp_min(s->step,0x100000u))>>12);
        if(width<2)width=0;
    }
    for(i=0;i<32;i++){
        if(!s->active || !s->source.pcm || s->frame>=s->end){out[i]=0;if(right)right[i]=0;continue;}
        out[i]=(sp_s32)overdrive(reduce_bits(sp_value(s,width,0),removed),gain)*65536;
        if(right)right[i]=stereo?(sp_s32)overdrive(reduce_bits(sp_value(s,width,1),removed),gain)*65536:
#ifdef SP_STEREO_TEST
                                 sp_haas(s,removed,gain);
#else
                                 out[i];        /* a mono sample on a track still ending a stereo tail */
#endif
        advance(s,width);
    }
}
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
void sp_render(void *out,void *voice)
{
    sp_s32 *a=(void *)0x80008c60u,*b=(void *)0x80008d84u;
    sp_u32 i,*v=voice;int locals[2]={0,1},t=track(voice);if(t<0)return;
    {const struct st_pool_sample *sample=st_machine_sample(voices[t].slot);
     if(!sample || sample->pcm!=voices[t].source.pcm || sample->frames!=voices[t].source.frames ||
        st_machine_sample_channels(voices[t].slot)!=(voices[t].source.channels==2?2u:1u))
         voices[t].active=0;}
    sp_s32 *ra=0,*rout=0;sp_u32 *rf=0,ramp,start[2],clock=0;
    /* A stereo sample makes the track stereo while it plays and while the
     * chain still sounds on either side: out and ST_STEREO_R(t) still hold
     * the previous block after the chain (the engine zeroes R of a track it
     * did not run). A tail that began stereo stays stereo to its end, even
     * after a mono sample takes over the track. A silent track costs nothing
     * on either CPU. A transport test build makes every sample stereo. */
    {sp_u32 sounding=0,stereo=voices[t].source.channels==2;const sp_s32 *l=out,*r=ST_STEREO_R(t);
#ifdef SP_STEREO_TEST
     stereo=1;
#endif
     for(i=0;i<32 && !sounding;i++)sounding=r[i]!=0;
     if(!sounding && stereo){sounding=voices[t].active;for(i=0;i<32 && !sounding;i++)sounding=l[i]!=0;}
     if(sounding && (rout=st_stereo_claim((sp_u32)t))!=0){
        ra=ST_STEREO_SCRATCH;rf=ST_STEREO_FILTER(t);
#ifdef SP_STEREO_TEST
        clock=st_stereo_clock();
#endif
    }}
    sp_pcm(voices+t,a,ra);for(i=0;i<32;i++)b[i]=0;
    st_machine_cursor_publish((sp_u32)t,(sp_u32)voices[t].source.pcm,voices[t].source.frames,voices[t].frame,voices[t].active);
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
#ifdef SP_STEREO_TEST
    /* The whole stereo render is counted, L included: compare with mono. */
    if(ra)st_stereo_render_spent(clock);
#else
    (void)clock;
#endif
    v[0x6c4/4]=voices[t].active;
}
