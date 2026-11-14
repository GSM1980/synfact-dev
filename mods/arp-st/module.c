/* SPDX-License-Identifier: GPL-2.0-or-later
 * Sound-owned monophonic ARP. Task-side metadata uses the shared lifecycle bus.
 * Twelve independent audio tracks; OFF at startup. Native events retain ownership.
 */
#include <syntakt/native.h>
#include <syntakt/host.h>
#include "runtime.h"
#include "native-st.h"
#include "keyboard.h"
#include "persist.h"
#include <syntakt/sound-meta.h>
#define DATA __attribute__((section(".data")))
#define ARP_COMMAND 0x41525031u
#define ARP_CONFIG 0x43464731u
#define ARP_STOP 0x53544f50u
static const struct st_machine_host_api *arp_host DATA=0;
static struct arp_runtime arp_voice[ARP_TRACKS] DATA={{0}};
#define ARP_DEFAULT {0,9,1,30,15,0,0xffff,{0}}
static struct arp_settings arp_parameters[ARP_TRACKS] DATA __attribute__((aligned(4)))={ARP_DEFAULT,ARP_DEFAULT,ARP_DEFAULT,ARP_DEFAULT,
    ARP_DEFAULT,ARP_DEFAULT,ARP_DEFAULT,ARP_DEFAULT,ARP_DEFAULT,ARP_DEFAULT,ARP_DEFAULT,ARP_DEFAULT};
static arp_u32 arp_track DATA=0,arp_running DATA=0;
static arp_u32 arp_generation[ARP_TRACKS] DATA={1,1,1,1,1,1,1,1,1,1,1,1},arp_sequence[ARP_TRACKS] DATA={0},arp_kit[ARP_TRACKS] DATA={0};
static unsigned int arp_live[ARP_TRACKS] DATA={0};
static arp_u32 arp_bound_sound[ARP_TRACKS] DATA={0};
static unsigned char arp_lab_override[ARP_TRACKS] DATA={0};
#include "locks.h"
#include "chord.h"
#include "record.c"
/* A sequence root plays its step's NOT2..NOT4 too. Stock TRIG may return a
 * different event from the one it was given; NOTE_ACCEPT sees the returned
 * event. Keep each step's intervals under that result, four per track. The
 * first step after STOP uses a static event which native start may copy, so
 * retain a separate prepared record as well. */
