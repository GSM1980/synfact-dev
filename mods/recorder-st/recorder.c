/* RAM recorder: 60 s mono or 30 s stereo PCM16 (interleaved L, R) in one
 * buffer, allocation only on the UI thread.
 * Included after syn_wave.c; audition uses Sample Manager's mixer primitive.
 * MAIN fx3_mix is already called every 32 frames at 48 kHz. */
static const char *sam_sources[]=SAMPLER_SOURCES;
static const char *sam_labels[]=SAMPLER_LABELS;
static u8 *sam_view DATA=0,*sam_manager DATA=0;
static u32 sam_iface_table[19] DATA={0};
static volatile u32 sam_source DATA=5; /* MAIN L+R: PLAY gives an immediate test. */
static volatile u32 sam_enabled DATA=0,sam_peak DATA=0,sam_clock DATA=0;
static u32 sam_last_source DATA=18,sam_rlen DATA=8,sam_thr DATA=14;
static volatile u32 sam_mon DATA=0;
static volatile u32 sam_post DATA=0,sam_take_post DATA=0; /*  POST FX, encoder B */
/* STEREO, encoder A. A take keeps the mode it was started in (sam_take_stereo):
 * its PCM is interleaved L, R and sam_frames counts frame pairs. */
static volatile u32 sam_stereo DATA=0,sam_take_stereo DATA=0,sam_peak_r DATA=0;
/* Stock AMP.T/FLT.T toggle renderer. Borrows the boot-owned bitmap vector. */
#define SAM_TOGGLE REC_TOGGLE
static u32 sam_sound_down DATA=0,sam_sound_pending DATA=0,sam_sound_at DATA=0;
static u32 sam_sound_release DATA=0,sam_focus DATA=2,sam_hint DATA=0;
static u8 *sam_sound_origin DATA=0,*sam_sound_browser DATA=0;
u32 sam_report[8] DATA={0}; /* opened,ticks,paints,idles,encoder edits,source,peak,closed */
#define SAM_MAX_FRAMES (60u*48000u)
#define SAM_STEREO_FRAMES (SAM_MAX_FRAMES/2u)   /* the same buffer, two channels */
static u32 sam_max_frames(u32 stereo){return stereo?SAM_STEREO_FRAMES:SAM_MAX_FRAMES;}
/* STEREO offers the stereo pairs and the tracks: IN L+R, MAIN L+R, TRK1..12. */
static u32 sam_source_ok(u32 source,u32 stereo)
{return !stereo || (source!=0 && source!=1 && source!=3 && source!=4);}
#define SAM_NORM_PEAK 29204u /* nearest PCM16 magnitude to -1 dBFS */
enum { SAM_EMPTY, SAM_ARMED, SAM_RECORDING, SAM_READY, SAM_NORMALIZING };
static short *sam_pcm DATA=0;
static volatile u32 sam_state DATA=SAM_EMPTY,sam_frames DATA=0;
static volatile u32 sam_play_length DATA=0,sam_play_pos DATA=0;
static u32 sam_limit DATA=SAM_MAX_FRAMES,sam_trigger DATA=0,sam_take_source DATA=5;
static u32 sam_keys_down DATA=0;
static u32 sam_no_clear DATA=0;
static u32 sam_seq_stop DATA=0; /* a step-length take stops the sequencer at its end */
static u32 sam_name_wait DATA=0;
static u32 sam_pcm_peak DATA=0,sam_norm_pos DATA=0,sam_norm_gain DATA=65536;
static u32 sam_norm_init DATA=0,sam_norm_shown DATA=0,sam_norm_bin DATA=0,sam_norm_next DATA=0;
static u32 sam_wave_ready DATA=0;
static short sam_wave_low[126] DATA={0},sam_wave_high[126] DATA={0};
static short sam_wave_low_r[126] DATA={0},sam_wave_high_r[126] DATA={0};   /* R of a stereo take */
#define SAM_DETAIL_BLOCK 256u
#define SAM_DETAIL_BINS ((SAM_MAX_FRAMES+SAM_DETAIL_BLOCK-1u)/SAM_DETAIL_BLOCK)
/* Range extrema index, built during the existing normalization pass. No PCM
 * duplicate or whole-take traversal from paint; 45,000 bytes in the file.
 * A stereo take (at most half the frames) keeps L in the first half, R in
 * the second (SAM_DETAIL_HALF). */
static short sam_detail_low[SAM_DETAIL_BINS] DATA={0},sam_detail_high[SAM_DETAIL_BINS] DATA={0};
#define SAM_DETAIL_HALF (SAM_DETAIL_BINS/2u)
static u32 sam_detail_frames DATA=0,sam_display_peak DATA=1;
static u32 sam_trim_start DATA=0,sam_trim_end DATA=0,sam_trim_valid DATA=0;
static u32 sam_zoom[2] DATA={0,0};
static u32 sam_loupe_side DATA=0,sam_loupe_at DATA=0;
static u32 sam_trimmed DATA=0;
static void sam_trim_init(void);
#include "save.c"

/* Trim is nondestructive: capture/normalization keep the original PCM. The
 * audition endpoint and the save snapshot apply [start,end), with at least
 * one frame. Reopening the editor preserves the current selection. */
static void sam_trim_init(void)
{
    if(sam_trim_valid && sam_trim_start<sam_trim_end && sam_trim_end<=sam_frames)return;
    sam_trim_start=0;sam_trim_end=sam_frames;
    sam_trim_valid=sam_frames?1:0;sam_zoom[0]=sam_zoom[1]=0;sam_loupe_side=0;
}

/* Single-core UI/IRQ publication: disable first; the audio IRQ never allocates,
 * frees, accesses disk or calls UI. PCM remains owned while the UI is closed. */
void sampler_suspend(void)
{
    u32 state=sam_state;
    sam_state=SAM_EMPTY;
    sam_play_length=0;
    __asm__ volatile("" ::: "memory");
    if(state==SAM_ARMED || state==SAM_RECORDING)
        sam_state=sam_frames?SAM_NORMALIZING:SAM_EMPTY;
    else sam_state=state;
}
static u32 sam_prepare(void)
{
    if(sam_pcm)return 1;
    sam_pcm=MALLOC(2u*SAM_MAX_FRAMES);
    if(!heap_ok(sam_pcm)){sam_pcm=0;sam_hint=1;return 0;}
    sam_hint=0;return 1;
}
static void sam_clear(void)
{
    sampler_suspend();
    sam_state=SAM_EMPTY;sam_frames=0;sam_play_pos=0;sam_hint=0;
    sam_pcm_peak=0;sam_norm_pos=0;sam_norm_init=0;sam_norm_shown=0;sam_wave_ready=0;
    sam_saved_id=0;sam_save_message[0]=0;sam_name_wait=0;sam_assign_phase=SAM_ASSIGN_IDLE;
    sam_trim_valid=0;sam_loupe_side=0;sam_trimmed=0;sam_detail_frames=0;sam_seq_stop=0;
}
static u32 sam_record_limit(void)
{
    u32 bpm=REC_BPM,steps,frames;
    if(sam_rlen>=8)return SAM_MAX_FRAMES;
    if(bpm<3600 || bpm>36000)bpm=14400; /* native BPM*120, fallback 120 BPM */
    steps=1u<<sam_rlen;
    frames=(86400000u/bpm)*steps+((86400000u%bpm)*steps)/bpm;
    return frames<SAM_MAX_FRAMES?frames:SAM_MAX_FRAMES;
}
static void sam_start(u32 armed)
{
    u32 level,peak,shift;
    if(sam_frames || !sam_prepare())return;
    preview_stop();sampler_suspend();
    sam_frames=0;sam_take_stereo=sam_stereo;sam_limit=sam_record_limit();
    if(sam_limit>sam_max_frames(sam_take_stereo))sam_limit=sam_max_frames(sam_take_stereo);
    if(!sam_source_ok(sam_source,sam_take_stereo))sam_source=sam_source<3?2:5;
    sam_take_source=sam_source;sam_take_post=sam_post;
    sam_seq_stop=sam_rlen<8;
    sam_pcm_peak=0;sam_norm_pos=0;sam_norm_init=0;sam_norm_shown=0;sam_wave_ready=0;
    sam_trim_valid=0;sam_loupe_side=0;sam_trimmed=0;sam_detail_frames=0;
    /* Inverse of the meter's logarithmic scale, computed outside the IRQ. */
    level=sam_thr*68u/127u;shift=level/4u;
    peak=0x4000u+(level%4u)*0x1000u;
    sam_trigger=shift>=17?0x7fffffffu:peak<<shift;
    sam_hint=0;
    __asm__ volatile("" ::: "memory");
    recorder_prime();sam_state=armed?SAM_ARMED:SAM_RECORDING;
}
static void sam_audition(void)
{
    if(sam_state!=SAM_READY || !sam_frames || !sam_pcm)return;
    sam_trim_init();
    sam_save_message[0]=0;
    preview_stop();sam_play_length=0;sam_play_pos=sam_trim_start;
    __asm__ volatile("" ::: "memory");
    sam_play_length=sam_trim_end;
}
void sampler_playback_mix(void)
{
    u32 n=sam_play_length,p=sam_play_pos,i;
    if(!n || !sam_pcm)return;
    if(n>sam_max_frames(sam_take_stereo) || p>n){sam_play_length=0;return;}
    if(sam_take_stereo)for(i=0;i<32 && p<n;i++,p++){
        preview_mix_side(2*i,(s32)sam_pcm[2*p]*2048);preview_mix_side(2*i+1,(s32)sam_pcm[2*p+1]*2048);
    }
    else for(i=0;i<32 && p<n;i++,p++)
        preview_mix_frame(i,(s32)sam_pcm[p]*2048);
    sam_play_pos=p;
    if(p==n)sam_play_length=0;
}
/* SRC as shown: in STEREO the pairs read IN LR and MAIN LR. */
static const char *sam_source_name(u32 source,u32 stereo)
{
    if(source>=18)source=0;
    if(stereo && source==2)return "IN LR";
    if(stereo && source==5)return "MAIN LR";
    return sam_sources[source];
}
static void sam_time_text(char *text,u32 frames)
{
    u32 seconds=frames/48000u;
    text[0]='0'+seconds/10u;text[1]='0'+seconds%10u;text[2]='.';
    text[3]='0'+(frames%48000u)/4800u;text[4]='s';text[5]=0;
}
/* UI-only, bounded in-place pass. The capture IRQ supplies the PCM16 peak;
 * fixed Q16 gain avoids a divide per sample. No second PCM allocation.
 * At least one NORMALIZING paint precedes work when the recorder is visible. */
static void sam_normalize_step(u32 visible)
{(void)visible;normalize_fast();}