struct arp_chord_record {arp_u32 event;int interval[3];unsigned int count;};
static volatile struct arp_chord_record arp_chord_ring[ARP_TRACKS][4] DATA={{{0}}};
static volatile struct arp_chord_record arp_chord_prep[ARP_TRACKS] DATA={{0}};
static unsigned char arp_chord_next[ARP_TRACKS] DATA={0};
#define ARP_PREP_EVENT(t) (0x444e2438u+(t)*0x50u)
static void chord_trig(const arp_u32 *a,arp_u32 event)
{
    arp_u32 track=a[0];unsigned int i,count;int interval[3];
    volatile struct arp_chord_record *r=0;
    if(track>=ARP_TRACKS || !a[1])return;
    if(!event){
        if(a[4]==ARP_PREP_EVENT(track))arp_chord_prep[track].event=0;
        return;
    }
    count=st_chord_intervals((const volatile unsigned char *)a[1]+track*ST_CHORD_TRACK_BYTES,a[3],interval);
    if(a[4]==ARP_PREP_EVENT(track)){
        r=&arp_chord_prep[track];r->event=0;
        for(i=0;i<count;i++)r->interval[i]=interval[i];
        r->count=count;r->event=event;
    }
    if(event!=ARP_PREP_EVENT(track)){
        r=0;
        for(i=0;i<4;i++)if(arp_chord_ring[track][i].event==event)r=&arp_chord_ring[track][i];
        if(!r){r=&arp_chord_ring[track][arp_chord_next[track]&3u];arp_chord_next[track]++;}
        r->event=0;
        for(i=0;i<count;i++)r->interval[i]=interval[i];
        r->count=count;r->event=event;
    }
}
static unsigned int chord_take(arp_u32 track,const arp_u32 *e,int *interval)
{
    volatile struct arp_chord_record *r=0;unsigned int i,count;
    const volatile unsigned char *sequence=*(const volatile unsigned char *const volatile *)0x43bd7430u;
    for(i=0;i<4;i++)if(arp_chord_ring[track][i].event==(arp_u32)e)r=&arp_chord_ring[track][i];
    if(!r && arp_chord_prep[track].event)r=&arp_chord_prep[track];
    if(!r)return sequence?st_chord_intervals(sequence+track*ST_CHORD_TRACK_BYTES,ST_CHORD_STEPS,interval):0;
    count=r->count;
    for(i=0;i<count;i++)interval[i]=r->interval[i];
    if(r!=&arp_chord_prep[track] && arp_chord_prep[track].event==(arp_u32)e)
        arp_chord_prep[track].event=0;
    r->event=0;return count;
}
static const volatile arp_u32 *held_notes(unsigned int track)
{return (const volatile arp_u32 *)(0x4553f6e0u+track*0x2e18u);}
static int arp_native(arp_u32,const arp_u32 *,arp_u32 *);
static void arp_trig_sync(void *);
static int arp_trig_layout(const arp_u32 *,arp_u32 *);
static int queue(arp_u32 *e,arp_u32 time)
{
    const volatile arp_u32 *b=*(const volatile arp_u32 **)0x444de434u;
    if(!*(const volatile arp_u32 *)0x444de43cu){
        /* Native allocation spins on exhaustion: never call it without a
         * free bucket or an existing non-immediate bucket at this timestamp. */
        while(b && (b[0] || b[1]!=time))b=(const volatile arp_u32 *)b[4];
        if(!b)return 0;
    }
    ARP_ENQUEUE(e,time);return 1;
}
static arp_u32 *allocate(void){return ARP_EVENT_ALLOC();}
static void copy(arp_u32 *d,const arp_u32 *s){ARP_EVENT_COPY(d,s);}
static void release(arp_u32 *e){ARP_EVENT_FREE(e);}
static const struct arp_runtime_api arp_native_api={allocate,copy,release,queue};
static int eligible(arp_u32 track)
{
    arp_u32 kit=*(const volatile arp_u32 *)0x43bd743cu;
    const unsigned char *owner;unsigned int i;
    if(track>=ARP_TRACKS || !kit || (*(const volatile unsigned short *)(kit+0x1550u)&(1u<<track)))return 0;
    owner=(const unsigned char *)arp_host->native_dispatch(ST_NATIVE_OWNERS,0);
    if(owner){if(owner[track]!=track)return 0;for(i=track+1;i<ARP_TRACKS;i++)if(owner[i]==track)return 0;}
    return 1;
}
static void stop_quiet(unsigned int track)
{
    arp_live[track]=0;arp_running&=~(1u<<track);
    arp_runtime_stop(&arp_voice[track]);if(!++arp_generation[track])arp_generation[track]=1;
}
static void stop_locked(unsigned int track)
{
    if(arp_voice[track].source)ARP_RELEASE(track,0x7fffu);
    stop_quiet(track);
}
static void interest(void)
{
    /* A newly loaded Sound can enable ARP before any page has been opened. */
    arp_u32 mask=(1u<<ST_NATIVE_IMMEDIATE)|(1u<<ST_NATIVE_HEADER_LOAD)|(1u<<ST_NATIVE_HEADER_RESET)|
        (1u<<ST_NATIVE_SOUND_META)|(1u<<ST_NATIVE_NOTE_ACCEPT)|(1u<<ST_NATIVE_TRIG)|
        (1u<<ST_NATIVE_PAGE_SYNC)|(1u<<ST_NATIVE_PAGE_LAYOUT);
    arp_host->native_interest((void (*)(void))arp_native,mask);
}
/* UI/test entry. Publication uses the existing native exclusion and private
 * command pattern already used by POLY. No native object is reinterpreted. */