static u32 sam_foreground(void)
{
    u8 *node;
    if(!sam_view || !sam_manager || sam_view[0x30]) return 0;
    node=*(u8 **)(sam_manager+0x1c);
    return node && node!=sam_manager+0x14 && *(u8 **)(node+8)==sam_view;
}
/*  POST FX. At the fx3_mix hook (4008fb04) this block's delay (8000dcd0)
 * and reverb (8000ddd0) returns, interleaved L/R, are final, and the native
 * 10-channel sums (4008ef14) have left their ramped Q31 gains: direct bus
 * L e188 / R e160, FX-track bus L e138 / R e110; channel 8 = delay return,
 * 9 = reverb return (VOL, routing and FX mute included). The FX-track bus is
 * weighted 8/10 against the direct bus (4008eeca stages). Q15 gains, no EMAC:
 * the native mixer owns MACSR inside this IRQ. */
static s32 sam_gain(u32 direct,u32 fx,u32 ch)
{
    s32 d=((const volatile s32 *)direct)[ch]>>16,f=((const volatile s32 *)fx)[ch]>>16;
    return d+((f*819)>>10);
}
/* RECORDER_MIX MAIN. USB slots 0/1 = 8 x (ADC sum 8000ded0 + FX-track bus 8000dfd0)
 * (4008ee80 with gain 80000000, then 4008eeca x8); the direct bus 8000dad0
 * is not in it. So MAIN already holds the returns routed to the FX track and
 * lacks the direct ones. Per block, PCM16 units, L/R interleaved:
 * sam_ret[0] = x8 FX-track returns (PRE removes them),
 * sam_ret[1] = x10 direct returns (POST adds them). MAIN is read one block
 * late, so the previous block's values apply; stale values are not used. */
static s32 sam_ret[2][64] DATA={{0}};
static u32 sam_ret_clock DATA=0;
static void sam_returns(void)
{
    const volatile s32 *del=(const volatile s32 *)0x8000dcd0u,*rev=(const volatile s32 *)0x8000ddd0u;
    const volatile s32 *t;
    s32 g[2][2][2],d,r,c;u32 i,b,k;
    /* Opaque native addresses: GCC must not fold them with sam_ret into a
     * single non-relocatable constant (sam_ret - 8000dcd0). */
    __asm__("" : "+r"(del),"+r"(rev));
    for(b=0;b<2;b++)for(k=0;k<2;k++) {
        t=(const volatile s32 *)(k?(b?0x8000e160u:0x8000e188u):(b?0x8000e110u:0x8000e138u));
        /* Rounded (not floored) Q15/PCM16: PRE cancels within a few LSB. */
        g[k][b][0]=((t[8]>>15)+1)>>1;g[k][b][1]=((t[9]>>15)+1)>>1;
    }
    for(i=0;i<64;i++) {
        b=i&1u;d=((del[i]>>15)+1)>>1;r=((rev[i]>>15)+1)>>1;
        for(k=0;k<2;k++) {
            /* -(g*x) as the native sums; c = PCM16 << 14. */
            c=-(((g[k][b][0]*d)>>1)+((g[k][b][1]*r)>>1));
            sam_ret[k][i]=k?(c>>13)*5:c>>11;
        }
    }
    sam_ret_clock=sam_clock;
}
static s32 sam_main_side(s32 value,s32 correction)
{
    s32 x=(value>>16)+correction;
    if(x>32767)x=32767;
    if(x< -32768)x=-32768;
    return (s32)((u32)x<<16);
}
/* STEREO, every source: 32 frames of L and R (Q31) -> meters, threshold,
 * interleaved PCM16. The trigger and the PCM peak take the louder side. */
static void sam_stereo_store(const s32 *left,const s32 *right,u32 source,u32 state)
{
    u32 i,b,mag,side[2],old,peak[2]={0,0},p=sam_frames,pcm_peak=sam_pcm_peak,pcm_mag,limit;
    s32 value;
    limit=sam_limit<SAM_STEREO_FRAMES?sam_limit:SAM_STEREO_FRAMES;
    for(i=0;i<32;i++) {
        for(b=0;b<2;b++){value=b?right[i]:left[i];side[b]=value<0?0u-(u32)value:(u32)value;if(side[b]>peak[b])peak[b]=side[b];}
        mag=side[0]>side[1]?side[0]:side[1];
        if(state==SAM_ARMED && mag>sam_trigger)state=SAM_RECORDING;
        if(state==SAM_RECORDING) {
            if(sam_pcm && p<limit) {
                for(b=0;b<2;b++) {
                    value=(b?right[i]:left[i])>>16;sam_pcm[2*p+b]=(short)value;
                    pcm_mag=value<0?0u-(u32)value:(u32)value;if(pcm_mag>pcm_peak)pcm_peak=pcm_mag;
                }
                if(++p==limit)state=SAM_NORMALIZING;
            } else state=p?SAM_NORMALIZING:SAM_EMPTY;
        }
    }
    if(sam_state==SAM_ARMED || sam_state==SAM_RECORDING) {
        sam_frames=p;sam_pcm_peak=pcm_peak;
        __asm__ volatile("" ::: "memory");
        sam_state=state;
    }
    old=sam_last_source==source?sam_peak:0;old-=old?(old>>9)+1u:0;sam_peak=peak[0]>old?peak[0]:old;
    old=sam_last_source==source?sam_peak_r:0;old-=old?(old>>9)+1u:0;sam_peak_r=peak[1]>old?peak[1]:old;
    sam_last_source=source;
    sam_report[1]++;sam_report[5]=source;sam_report[6]=peak[0]>peak[1]?peak[0]:peak[1];
}
/* A stereo sampler track's R is L + D (samplers-st stereo/main.c, in PCM16
 * units). Weak: a bank without the samplers links it to zero. */
extern int st_stereo_track_d(u32 track,s32 *out) __attribute__((weak));
static s32 sam_clip(s32 x){return x>32767?32767:x< -32768?-32768:x;}
/* STEREO inputs (IN L+R) and tracks, as heard: each side with its own gain.
 * A digital track takes its level x pan gains of this block (an LFO on PAN
 * included), an analog track its ADC-bus gains; POST FX adds each side's own
 * delay and reverb returns. Q15 gains x PCM16 >> 15: one side, unity = x. */
static void sampler_stereo_tick(u32 source,u32 post,u32 state)
{
    const volatile s32 *del=(const volatile s32 *)0x8000dcd0u,*rev=(const volatile s32 *)0x8000ddd0u;
    const volatile s32 *in=(const volatile s32 *)0x80003b50u,*track=0;
    s32 out[2][32],d[32],g[2]={0,0},gd[2]={0,0},gr[2]={0,0},x,acc;u32 i,b,direct,fx,has_d=0;
    __asm__("" : "+r"(del),"+r"(rev),"+r"(in)); /* see sam_returns */
    if(source>=6 && source<14) {
        track=(const volatile s32 *)(0x80003d50u+(source-6)*128u);
        for(b=0;b<2;b++)g[b]=sam_gain(b?0x8000e160u:0x8000e188u,b?0x8000e110u:0x8000e138u,source-6);
        has_d=st_stereo_track_d && st_stereo_track_d(source-6,d);
    } else if(source>=14) {
        track=(const volatile s32 *)(0x80003950u+(source-14)*128u);
        for(b=0;b<2;b++)g[b]=((const volatile s32 *)(b?0x8000e0d0u:0x8000e0f0u))[source-14]>>16;
    }
    if(post)for(b=0;b<2;b++) {
        direct=b?0x8000e160u:0x8000e188u;fx=b?0x8000e110u:0x8000e138u;
        gd[b]=sam_gain(direct,fx,8);gr[b]=sam_gain(direct,fx,9);
    }
    for(i=0;i<32;i++)for(b=0;b<2;b++) {
        acc=post?((gd[b]*(del[2*i+b]>>16))>>3)+((gr[b]*(rev[2*i+b]>>16))>>3):0;
        if(track) {
            x=track[i]>>16;if(b && has_d)x+=d[i];
            acc+=(g[b]*x)>>3;x=-(acc>>12);
        } else x=(in[i+32*b]>>16)-(acc>>12);
        out[b][i]=(s32)((u32)sam_clip(x)<<16);
        /* MON: external inputs only, each on its own side. */
        if(b==1 && sam_mon && !track && ((state==SAM_EMPTY && !sam_frames) || state==SAM_ARMED || state==SAM_RECORDING)) {
            preview_mix_side(2*i,out[0][i]>>5);preview_mix_side(2*i+1,out[1][i]>>5);
        }
    }
    sam_stereo_store(out[0],out[1],source,state);
}
/* Sources are read-only; the IRQ writes only our owned PCM and scalar state.
 * Peak decay is producer-owned, with no read/reset race. */