int arp_experiment_configure(arp_u32 track,const struct arp_settings *p)
{
    arp_u32 command[6]={ARP_COMMAND,ARP_CONFIG,track,(arp_u32)p,0,0};
    ARP_IMMEDIATE(command);return (int)command[5];
}
static int arp_sound_configure(arp_u32 track,const struct arp_settings *p,arp_u32 sound)
{
    arp_u32 command[6]={ARP_COMMAND,ARP_CONFIG,track,(arp_u32)p,sound,0};
    ARP_IMMEDIATE(command);return (int)command[5];
}
typedef arp_u32 arp_settings_word __attribute__((may_alias));
static void restore_sound(unsigned int track,arp_u32 sound)
{
    const struct arp_settings *p;const arp_settings_word *src,*dst;int different;
    if(arp_lab_override[track])return;
    p=arp_sound_settings(sound);
    /* Both persistent banks/defaults and this published array are aligned.
     * Compare the complete 24-byte settings with six words, no byte loop. */
    src=(const arp_settings_word *)p;dst=(const arp_settings_word *)&arp_parameters[track];
    different=src[0]!=dst[0] || src[1]!=dst[1] || src[2]!=dst[2] ||
              src[3]!=dst[3] || src[4]!=dst[4] || src[5]!=dst[5];
    if(arp_bound_sound[track]!=sound || (different && (p->mode!=arp_parameters[track].mode || p->speed!=arp_parameters[track].speed)))stop_locked(track);
    if(different)arp_parameters[track]=*p;
    arp_bound_sound[track]=sound;
}
/* Task-side object changes must also reach a held root: loading an OFF Sound
 * or resetting/freeing its model stops ARP even before another note arrives.
 * Parameters publish through IMMEDIATE; task code never mutates the runtime. */
static void changed_sound(const arp_u32 *a)
{
    arp_u32 bytes=0,sound;unsigned int t,i;struct arp_settings p;
    if(a[0]==ST_SOUND_DECODE)bytes=0x123;
    else if(a[0]==ST_SOUND_KIT_DECODE)bytes=0x1618;
    else if(a[0]==ST_SOUND_FREE)bytes=a[3];
    else if((a[0]==ST_SOUND_COPY || a[0]==ST_SOUND_NEW) && (a[3]==0x123 || a[3]==0x1618))bytes=a[3];
    if(!bytes)return;
    for(t=0;t<ARP_TRACKS;t++){
        sound=arp_bound_sound[t];if(!sound || arp_lab_override[t] || sound-a[1]>=bytes)continue;
        arp_sound_read(sound,&p);
        for(i=0;i<sizeof(p);i++)if(((const unsigned char *)&p)[i]!=((const volatile unsigned char *)&arp_parameters[t])[i])break;
        if(i<sizeof(p))arp_sound_configure(t,&p,sound);
    }
}
static void stop_command(void)
{arp_u32 c[6]={ARP_COMMAND,ARP_STOP,0,0,0,0};ARP_IMMEDIATE(c);}
/* pending is nonzero inside the running-track loop. ISA_C bit reversal puts
 * its lowest set bit first, so FF1 returns the track directly. */
static __inline__ unsigned int arp_first_track(arp_u32 pending)
{
#ifdef __mcoldfire__
    __asm__("bitrev %0\n\tff1 %0":"+d"(pending)::"cc");
    return pending;
#else
    return 31-__builtin_clz(pending&-pending);
#endif
}
static void arp_frame(void)
{
    arp_u32 begin,tempo,sequence,kit,pending=arp_running;unsigned int track;
    if(!pending)return;
    sequence=*(const volatile arp_u32 *)0x43bd7430u;kit=*(const volatile arp_u32 *)0x43bd743cu;
    begin=*(const volatile arp_u32 *)0x800046ecu;tempo=*(const volatile arp_u32 *)0x402d7010u;
    while(pending){
        struct arp_runtime *r;
        track=arp_first_track(pending);pending&=pending-1;
        r=&arp_voice[track];
        if(sequence!=arp_sequence[track] || kit!=arp_kit[track] || tempo<3600 || tempo>36000 ||
           (!arp_live[track] && *(const volatile arp_u32 *)0x43bd73bcu!=1)) {stop_locked(track);continue;}
        if(arp_live[track] && arp_keyboard_sync(&r->selector,held_notes(track))<=0){stop_locked(track);continue;}
        if((int)(r->clock.next-(begin+2*tempo))<0 && !eligible(track)){stop_locked(track);continue;}
        {
            struct arp_settings scratch;arp_u32 period;
            const struct arp_settings *effective=arp_effective_read(track,&scratch);
            /* The running generation is nonzero (start/stop enforce it).
             * Only the desired period is needed; do not arm a scratch clock. */
            period=arp_clock_period(effective->speed);
            if(r->clock.period!=period){
                /* A new rate keeps the phase of the last emitted beat. */
                struct arp_clock *c=&r->clock;
                c->next=c->next-c->period+period;c->period=period;
                if((int)(c->next-begin)<0)c->next=begin;
            }
            arp_runtime_frame(r,effective,begin,begin+2*tempo,arp_generation[track]);
        }
        if(!r->source)arp_running&=~(1u<<track);
    }
}
static int arp_native(arp_u32 op,const arp_u32 *a,arp_u32 *result)
{
    arp_u32 *e,before;const struct arp_settings *p;unsigned int i,track;
    if(op==ST_NATIVE_SOUND_META){arp_sound_meta(a);changed_sound(a);return 0;}
    /* UI task: ARP's two TRIG pages (list item 37); a notification. */
    if(op==ST_NATIVE_PAGE_SYNC){if(a && a[0])arp_trig_sync((void *)a[0]);return 0;}
    if(op==ST_NATIVE_PAGE_LAYOUT)return arp_trig_layout(a,result);
    if(op==ST_NATIVE_TRIG){
        /* Only independent audio tracks reach the stock replay here. POLY
         * owns grouped tracks and must prepare their voices itself. */
        if(!a || a[0]>=ARP_TRACKS || !eligible(a[0]))return 0;
        *result=arp_host->native_original(ST_NATIVE_TRIG,a);
        chord_trig(a,*result);
        return 1;
    }
    if(op==ST_NATIVE_IMMEDIATE){
        e=(arp_u32 *)a[0];if(e[0]!=ARP_COMMAND){
            int held,fresh;
            track=e[2];
            if(track>=ARP_TRACKS || e[0]>1 || e[3]!=2 ||
               (e[1]!=1 && e[1]!=2) || !eligible(track))return 0;
            fresh=e[1]==1 && e[6]<128 &&
                (!arp_live[track] || !(arp_voice[track].selector.mask[e[6]>>5]&(1u<<(e[6]&31))));
            restore_sound(track,e[11]?e[11]:e[10]?e[10]:*(const volatile arp_u32 *)0x43bd743cu+0x2a+track*0x123u);
            if(!arp_parameters[track].mode)return 0;
            if(arp_live[track]) {
                held=arp_keyboard_sync(&arp_voice[track].selector,held_notes(track));
                if(held<0 || (e[1]==1 &&
                   ((e[11] && e[11]!=*(const volatile arp_u32 *)0x444842d0u) ||
                    (e[9]&0x30201u)!=0x10201u))) {stop_locked(track);return 0;}
                if(!held) {
                    stop_locked(track);
                    if(held<0 || e[1]==1)return 0;
                }
            }else {
                /* Check the whole set, before retaining or consuming anything.
                 * Native priority may enqueue only the most recent key. */
                struct arp_selector keys;arp_reset(&keys,arp_generation[track]);
                held=arp_keyboard_sync(&keys,held_notes(track));
                if(held<=0 || e[1]!=1 || (e[11] && e[11]!=*(const volatile arp_u32 *)0x444842d0u))return 0;
                if(!arp_runtime_live_start(&arp_voice[track],&arp_parameters[track],e,
                    *(const volatile arp_u32 *)0x800046ecu,arp_generation[track])) {
                    if(arp_voice[track].source)stop_locked(track);return 0;
                }
                for(i=0;i<sizeof(keys);i++)
                    ((volatile unsigned char *)&arp_voice[track].selector)[i]=((const volatile unsigned char *)&keys)[i];
                arp_live[track]=1;arp_running|=1u<<track;arp_root_lock[track].mask=0;
                arp_sequence[track]=*(const volatile arp_u32 *)0x43bd7430u;arp_kit[track]=*(const volatile arp_u32 *)0x43bd743cu;
            }
            if(fresh)arp_rec_note(track,e[6]);
            /* IMMEDIATE owns a pooled input. As for POLY's consuming handler,
             * release it exactly once; outputs use ENQUEUE, avoiding recursion. */
            if(!e[0])ARP_EVENT_FREE(e);
            *result=1;return 1;
        }
        if(e[1]==ARP_STOP){for(i=0;i<ARP_TRACKS;i++)if(arp_voice[i].source)stop_locked(i);*result=1;return 1;}
        if(e[1]!=ARP_CONFIG){*result=1;return 1;}
        p=(const struct arp_settings *)e[3];track=e[2];e[5]=0;
        if(p && p->mode<=4 && p->speed<18 && p->range<=ARP_RANGE_MAX && p->scale<=1 && p->note_length<128 && p->length<16 && e[2]<ARP_TRACKS &&
           (!p->mode || eligible(e[2]))){
            if(e[4]){
                const struct arp_settings *current=arp_sound_settings(e[4]);
                for(i=0;i<sizeof(*p);i++)if(((const unsigned char *)current)[i]!=((const unsigned char *)p)[i])break;
                if(i<sizeof(*p)){*result=1;return 1;} /* task snapshot was superseded */
                if(arp_voice[track].source && arp_bound_sound[track]!=e[4]){
                    e[5]=1;*result=1;return 1; /* another Sound owns this root */
                }
            }
            /* Preserve the root when changing mode or rate. Retiming uses the
             * last emitted beat as the phase origin and never queues in the past. */
            if(!p->mode || (arp_voice[track].source &&
               (arp_sequence[track]!=*(const volatile arp_u32 *)0x43bd7430u ||
                arp_kit[track]!=*(const volatile arp_u32 *)0x43bd743cu ||
                (!arp_live[track] && *(const volatile arp_u32 *)0x43bd73bcu!=1))))stop_locked(track);
            else if(arp_voice[track].source && p->speed!=arp_parameters[track].speed){
                struct arp_clock *c=&arp_voice[track].clock;
                arp_u32 last=c->next-c->period,now=*(const volatile arp_u32 *)0x800046ecu;
                arp_clock_arm(c,last,p->speed,arp_generation[track]);c->next+=c->period;
                if((int)(c->next-now)<0)c->next=now;
            }
            for(i=0;i<sizeof(*p);i++)
                ((volatile unsigned char *)&arp_parameters[track])[i]=((const volatile unsigned char *)p)[i];
            arp_lab_override[track]=!e[4];arp_bound_sound[track]=e[4];
            arp_sequence[track]=*(const volatile arp_u32 *)0x43bd7430u;arp_kit[track]=*(const volatile arp_u32 *)0x43bd743cu;
            interest();
            e[5]=1;
        }
        *result=1;return 1;
    }
    if(op==ST_NATIVE_HEADER_LOAD || op==ST_NATIVE_HEADER_RESET){
        if(arp_running)stop_command();return 0;
    }
    if(op!=ST_NATIVE_NOTE_ACCEPT)return 0;
    e=(arp_u32 *)a[0];track=e[2];
    if(track>=ARP_TRACKS || e[3]!=1)return 0;
    /* Our own output: play it, or drop one a newer root has superseded. */
    switch(arp_runtime_owns(&arp_voice[track],e)){case 1:return 0;case 2:*result=1;return 1;}
    restore_sound(track,a[1]);
    /* The step's locks precede native effect application at NOTE_ACCEPT. */
    struct arp_settings effective;
    /* A trigless lock trig passing a running sequence arp adds its locks to
     * it, as a lock trig changes the sounding note; the native applies the
     * rest. It neither restarts nor stops the arp. */
    if(e[1]==1 && (e[9]&0x10201u)!=0x10201u){
        if(arp_voice[track].source && !arp_live[track] && e[17])arp_root_capture(track,e[17],1);
        return 0;
    }
    arp_root_capture(track,e[17],0);
    p=arp_effective_read(track,&effective);
    if(!p->mode)return 0;
    if(arp_live[track]){*result=1;return 1;}
    /* Stopped/held-step preview is not a running sequence root. Held keyboard
     * input has its own IMMEDIATE lifecycle above. */
    if(*(const volatile arp_u32 *)0x43bd73bcu!=1){
        if(arp_voice[track].source)stop_locked(track);return 0;
    }
    if(e[1]!=1 || !eligible(track)){
        stop_locked(track);return 0;
    }
    /* A new root replaces the running one: its first output retriggers the
     * track. The native gate release (400a5708) is applied by the audio
     * engine after that output and silenced the new root (LEN = the loop). */
    if(arp_voice[track].source)stop_quiet(track);
    {
        int chord[3];unsigned int notes=chord_take(track,e,chord);
        i=arp_runtime_start(&arp_voice[track],p,e,a[2],arp_generation[track],notes?chord:0,notes);
    }
    if(!i)return 0;
    arp_sequence[track]=*(const volatile arp_u32 *)0x43bd7430u;arp_kit[track]=*(const volatile arp_u32 *)0x43bd743cu;
    arp_running|=1u<<track;before=arp_voice[track].dropped;
    arp_runtime_frame(&arp_voice[track],p,a[2],a[3],arp_generation[track]);
    /* Pool pressure on the very first output retains the original note. */
    if(arp_voice[track].dropped!=before){stop_locked(track);return 0;}
    *result=1;return 1;
}
#include "view.c"
static int arp_initialize(const struct st_smz_api *a)
{
    unsigned int i;
    if(!a || a->version<3 || !a->ui || !a->ui->host || a->ui->host->version<9 ||
       !a->ui->host->native_dispatch || !a->ui->host->storage ||
       !a->ui->host->machine_telemetry || a->ui->host->version<10)return 1;
    arp_host=a->ui->host;arp_persist_init();for(i=0;i<ARP_TRACKS;i++)arp_runtime_init(&arp_voice[i],&arp_native_api);interest();
    /* PAGE_SYNC for the TRIG view only, as POLY declares (host API 6). */
    arp_host->native_page_view((void (*)(void))arp_native,ST_TRIG_VIEW);return 0;
}
static const struct st_smz_subscription subscriptions[]={
    {ST_EV_KEY,(void (*)(void))arp_view_key},
    {ST_EV_ENC,(void (*)(void))arp_view_encoder},
    {ST_EV_DRAW|ST_EV_VIEW,(void (*)(void))arp_view_draw},
    {ST_EV_TICK,(void (*)(void))arp_view_tick},
    {ST_EV_RENDER_IN,(void (*)(void))arp_frame},
    {ST_EV_NATIVE,(void (*)(void))arp_native}};
const struct st_smz_module arp_module={"ARP EXPERIMENT",arp_initialize,subscriptions,6};