static void sampler_base_tick(void)
{
    const volatile s32 *left,*right=0;
    const volatile s32 *del=(const volatile s32 *)0x8000dcd0u,*rev=(const volatile s32 *)0x8000ddd0u;
    u32 source=sam_source,peak=0,i,mag,old,stride=1,state=sam_state,p=sam_frames,pcm_peak=sam_pcm_peak,pcm_mag;
    u32 post=sam_post,b,direct,fx,main=0,side=0,ready;
    s32 value,pcm_value,acc,gt[2],gd[2],gr[2];
    __asm__("" : "+r"(del),"+r"(rev)); /* see sam_returns */
    sam_clock+=32;
    if(!sam_enabled || source>=18) return;
    if(state==SAM_ARMED || state==SAM_RECORDING){source=sam_take_source;post=sam_take_post;}
    if((state==SAM_ARMED || state==SAM_RECORDING)?sam_take_stereo:sam_stereo) {
        if(source<3 || source>=6)sampler_stereo_tick(source,post,state);
        return;
    }
    /* MAIN: PRE/POST corrections below, not the bus terms of the other sources. */
    if(source>=3 && source<6){main=post?2:1;post=0;side=source==4;}
    ready=sam_ret_clock==sam_clock-32u;
    if(post) {
        for(b=0;b<2;b++) {
            direct=b?0x8000e160u:0x8000e188u;fx=b?0x8000e110u:0x8000e138u;
            gt[b]=source>=6 && source<14?sam_gain(direct,fx,source-6):0;
            gd[b]=sam_gain(direct,fx,8);gr[b]=sam_gain(direct,fx,9);
        }
    }
    if(source<3) {
        left=(const volatile s32 *)0x80003b50u;
        i=source;
        if(i==1) left+=32;
        else if(i==2) right=left+32;
    } else if(source<6) {
        /* Native USB main pair, completed by 4008fb88/4008fb92 in the
         * previous block. ADC decoding only replaces slots 10..15/18..19,
         * so slots 0/1 survive until that later copy. Reading here adds one
         * 32-frame block of meter latency, without a new hook or audio mix. */
        left=(const volatile s32 *)0x80004760u;stride=20;
        if(source==4)left++;
        else if(source==5)right=left+1;
    } else if(source<14) left=(const volatile s32 *)(0x80003d50u+(source-6)*128u);
    else left=(const volatile s32 *)(0x80003950u+(source-14)*128u);
    for(i=0;i<32;i++) {
        value=left[i*stride];
        if(main && ready) {
            value=sam_main_side(value,main==2?sam_ret[1][2*i+side]:-sam_ret[0][2*i+side]);
            if(right)value=(value>>1)+(sam_main_side(right[i*stride],
                main==2?sam_ret[1][2*i+1]:-sam_ret[0][2*i+1])>>1);
        } else if(right) value=(value>>1)+(right[i*stride]>>1);
        /* MON auditions only external inputs, in mono, on the existing dry
         * preview bus. Same headroom/sign as PCM audition, with no PCM16
         * quantization here. Never feed MAIN/TRK back into its own output. */
        if(sam_mon && source<3 && ((state==SAM_EMPTY && !p) || state==SAM_ARMED || state==SAM_RECORDING))
            preview_mix_frame(i,value>>5);
        if(post) {
            /* Mono (L+R)/2 of the bus terms, -sum(g*x) as the native sums.
             * Six Q15 x PCM16 terms >>3 cannot overflow; acc = PCM16 << 13.
             * Digital tracks use their own bus gains (level, pan, mute);
             * analog tracks and inputs keep the raw dry signal. */
            acc=((gd[0]*(del[2*i]>>16))>>3)+((gd[1]*(del[2*i+1]>>16))>>3)
               +((gr[0]*(rev[2*i]>>16))>>3)+((gr[1]*(rev[2*i+1]>>16))>>3);
            if(source>=6 && source<14) {
                acc+=((gt[0]*(value>>16))>>3)+((gt[1]*(value>>16))>>3);
                acc=-(acc>>13);
            } else acc=(value>>16)-(acc>>13);
            if(acc>32767)acc=32767;
            if(acc< -32768)acc=-32768;
            value=(s32)((u32)acc<<16);
        }
        mag=value<0?0u-(u32)value:(u32)value;
        if(mag>peak) peak=mag;
        if(state==SAM_ARMED && mag>sam_trigger)state=SAM_RECORDING;
        if(state==SAM_RECORDING) {
            if(sam_pcm && p<sam_limit && p<SAM_MAX_FRAMES) {
                pcm_value=value>>16;sam_pcm[p++]=(short)pcm_value;
                pcm_mag=pcm_value<0?0u-(u32)pcm_value:(u32)pcm_value;
                if(pcm_mag>pcm_peak)pcm_peak=pcm_mag;
                if(p==sam_limit || p==SAM_MAX_FRAMES)state=SAM_NORMALIZING;
            } else state=p?SAM_NORMALIZING:SAM_EMPTY;
        }
    }
    if(main)sam_returns(); /* this block's returns, for next block's MAIN */
    if(sam_state==SAM_ARMED || sam_state==SAM_RECORDING) {
        sam_frames=p;
        sam_pcm_peak=pcm_peak;
        __asm__ volatile("" ::: "memory");
        sam_state=state;
    }
    old=sam_last_source==source?sam_peak:0;
    old-=old?(old>>9)+1u:0;
    sam_peak=peak>old?peak:old;
    sam_last_source=source;
    sam_report[1]++;sam_report[5]=source;sam_report[6]=peak;
}
/* Compact monotonic logarithmic display: about -102 dBFS .. 0 dBFS.
 * No log/float calls in the IRQ. Digitakt's meter uses a logarithmic mapper. */
u32 sampler_meter_width(u32 peak)
{
    u32 shift=0,whole,fraction;
    if(peak<0x4000u) return 0;
    if(peak>=0x7fffffffu) return 62;
    while(peak>=0x8000u) {peak>>=1;shift++;}
    whole=shift*4u;fraction=((peak-0x4000u)*4u)>>14;
    return (whole+fraction)*62u/68u;
}
static void sam_redraw(void) {if(sam_view)UI_DIRTY(sam_view);}
void sampler_dispose(u8 *view)
{
    if(view==sam_view) {
        sampler_suspend();sam_keys_down=0;
        sam_enabled=0;sam_view=0;sam_manager=0;sam_peak=sam_peak_r=0;
        sam_sound_pending=0;sam_assign_phase=SAM_ASSIGN_IDLE;sam_report[7]++;
        if(sam_save_phase)sam_save_cleanup("SAVE CANCELLED");else recorder_save_release();
    }
    REC_VIEW_DTOR(view);
}
static void recorder_ui_tick(void *controller)
{
    u32 began=sam_clock,q,before;(void)controller;
    if(!sam_view){sam_enabled=0;return;}
    sam_enabled=sam_foreground();
    if(sam_enabled)sam_name_view=0;
    if(!sam_enabled)sampler_suspend();
    /* A take that filled its steps stops a playing sequencer before the
     * normalization; YES/STOP or a closed view end it without stopping. */
    if(sam_seq_stop && sam_state!=SAM_ARMED && sam_state!=SAM_RECORDING) {
        sam_seq_stop=0;
        if(sam_enabled && sam_frames && sam_frames>=sam_limit && REC_SEQ_STATE==1u)REC_SEQ_STOP();
    }
    for(q=0;q<32 && sam_state==SAM_NORMALIZING;q++){
        before=sam_norm_pos;sam_normalize_step(sam_enabled);
        if(before==sam_norm_pos || sam_clock-began>=192u)break;
    }
    if(sam_enabled && sam_assign_phase)sam_assign_step();
    for(q=0;q<32 && sam_enabled && sam_save_phase;q++){
        sam_save_step();if(sam_clock-began>=192u)break;
    }
    if(sam_enabled){
        sam_report[3]++;
        if(sam_clock-recorder_last_paint>=3200u){recorder_last_paint=sam_clock;sam_redraw();}
    }
}
#include "trim.c"
/* Fields: 0 RLEN, 1 THR, 2 SRC, 3 MON, 4 POST FX, 5 STEREO. */
static void sam_adjust(u32 field,s32 delta)
{
    s32 value,limit,step;
    if(!delta || field>5) return;
    if((sam_state==SAM_ARMED || sam_state==SAM_RECORDING || sam_state==SAM_NORMALIZING) && field!=3)return;
    if(delta>8)delta=8;
    if(delta< -8)delta=-8;
    if(field==2) {
        /* One step per detent, skipping what STEREO does not offer. */
        value=(s32)sam_source;step=delta>0?1:-1;
        for(;delta;delta-=step) {
            s32 next=value+step;
            while(next>=0 && next<=17 && !sam_source_ok((u32)next,sam_stereo))next+=step;
            if(next<0 || next>17)break;
            value=next;
        }
        sam_source=(u32)value;sam_peak=sam_peak_r=0;sam_hint=0;
        sam_focus=field;sam_report[4]++;sam_redraw();return;
    }
    value=(s32)(field==0?sam_rlen:field==1?sam_thr:field==3?sam_mon:field==4?sam_post:sam_stereo);
    limit=field==0?8:field==1?127:1;
    value+=delta;if(value<0)value=0;if(value>limit)value=limit;
    if(field==0)sam_rlen=(u32)value;
    else if(field==1)sam_thr=(u32)value;
    else if(field==3)sam_mon=(u32)value;
    else if(field==4){sam_post=(u32)value;sam_peak=sam_peak_r=0;}
    else {
        /* STEREO: the mono single sides become their stereo pair. */
        sam_stereo=(u32)value;sam_peak=sam_peak_r=0;
        if(!sam_source_ok(sam_source,sam_stereo))sam_source=sam_source<3?2:5;
    }
    if(field<4)sam_focus=field; /* STEREO (A) and POST FX (B) are not arrow-key fields */
    sam_report[4]++;sam_redraw();
}
u32 sampler_encoder(u8 *iface,const u8 *ev)
{
    u32 encoder=be32(ev+12);
    if(!sam_foreground() || iface!=sam_view+4) return 0;
    /* Native EncoderEvent: ids 1..8 = A..H, signed delta at +16. */
    if(sam_state==SAM_READY && sam_wave_ready) {
        if(!sam_trimmed && !sam_save_phase && encoder>=1 && encoder<=4)sam_trim_adjust(encoder-1,(s32)be32(ev+16));
    } else if(encoder>=5 && encoder<=8) sam_adjust(encoder-5,(s32)be32(ev+16));
    else if(encoder==1) sam_adjust(5,(s32)be32(ev+16));   /* A: STEREO */
    else if(encoder==2) sam_adjust(4,(s32)be32(ev+16));   /* B: POST FX */
    return encoder>=1 && encoder<=9;
}
static u32 sampler_key_base(u8 *iface,const u8 *ev)
{
    u32 key=be32(ev+12),flags=be32(ev+16);
    if(!sam_foreground() || iface!=sam_view) return 0;
    if(sam_save_phase || sam_assign_phase)return 1;
    if(key==11) {if(flags&1)sampler_suspend();return 0;}
    if(key==13) {
        /* TRIMMED has only UNTRIM. Ignore FUNC+NO on both edges; the
         * release may arrive after FUNC has already been released. */
        if(sam_trimmed) {
            if(flags&1) {
                if((flags&8) || (sam_keys_down&2))return 1;
                sam_keys_down|=2;sam_no_clear=1;
                if(!(flags&2)){sam_trimmed=0;sam_play_length=0;sam_loupe_side=0;sam_redraw();}
            } else {sam_keys_down&=~2u;sam_no_clear=0;}
            return 1;
        }
        if(flags&1) {
            if((flags&8) || (sam_keys_down&2))return 1;
            sam_keys_down|=2;sam_no_clear=flags&2;
            if(sam_no_clear){sam_clear();sam_redraw();}
        } else {
            sam_keys_down&=~2u;
            if(!sam_no_clear && !(flags&2))UI_CLOSE(iface);
            sam_no_clear=0;
        }
        return 1;
    }
    if(key==12) {
        if(!(flags&1)){
            sam_keys_down&=~1u;
            if(sam_name_wait){sam_name_wait=0;sam_name_open();}
            return 1;
        }
        if((flags&8) || (sam_keys_down&1))return 1;
        sam_keys_down|=1;
        if(sam_state==SAM_NORMALIZING){sam_redraw();return 1;}
        if(sam_state==SAM_RECORDING)sampler_suspend();
        else if(sam_state==SAM_ARMED && !(flags&2))sampler_suspend();
        else if(sam_frames) {
            if(flags&2)sam_audition();
            else if(sam_trimmed)sam_name_wait=1;
            else {sam_trim_init();sam_trimmed=1;sam_play_length=0;sam_loupe_side=0;sam_save_message[0]=0;}
        }
        else sam_start((flags&2)?0:1);
        sam_redraw();return 1;
    }
    if(key>=14 && key<=17) {
        if(flags&1) {
            if(key==16 && sam_focus)sam_focus--;
            if(key==17 && sam_focus<3)sam_focus++;
            if(key==14 || key==15) {
                if(sam_state==SAM_READY && sam_wave_ready){if(!sam_trimmed)sam_trim_adjust(sam_focus,key==14?1:-1);}
                else sam_adjust(sam_focus,key==14?1:-1);
            }
            sam_redraw();
        }
        return 1;
    }
    return 0; /* PLAY/STOP and transport continue through the native app. */
}
static u32 sampler_key(u8 *iface,const u8 *ev)
{
    u32 ready=sam_state==SAM_READY && sam_frames,result;
    result=sampler_key_base(iface,ev);
    if(ready && sam_state==SAM_EMPTY && !sam_frames)recorder_resume();
    return result;
}
void sampler_draw(u8 *iface,void *ctx)
{
    u32 i,width,mark,frames=sam_frames,state=sam_state,stereo; s32 lo,hi;
    const char *source,*line1,*line2;char value[12],time[6];
    (void)iface;
    sam_report[2]++;
    if(state==SAM_READY && sam_wave_ready && frames) {sam_editor_draw(ctx);return;}
    /* The mode of the take on screen, or the one the next take will use. */
    stereo=frames?sam_take_stereo:sam_stereo;
    source=sam_source_name(sam_source,sam_stereo);
    WAVE_FILL(ctx,0,0,127,63,0);
    if(frames>sam_max_frames(stereo))frames=sam_max_frames(stereo);
    /* Top row, left to right: STEREO (encoder A) and POST FX (encoder B),
     * stock toggles with their labels underneath as on native pages; then
     * SRC, the meter (L above R in stereo) and MEM below it. */
    SAM_TOGGLE(0,sam_stereo?0x100u:0u,ctx,6,47,0);
    WAVE_TEXT(ctx,(void *)0x443da210u,16,45,2,"%s","STEREO");
    SAM_TOGGLE(0,sam_post?0x100u:0u,ctx,36,47,0);
    WAVE_TEXT(ctx,(void *)0x443da210u,46,45,2,"%s","POST FX");
    WAVE_TEXT(ctx,(void *)0x443da210u,64,58,0,"SRC: %s",source);
    WAVE_FILL(ctx,64,49,127,55,1);
    WAVE_FILL(ctx,65,50,126,54,0);
    if(sam_stereo) {
        width=sampler_meter_width(sam_peak);if(width)WAVE_FILL(ctx,65,53,64+(s32)width,54,1);
        width=sampler_meter_width(sam_peak_r);if(width)WAVE_FILL(ctx,65,50,64+(s32)width,51,1);
    } else {
        width=sampler_meter_width(sam_peak);
        if(width)WAVE_FILL(ctx,65,50,64+(s32)width,54,1);
    }
    mark=65+sam_thr*61u/127u;
    for(i=48;i<=56;i+=2)WAVE_PIXEL(ctx,(s32)mark,(s32)i,1);
    if(sam_peak>=0x7f000000u || sam_peak_r>=0x7f000000u)WAVE_FILL(ctx,126,48,127,56,1);
    sam_time_text(time,sam_pcm?sam_max_frames(stereo)-frames:0);
    WAVE_TEXT(ctx,(void *)0x443da210u,64,41,0,"MEM:%s",time);
    /* Original recorder frame: x0..127, y16..40. */
    WAVE_FILL(ctx,0,16,127,16,1);WAVE_FILL(ctx,0,40,127,40,1);
    WAVE_FILL(ctx,0,16,0,40,1);WAVE_FILL(ctx,127,16,127,40,1);
    if(sam_save_phase || sam_save_message[0]) {
        WAVE_TEXT(ctx,(void *)0x443da210u,64,32,2,"%s",sam_save_phase?"SAVING":sam_save_message);
        if(sam_save_phase){number3(value,sam_save_total?sam_save_pos*100u/sam_save_total:0);value[3]='%';value[4]=0;
            WAVE_TEXT(ctx,(void *)0x443da210u,64,23,2,"%s",value);}
        else WAVE_TEXT(ctx,(void *)0x443da210u,64,23,2,"%s","FUNC+YES: PLAY");
    } else if(state==SAM_READY && sam_wave_ready && frames) {
        for(i=0;i<126;i++) {
            lo=28+((s32)sam_wave_low[i]*10>>15);
            hi=28+((s32)sam_wave_high[i]*10>>15);
            WAVE_FILL(ctx,1+(s32)i,lo,1+(s32)i,hi,1);
        }
        if(sam_play_length) {
            i=1+sam_play_pos*125u/frames;
            WAVE_FILL(ctx,(s32)i,17,(s32)i,39,1);
        }
    } else {
    if(sam_hint) {line1="NO MEMORY";line2="NO: BACK";}
    else if(state==SAM_NORMALIZING) {line1="NORMALIZING";line2="PLEASE WAIT";}
    else if(state==SAM_RECORDING) {line1="RECORDING";line2="STOP: END";}
    else if(state==SAM_ARMED) {line1="ARMED";line2="WAITING FOR THR";}
    else if(frames) {line1=sam_play_length?"PLAYING":"FUNC+YES: PLAY";line2="FUNC+NO: CLEAR";}
    else {line1="FUNC+YES: REC";line2="YES: ARM";}
    WAVE_TEXT(ctx,(void *)0x443da210u,64,32,2,"%s",line1);
    WAVE_TEXT(ctx,(void *)0x443da210u,64,25,2,"%s",line2);
    if(frames) {
        sam_time_text(time,sam_play_length?sam_play_pos:frames);
        WAVE_TEXT(ctx,(void *)0x443da210u,64,18,2,"%s",time);
    }
    if(state==SAM_NORMALIZING)sam_norm_shown=1;
    }
    for(i=0;i<4;i++) {
        if(i==0) {
            if(sam_rlen==8)text_set(value,"MAX",11);
            else {number3(value,1u<<sam_rlen);value[3]=0;}
        } else if(i==1) {number3(value,sam_thr);value[3]=0;}
        else if(i==2)text_set(value,sam_stereo?source:sam_source==5?"MAIN+":sam_source==2?"IN L+R":source,11);
        else text_set(value,sam_source<3?(sam_mon?"YES":"NO"):"--",11);
        WAVE_TEXT(ctx,(void *)0x443da210u,16+32*i,7,2,"%s",value);
        WAVE_TEXT(ctx,(void *)0x443da210u,16+32*i,0,2,"%s",sam_labels[3+i]);
        if(sam_focus==i)WAVE_FILL(ctx,2+32*i,14,29+32*i,14,1);
    }
}
void sampler_open(u8 *manager)
{
    u8 *root,*view;u32 i;void *pair[2];
    if(sam_view || !manager)return;
    preview_stop();
    root=MALLOC(16+0x50);if(!heap_ok(root))return;
    *(u32 *)root=0x401c9670u;put32(root+4,1);put32(root+8,1);
    view=root+16;*(u8 **)(root+12)=view;
    /* A recorder is a plain custom IView, not a list or a ThreePaneList.
     * No list-model, row-label or inherited child-list callbacks exist here. */
    REC_VIEW_CTOR(view,"RECORDER");
    UI_OPAQUE(view,1);
    for(i=0;i<19;i++)sam_iface_table[i]=REC_VIEW_TABLE[i];
    sam_iface_table[2]=(u32)sampler_dispose;
    /* Complete-view ev_draw paints this view; keep its stock draw method. */
    *(u32 **)view=sam_iface_table+2;
    sam_view=view;sam_manager=manager;sam_peak=sam_peak_r=0;sam_hint=0;sam_last_source=18;
    sampler_suspend();sam_keys_down=0;sam_no_clear=0;sam_prepare();
    pair[0]=view;pair[1]=root;
    UI_PUSH(manager,pair,0);UI_DROP(pair+1);
    sam_enabled=sam_foreground();sam_report[0]++;sam_redraw();
}
static u8 *sam_top(void *app)
{
    u8 *manager=(u8 *)app+0x40,*node=*(u8 **)(manager+0x1c);
    return node && node!=manager+0x14?*(u8 **)(node+8):0;
}
/* Existing App key hook. SOUND is native key 5 (MainScreenView case 5).
 * Audio-frame clock measures the 350 ms window without a new timer hook.
 * First press retains the native action; only the second press/release is ours. */
u32 sampler_pre_event(void *app,const u8 *ev)
{
    u32 key=be32(ev+12),flags=be32(ev+16),now=sam_clock;
    if(key==12)sam_yes_down=flags&1; /* CHORUS_INTEGRATED: track popup waits for YES up */
    /* CHORUS_INTEGRATED: TrackSelect (stack tail) owns every key while it is shown. */
    if(sam_assign_phase==SAM_ASSIGN_SHOWN && !sam_tail_is_recorder())return 0;
    if(sam_name_view && sam_top(app)==sam_name_view && (key==12 || key==13)) {
        /* Native editor handles FUNC insert/delete itself; bypass PTN listeners
         * and manager shortcuts for both YES/NO edges, as in RECORDER. */
        if(!sam_name_view[0x30])REC_NAME_KEY(sam_name_view,ev);
        return 1;
    }
    if(key==5 && sam_foreground()) {
        if(!(flags&1)) {sam_sound_down=0;sam_sound_release=0;}
        return 1;
    }
    if(key==5 && !(flags&2)) {
        if(!(flags&1)) {
            sam_sound_down=0;
            if(sam_sound_release){
                sam_sound_release=0;
                /* Finish the native key-release callbacks before publishing a
                 * new view. The first SOUND keeps its original behaviour. */
                REC_KEY_MANAGER((u8 *)app+0x40,ev);
                /* Retire only the view opened by the first SOUND. Do not
                 * retain its list/label maintenance underneath RECORDER. */
                if(sam_sound_browser && sam_top(app)==sam_sound_browser) {
                    UI_CLOSE(sam_sound_browser);
                    REC_MANAGER_CLEANUP((u8 *)app+0x40);
                }
                sam_sound_browser=0;
                sampler_open((u8 *)app+0x40);
                return 1;
            }
            if(sam_sound_pending && sam_top(app)!=sam_sound_origin)
                sam_sound_browser=sam_top(app);
            return 0;
        }
        if(sam_sound_down || (flags&8))return sam_sound_release;
        sam_sound_down=1;
        if(sam_sound_pending && now-sam_sound_at<=16800u) {
            sam_sound_pending=0;sam_sound_release=1;
            return 1;
        }
        sam_sound_at=now;sam_sound_pending=1;
        sam_sound_origin=sam_top(app);sam_sound_browser=0;return 0;
    }
    if(flags&1){sam_sound_pending=0;sam_sound_browser=0;}
    if(sam_foreground() && key==11) {
        if(flags&1)sampler_suspend();
        return (flags&2)?1:0; /* plain STOP still stops native transport */
    }
    if(sam_foreground() && (sam_save_phase || sam_assign_phase || key==12 || key==13)) {
        /* The manager dispatches registered shortcut/release callbacks before
         * views. Consume both edges here, including a release without FUNC,
         * so SAVE/RELOAD PTN cannot run behind the opaque recorder. */
        sampler_key(sam_view,ev);
        return 1;
    }
    return 0;
}
