/*  common state. File-owned heap storage, supplied by the existing plugin
 * image allocation. No fixed SRAM reservation or owning Sound copies. */
#define DATA __attribute__((section(".data")))
static u32 poly_unison_spread(unsigned int);
#include "poly_event.c"
#include "poly_groups.c"
#include "poly_pattern.c"
static u8 poly_modes[POLY_MODE_TRACKS],poly_next_modes[POLY_MODE_TRACKS];
static u8 poly_unison[POLY_MODE_TRACKS] DATA={0},poly_next_unison[POLY_MODE_TRACKS] DATA={0};
static u32 poly_unison_spread(u32 track){return track<POLY_MODE_TRACKS?poly_unison[track]:0;}
static void poly_publish_voice_groups(void);
static void poly_play_maps(const volatile u8 *);
static void poly_copy_group(u32,u32);
static u32 poly_seq_voices(u32 **,u32,u32,u32,const u32 *,u32 *);
static void poly_seq_committed(u32,u32,u32,u32);
static void poly_seq_started(u32,u32);
static void poly_live_reset(const u8 *);
#include "poly_sequence.c"
#include "poly_publication.c"
#include "poly_play.c"
#include "poly_copy.c"
#include "poly_type.c"
/* Generic core parameter transport owns all machine frames. */

volatile struct config poly_seq_config DATA={1,0,{0,0,0,0,0,0,0,0,0,0,0,0},
    {0,1,2,3,4,5,6,7,8,9,10,11},{{0,0,0,0}}};
#ifndef POLY_HOST_TEST
__asm__(".globl poly_owner_map\n.set poly_owner_map,poly_seq_config+20");
#else
__asm__(".globl _poly_owner_map\n.set _poly_owner_map,_poly_seq_config+20");
#endif
static u8 record_cache[7] DATA={0};
static u32 cache_sequence DATA=0;
static u32 cache_midi_mask DATA=0xffffffffu;
#ifdef __mcoldfire__
/* Private aliases let the assembly reader use the existing cache, whose only
 * writers remain the locked C publication path. No new cached state. */
__asm__(".globl poly_sync_sequence_cache\n.set poly_sync_sequence_cache,cache_sequence\n"
        ".globl poly_sync_midi_cache\n.set poly_sync_midi_cache,cache_midi_mask\n"
        ".globl poly_sync_record_cache\n.set poly_sync_record_cache,record_cache");
#endif
static u32 preview_notes[12][4] DATA={{0}};
static u32 preview_generation[12] DATA={0};
static u32 preview_depth DATA=0;

u32 poly_effective_owner(u32 track) {return track<12?poly_owner_map[track]:track;}

static u32 poly_has_unison_api(void){return poly_host && poly_host->version>=22;}
static void poly_publish_voice_groups(void)
{
    int pitch[12]={0};
    u32 t,v,voices,spread,active=0;
    if(!poly_has_unison_api())return;
    for(t=0;t<POLY_MODE_TRACKS;t++)if(poly_seq_config.states[t]>=3 && (spread=poly_unison[t])) {
        voices=poly_seq_config.states[t]-1;
        for(v=0;v<voices;v++)
            pitch[t+v]=((int)(2*v)-(int)(voices-1))*(int)(spread*65536u)/(int)(200*(voices-1));
        active=1;
    }
    poly_host->voice_groups_set(poly_relation_tracks&0xffffu,active?pitch:0);
}

/* Called at the early MAIN frame boundary and before sequencer construction.
 * UI edits are committed under the same native interrupt exclusion. */
static u32 poly_sync_locked(void)
{
    u32 i,changed=0,midi_bit;
    u32 kit=*(volatile u32 *)0x43bd843cu,midi_mask,j,width;
    u8 states[12];
    const volatile u8 *sequence=*(const volatile u8 **)0x43bd8430u;
    const volatile u8 *record;
    if(!sequence)return 0;
    midi_mask=kit?*(volatile u16 *)(kit+0x1550u):0xffffu;
    record=sequence+0x21381u;
    if((u32)sequence!=cache_sequence)changed=1;
    if(midi_mask!=cache_midi_mask)changed=1;
    for(i=0;i<7;i++)if(record[i]!=record_cache[i])changed=1;
    if(!changed)return 1;
    if(!poly_decode_settings(record,states,poly_next_modes,poly_next_unison)) {
        for(i=0;i<12;i++)states[i]=0;
        for(i=0;i<POLY_MODE_TRACKS;i++)poly_next_modes[i]=poly_next_unison[i]=0;
    }
    if(!poly_has_unison_api())for(i=0;i<POLY_MODE_TRACKS;i++)poly_next_unison[i]=0;
    /* A saved group can be loaded with a KIT whose member is now MIDI.
     * Suspend only that group; retain its recorded topology for audio mode. */
    for(i=0;i<12;i++)if(states[i]>=3u) {
        width=states[i]-1u;
        for(j=i;j<i+width;j++) {
            midi_bit=(midi_mask>>j)&1u;
            if(midi_bit){states[i]-=3u;break;}
        }
        i+=width-1u;
    }
    if(!poly_commit((u32)sequence,states))return 0;
    for(i=0;i<7;i++)record_cache[i]=record[i];
    cache_sequence=(u32)sequence;
    cache_midi_mask=midi_mask;
    return 1;
}

static u32 poly_record_settings_locked(u32 track,u32 enabled,u32 voices,u32 mode,u32 spread)
{
    u8 states[12],owners[12],raw[POLY_RECORD_SIZE],modes[POLY_MODE_TRACKS],values[POLY_MODE_TRACKS];
    u32 i,midi_bit;
    volatile u8 *sequence=*(volatile u8 **)0x43bd8430u;
    u32 kit=*(volatile u32 *)0x43bd843cu;
    if(!sequence || !kit || voices<2 || voices>poly_max_voices(track))return 0;
    poly_sync_locked();
    if(poly_seq_config.owners[track]!=track)return 0;
    /* Keep suspended groups in the persistent record when another master is
     * edited. Runtime states can differ only because of the current MIDI KIT. */
    if(!poly_decode_settings(sequence+0x21381u,states,modes,values)) {
        for(i=0;i<12;i++)states[i]=0;
        for(i=0;i<POLY_MODE_TRACKS;i++)modes[i]=values[i]=0;
    }
    if(mode<2)modes[track]=(u8)mode;
    if(spread<=POLY_UNISON_MAX)values[track]=(u8)spread;
    states[track]=(u8)(voices-2u+(enabled?3u:0u));
    if(!poly_resolve(states,owners))return 0;
    /* Do not silently convert existing MIDI tracks into digital voices. */
    if(enabled)for(i=track;i<track+voices;i++) {
        midi_bit=(*(volatile u16 *)(kit+0x1550u)>>i)&1u;
        if(midi_bit)return 0;
    }
    poly_encode_settings(raw,states,modes,values);
    for(i=0;i<POLY_RECORD_SIZE;i++)sequence[0x21381u+i]=raw[i];
    return poly_sync_locked();
}

static u32 poly_record_write_locked(u32 track,u32 enabled,u32 voices)
{return poly_record_settings_locked(track,enabled,voices,4,~0u);}

static u32 poly_mode_write(u32 track,u32 mode)
{
    u32 command[6]={0x504f4c59u,0x43464732u,track,mode,0,0};
    native_immediate(command);return command[5];
}

static u32 poly_spread_write(u32 track,u32 spread)
{
    u32 command[6]={0x504f4c59u,0x554e4953u,track,spread,0,0};
    native_immediate(command);return command[5];
}

/* Only UI reads machine IDs: never scan them from the audio fast path. */
static void poly_analog_publish_locked(void)
{
    u32 t,i,kit=*(volatile u32 *)0x43bd843cu;
    if(!kit)return;
    for(t=8;t<10;t++)if(poly_seq_config.states[t]>=3) {
        u8 type=*(volatile u8 *)(0x80002f50u+t);
        if(type==poly_analog_type[t-8])continue;
        for(i=t+1;i<t+poly_seq_config.states[t]-1u;i++)
            native_publish_sound((const volatile u8 *)(kit+0x2au+t*0x123u),i);
        poly_analog_type[t-8]=type;
    }
}

static void poly_analog_ui(void)
{
    u32 t;
    if(!poly_analog_active)return;
    for(t=8;t<10;t++)if(poly_seq_config.states[t]>=3 &&
        *(volatile u8 *)(0x80002f50u+t)!=poly_analog_type[t-8]) {
        u32 command[6]={0x504f4c59u,0x414e4c47u,0,0,0,0};
        native_immediate(command);break;
    }
}

/* The existing native immediate queue entry supplies its SR save/mask/restore
 * around a private stack command. The hook consumes the command before the
 * stock queue sees any fields; no event/lock/bucket is allocated or enqueued.
 * This keeps multi-byte pattern writes and config publication atomic without
 * injecting new privileged instructions or changing native lock routines. */
#ifndef __mcoldfire__
u32 poly_sync(void)
{
    u32 command[6],kit;const u8 *record;
    const u8 *sequence=*(const u8 *const volatile *)0x43bd8430u;
    /* : called on every published block (1500/s), Sound write and draw.
     * Read-only fast path: when the sequence, MIDI mask and topology record
     * still equal the last committed ones and no recording fixup is pending,
     * the locked pass would change nothing. Any difference takes it. */
    if(sequence && (u32)sequence==cache_sequence) {
        kit=*(volatile u32 *)0x43bd843cu;
        if((kit?*(volatile u16 *)(kit+0x1550u):0xffffu)==cache_midi_mask) {
            /* Odd-addressed seven-byte record: byte comparisons preserve its
             * alignment contract and avoid the loop/index bookkeeping. */
            record=sequence+0x21381u;
            if(record[0]==record_cache[0] && record[1]==record_cache[1] &&
               record[2]==record_cache[2] && record[3]==record_cache[3] &&
               record[4]==record_cache[4] && record[5]==record_cache[5] &&
               record[6]==record_cache[6])return 1;
        }
    }
    command[0]=0x504f4c59u;command[1]=0x53594e43u;
    command[2]=0;command[3]=0;command[4]=0;
    command[5]=0;native_immediate(command);return command[5];
}
#endif

u32 poly_record_write(u32 track,u32 enabled,u32 voices)
{
    u32 command[6];
    command[0]=0x504f4c59u;command[1]=0x43464731u;
    command[2]=track;command[3]=enabled;command[4]=voices;
    command[5]=0;native_immediate(command);return command[5];
}

void poly_preview(u32 *event)
{
    u32 track=event[2],step,count=0,i;
    u32 *batch[4];
    if(track>=12 || event[3]!=1 || event[11]){native_immediate(event);return;}
    poly_sync();
    if(poly_seq_config.owners[track]<track) {
        if(!event[0])native_event_free(event);
        return;
    }
    /* CHORUS_CODEC: native long (401055be/cmp.l), -1 = none; the .. byte read
     * took its high byte, i.e. step 0 for any held step. */
    step=*(volatile u32 *)0x402d5aacu;
    if(event[1]==1 && poly_seq_config.states[track]>=3 && step<64) {
        count=poly_prepare(event,(const volatile u8 *)poly_seq_config.sequence,step,
                           poly_seq_config.states[track]-1u,batch);
        if(count && poly_submit(batch,count,0,1)) {
            for(i=0;i<4;i++)preview_notes[track][i]=128;
            for(i=0;i<count;i++)preview_notes[track][batch[i][2]-track]=batch[i][6];
            preview_generation[track]=poly_seq_config.generation;
            if(!event[0])native_event_free(event);
            return;
        }
        while(count)native_event_free(batch[--count]);
    }else if(event[1]!=1 && preview_generation[track]==poly_seq_config.generation) {
        for(i=0;i<4;i++)if(preview_notes[track][i]<128) {
            batch[count]=copy_event(event);
            if(!batch[count])break;
            poly_voice_target(batch[count],track,i);
            batch[count][6]=preview_notes[track][i];count++;
        }
        if(i==4 && count && poly_submit(batch,count,0,1)) {
            preview_generation[track]=0;
            if(!event[0])native_event_free(event);
            return;
        }
        while(count)native_event_free(batch[--count]);
    }
    native_immediate(event);
}

/* Live notes (internal keyboard, TRIG keys, step preview, MIDI note input)
 * reach the native immediate queue from 40119684 with kind 2; +4 is 1 for on
 * and 2 for off, after that function's per-track note priority.
 *
 * : the origin decides the POLY behavior. Keys of the KEYBOARD mode and
 * MIDI note input play one voice per key; everything else (a track's TRIG
 * key, a held step's preview) plays the chord. The origin is the native
 * caller still on the stack: KEYBOARD press 4002e850 (returns to 4002ed0a)
 * and its release callback 4002d5f2 (4002d602); MIDI press returns to
 * 401055cc and release to 40105488. The CC64 pedal-up loop calls the same
 * note-off producer and returns to 4010a096. Only live frames are read. */
#ifdef __mcoldfire__
extern u32 poly_scan_key_origin(const volatile u16 *);
#endif
static u32 poly_live_from_keys(void)
{
#ifdef POLY_HOST_TEST
    return poly_test_origin;
#elif defined(__mcoldfire__)
    /* Same parent-frame cursor and 1024 halfword positions as the C census.
     * The leaf scanner never changes the parent frame it is inspecting. */
    u32 sp;
    __asm__ volatile("move.l %%sp,%0":"=d"(sp));
    return poly_scan_key_origin((const volatile u16 *)sp);
#else
    const volatile u16 *stack;
    u32 i,word,sp;
    __asm__ volatile("move.l %%sp,%0":"=d"(sp));
    stack=(const volatile u16 *)sp;
    for(i=0;i<1024;i++) {
        word=((u32)stack[i]<<16)|stack[i+1];
        /* 2 = a key press (recorded by the native recorder); 1 = a release,
         * whose native last-note re-send is not a new key. */
        if(word==0x4002ed0au || word==0x401055ccu)return 2;
        if(word==0x4002d602u || word==0x40105488u || word==0x4010a096u)return 1;
    }
    return 0;
#endif
}

/*  live recording of played chords. The native live recorder 4001ecb6
 * runs in the UI loop from the note message of 40104c02; on an audio track it
 * keeps one note per step (NOTE = played note, NOT2..NOT4 = FFh = inherit the
 * track defaults) in the UI pattern model. The sequencer reads its own copy
 * (43bd7430), refreshed from that model, so the  fixup of that copy alone
 * was lost. : a chord is the set of keys held together that fall on the
 * same recorder step. Each key press while live recording (same test as
 * 400087d6 -> 400c2f6a) notes the step the recorder will use (4008da6e on the
 * same clock, clamp, 4002a3b6 remap). In the UI loop (draw dispatch), once the
 * recorder has written that step for the newest note (NOTE = note, NOT2..NOT4
 * = FFh), the step gets the played chord as its own trig values, in the model
 * and in the sequencer copy: NOTE = first key, NOT2..NOT4 = the other keys as
 * intervals, unused ones OFF (64). Existing step values and track defaults
 * are not used. */
struct rec_chord {u32 sequence,age;u8 dirty,step,count,notes[4];};
static struct rec_chord rec_chords[12] DATA={{0}};

static u32 poly_rec_active(void)
{
    u32 state=*(volatile u32 *)0x43bd8424u|*(volatile u32 *)0x43bd83bcu;
    u32 object=*(volatile u32 *)0x444e23ecu;
    return state==1 && object && *(volatile u8 *)(object+0x169u);
}

static void poly_rec_note(u32 track,u32 note,u32 held)
{
    u8 position[4];
    s32 step;
    const volatile u8 *data;
    struct rec_chord *chord=&rec_chords[track];
    u32 length;
    if(!poly_seq_config.sequence || !poly_rec_active())return;
    data=(const volatile u8 *)poly_seq_config.sequence+track*0x499u;
    POLY_CALL(0x4008dae6u,void (*)(u32,u32,u8 *))(*(volatile u32 *)0x800046ecu,track,position);
    step=(s32)(signed char)position[0];
    if(step<0)step=0;
    if(step>63)step=63;
    length=data[0x458];
    if(length) {
        step=(s32)(signed char)data[0x459u+(u32)step%length];
        if(step<0)step=0;
        if(step>63)step=63;
    }
    if(held && chord->count && chord->step==(u8)step && chord->sequence==poly_seq_config.sequence) {
        if(chord->count>=4)return;
        chord->notes[chord->count++]=(u8)note;
    }else {
        chord->sequence=poly_seq_config.sequence;chord->step=(u8)step;
        chord->notes[0]=(u8)note;chord->count=1;
    }
    chord->age=0;
    chord->dirty=1;
}

/* UI loop only (draw dispatch): the same context as the native recorder. */
void poly_rec_ui(void)
{
    u32 t,k,interval,app,pattern,trk;
    volatile u8 *model,*mirror;
    struct rec_chord *chord;
    u8 lanes[4];
    for(t=0;t<12;t++) {
        chord=&rec_chords[t];
        if(!chord->dirty)continue;
        if(chord->sequence!=poly_seq_config.sequence || ++chord->age>2000u){chord->dirty=0;continue;}
        app=*(volatile u32 *)0x444e23f4u;
        if(!app)continue;
        pattern=POLY_CALL(0x40019000u,u32 (*)(u32))(app);
        if(!pattern)continue;
        trk=pattern+(t+1u)*0x70u;
        model=((volatile u8 *(*)(u32))(*(u32 **)trk)[0x28/4])(trk);
        if(!model)continue;
        model+=chord->step;
        /* The recorder has not written the newest key of this step yet. */
        if(model[0x2c0]!=chord->notes[chord->count-1u] || model[0x300]!=255u || model[0x340]!=255u ||
           model[0x380]!=255u)continue;
        lanes[0]=chord->notes[0];
        for(k=1;k<4;k++) {
            lanes[k]=64;
            if(k<chord->count) {
                interval=(u32)chord->notes[k]+64u-chord->notes[0];
                if(interval<=127u)lanes[k]=(u8)interval;
            }
        }
        mirror=(volatile u8 *)poly_seq_config.sequence+t*0x499u+chord->step;
        for(k=0;k<4;k++){model[0x2c0u+k*0x40u]=lanes[k];mirror[0x2c0u+k*0x40u]=lanes[k];}
        chord->dirty=0;
    }
}

/* A live pitch owns its voice until the native release or a newer allocation
 * replaces it. Native mono fallback on release never creates an allocation. */
static u8 live_notes[12][4] DATA={{0}};
static u8 live_mask[12] DATA={0};
static u8 live_confirmed[12] DATA={0};
static u8 seq_timed[12] DATA={0};
static u8 seq_untimed[12] DATA={0};
static u32 live_age[12][4] DATA={{0}};
static u32 live_clock DATA=0;
static u8 live_next[12] DATA={0};
static unsigned short poly_relation_keys[12] DATA={0};

static void poly_forget_relation(u32 track)
{
    u32 key=poly_relation_keys[track];
    if(key) {
        ((volatile unsigned short *)0x43bdda3cu)[key-1u]&=(unsigned short)~(1u<<track);
        poly_relation_keys[track]=0;
    }
}

static void poly_live_reset(const u8 *affected)
{
    u32 track;
    for(track=0;track<12;track++)if(affected[track]) {
        u32 voice;
        poly_forget_relation(track);
        for(voice=0;voice<4;voice++)live_age[track][voice]=0;
        live_mask[track]=0;
        live_confirmed[track]=0;
        seq_timed[track]=0;
        seq_untimed[track]=0;
        live_next[track]=0;
    }
}

static u32 poly_live_voice(u32 track,u32 note)
{
    u32 voice,mask=live_mask[track];
    for(voice=0;mask;mask>>=1,voice++)
        if((mask&1u) && live_notes[track][voice]==note)return voice;
    return 4;
}

/* The same OS 1.42 held-note bitmap the ARP reads after note priority. */
static const volatile u32 *poly_native_held(u32 track)
{return (const volatile u32 *)(0x455406e0u+track*0x2e18u);}

static u32 poly_live_stale(u32 track,u32 mask)
{
    const volatile u32 *held=poly_native_held(track);
    u32 voice,stale=0;
    for(voice=0;mask;mask>>=1,voice++)if(mask&1u) {
        u32 note=live_notes[track][voice];
        if(!(held[note>>5]>>(note&31u)&1u))stale|=1u<<voice;
    }
    return stale;
}

static void poly_live_reap(u32 track)
{
    u32 voice,stale=poly_live_stale(track,live_confirmed[track]);
    live_confirmed[track]&=(u8)~stale;
    live_mask[track]&=(u8)~stale;
    for(voice=0;stale;stale>>=1,voice++)if(stale&1u)native_release_track(track+voice,0x7fffu);
}

static void poly_live_ui(void)
{
    u32 track;
    if(!poly_has_groups)return;
    for(track=0;track<12;track++) {
        u32 mask=live_confirmed[track];
        if(mask && poly_live_stale(track,mask)) {
            u32 command[6]={0x504f4c59u,0x4c495645u,track,0,0,0};
            /* A release suppressed by note priority has no IMMEDIATE event.
             * Recheck under native exclusion before releasing any voice. */
            native_immediate(command);
        }
    }
}

/* A finite gate can reach zero while its native held bit stays set. Some
 * untimed notes never set that bit. Keep the duration class of routed notes
 * to disambiguate zero; a merely prepared first step is not yet sounding.
 * The native timer and queue cover releases and pending notes. */
static u32 poly_voice_busy(u32 track,u32 voices,u32 live)
{
    u32 limit=(1u<<voices)-1u;
    u32 busy=live,voice,held,timed,untimed,timers;
    const volatile s32 *countdown=(const volatile s32 *)0x80003084u+track;
    const volatile u32 *bucket;
    if(live==limit)return live;
    held=*(const volatile u32 *)0x80003124u>>track;
    timed=seq_timed[track];
    untimed=*(const volatile u32 *)0x43bd83bcu==1?seq_untimed[track]:0;
    timers=(held|timed|untimed)&limit&~live;
    held=(held&~timed)|untimed;
    for(voice=0;timers;timers>>=1,voice++)if(timers&1u) {
        s32 remaining=countdown[voice];
        /* A native release request sets the countdown to one. */
        if(remaining>1 || (!remaining && (held>>voice&1u)))busy|=1u<<voice;
    }
    if(busy==limit)return limit;
    bucket=*(const volatile u32 **)0x444df434u;
    while(bucket) {
        const volatile u32 *event=(const volatile u32 *)bucket[2];
        while(event) {
            u32 voice=event[2]-track;
            if(voice<voices && event[3]-1u<2u && event[1]==1 && (event[9]&0x10201u)==0x10201u) {
                busy|=1u<<voice;
                if(busy==limit)return busy;
            }
            event=(const volatile u32 *)event[19];
        }
        bucket=(const volatile u32 *)bucket[4];
    }
    return busy;
}

static u32 poly_oldest(u32 track,u32 voices,u32 available)
{
    u32 v,voice=4;
    for(v=0;v<voices;v++)
        if((available>>v&1u) && (voice==4 || live_age[track][v]<live_age[track][voice]))voice=v;
    return voice;
}

/* Select free voices first and steal the oldest sequenced voice on overflow.
 * Keep the selected order packed until the native queue accepts the batch. */
static u32 poly_seq_voices(u32 **batch,u32 count,u32 track,u32 voices,const u32 *source,u32 *routes)
{
    u32 j,v,voice,available,busy,live,eligible,order=0;
    *routes=0;
    if((source[9]&0x10201u)!=0x10201u) {
        if(count==1) {
            for(voice=0,v=1;v<voices;v++)if(live_age[track][v]>live_age[track][voice])voice=v;
            batch[0][10]=source[10];
            poly_voice_target(batch[0],track,voice);
        }
        return count;
    }
    live=live_mask[track];
    if(poly_unison_spread(track)) {
        if(live)return 0;
        for(j=0;j<count;j++) {
            batch[j][10]=source[10];
            poly_voice_target(batch[j],track,j);
        }
        *routes=0xe4u|(((1u<<count)-1u)<<8);
        return count;
    }
    eligible=((1u<<voices)-1u)&~live;
    available=eligible;
    if(!available)return 0;
    busy=poly_voice_busy(track,voices,live);
    for(j=0;j<count && available;j++) {
        u32 free=available&~busy;
        voice=poly_oldest(track,voices,free?free:available);
        available&=~(1u<<voice);
        batch[j][10]=source[10];
        poly_voice_target(batch[j],track,voice);
        order|=voice<<(j*2u);
    }
    *routes=order|((eligible^available)<<8);
    return j;
}

static void poly_seq_committed(u32 track,u32 count,u32 routes,u32 length)
{
    u32 used=routes>>8;
    if(length) {
        seq_timed[track]|=(u8)used;
        seq_untimed[track]&=(u8)~used;
    }else {
        seq_timed[track]&=(u8)~used;
        seq_untimed[track]|=(u8)used;
    }
    for(;count;count--,routes>>=2)live_age[track][routes&3u]=++live_clock;
}

/* Trigger relations are defined per logical track. Older kits may name a
 * voice slot directly. Fold its bit onto the owner without changing the kit. */
static u32 poly_relation_mask(const volatile u8 *kit,u32 source,u32 stock)
{
    u32 owner=poly_effective_owner(source),track,mask=stock;
    if(owner!=source)mask=*(const volatile unsigned short *)(kit+0x15e4u+2*owner);
    for(track=1;track<12 && (mask>>track);track++)if(mask>>track&1u) {
        u32 target=poly_owner_map[track];
        if(target!=track)mask=(mask&~(1u<<track))|(1u<<target);
    }
    return owner<13?mask&~(1u<<owner):mask;
}

static u32 poly_relation_route(const u32 *parent,const volatile u8 *kit,u32 stock)
{
    u32 track,mask=poly_relation_mask(kit,parent[2],stock),physical=mask,previous=0;
    u32 pitch=parent[6],source=parent[2];
    u32 owner=poly_effective_owner(source);
    if(owner!=source && poly_unison_spread(owner))return 0;
    if(mask && parent[3]==2 && (parent[9]&0x81u)==0x81u && source<13 && pitch<128) {
        /* Stock stores one mask per source and pitch. A live retrigger must
         * reuse the same sounding voice so its next Note Off can reach it. */
        previous=((const volatile unsigned short *)0x43bdda3cu)[128*source+pitch];
        previous&=*(const volatile u32 *)0x80003124u;
    }
    for(track=0;track<12 && (mask>>track);track++)if(mask>>track&1u) {
        u32 state=poly_seq_config.states[track];
        if(state>=3) {
            u32 voices=state-1,live=live_mask[track];
            u32 available=((1u<<voices)-1u)&~live;
            physical&=~(1u<<track);
            if(available) {
                if(poly_unison_spread(track)) {
                    if(!live)physical|=available<<track;
                    continue;
                }
                u32 voice,retrigger=(previous>>track)&available;
                for(voice=0;voice<voices;voice++)
                    if((retrigger>>voice&1u) && ((const volatile u32 *)0x45512208u)[track+voice]==pitch)break;
                if(voice==voices) {
                    u32 free=available&~poly_voice_busy(track,voices,live);
                    voice=poly_oldest(track,voices,free?free:available);
                }
                physical|=1u<<(track+voice);
            }
        }
    }
    return physical;
}

static void poly_relation_event(u32 *event)
{
    u32 track=event[2],owner,voice,bit,key=0;
    if(track>=12)return;
    owner=poly_owner_map[track];
    if(poly_seq_config.states[owner]<3)return;
    if(event[3]==2 && event[18]<13 && event[6]<128)key=1u+128u*event[18]+event[6];
    if(event[1]!=1) {
        if(event[1]==2 && poly_relation_keys[track]==key)poly_relation_keys[track]=0;
        return;
    }
    if((event[9]&0x81u)!=0x81u)key=0;
    if(poly_relation_keys[track]!=key) {
        poly_forget_relation(track);
        poly_relation_keys[track]=(unsigned short)key;
    }
    voice=track-owner;bit=1u<<voice;
    if(voice) {
        u32 kit=*(volatile u32 *)0x800030b8u;
        if(kit)event[10]=kit+poly_sound_offset[owner];
    }
    /* Native relation events set the held bit and release through the
     * stock relation table. Retire an older duration class when reusing it. */
    seq_untimed[owner]&=(u8)~bit;
    seq_timed[owner]&=(u8)~bit;
    if(event[12])seq_timed[owner]|=(u8)bit;
    live_age[owner][voice]=++live_clock;
}

/* The first-step statics play voices 0.. natively: mark them started. */
static void poly_seq_started(u32 track,u32 played)
{
    u32 v,timed=0;
    for(v=0;v<4;v++)if(played>>v&1u) {
        if(PREP_EVENT(track+v)[12])timed|=1u<<v;
        live_age[track][v]=++live_clock;
    }
    seq_timed[track]=(u8)timed;
    seq_untimed[track]=0;
}

static void poly_live_press(u32 *event,u32 track,u32 voices)
{
    u32 note=event[6],i,v,voice=poly_live_voice(track,note);
    if(voice==4) {
        u32 live=live_mask[track],busy=poly_voice_busy(track,voices,live);
        for(i=0;i<voices;i++) {
            v=(u32)live_next[track]+i;
            if(v>=voices)v-=voices;
            if(!(busy>>v&1u)){voice=v;break;}
        }
        if(voice==4)voice=poly_oldest(track,voices,(1u<<voices)-1u);
        live_notes[track][voice]=(u8)note;
        live_mask[track]=(u8)(live|(1u<<voice));
        live_confirmed[track]&=(u8)~(1u<<voice);
        seq_timed[track]&=(u8)~(1u<<voice);
        seq_untimed[track]&=(u8)~(1u<<voice);
        live_next[track]=(u8)(voice+1u<voices?voice+1u:0);
        poly_rec_note(track,note,live!=0);
    }
    if(poly_native_held(track)[note>>5]>>(note&31u)&1u)live_confirmed[track]|=(u8)(1u<<voice);
    poly_forget_relation(track+voice);
    live_age[track][voice]=++live_clock;
    /* Voice 0 is the master's own event, untouched; SLAVE fields as FM_FAST. */
    if(voice)poly_voice_target(event,track,voice);
}

/* FM_FAST chord (restored in  for TRIG keys and step preview): the master's
 * note also plays NOT2..NOT4 (held step lane, else the defaults) on its SLAVE
 * voices. A note-off repeats each voice's offset, so the native handling of
 * a non-current note is mirrored, not reinterpreted. */
static u32 chord_root[12] DATA={0};
static u8 chord_notes[12][4] DATA={{0}};
static u32 chord_generation[12] DATA={0};

static u32 poly_live_chord(u32 *event,u32 track,u32 voices)
{
    u32 root=event[6],i,count=1,step;
    u32 *batch[4];
    s32 note;
    batch[0]=event;
    if(event[1]==1) {
        step=*(volatile u32 *)0x402d5aacu;
        for(i=1;i<4;i++)chord_notes[track][i]=128;
        for(i=1;i<voices;i++) {
            note=poly_chord_note((const volatile u8 *)poly_seq_config.sequence,track,step,i,root);
            if(note<0)continue;
            chord_notes[track][i]=(u8)note;
        }
    }else if(chord_generation[track]!=poly_seq_config.generation)return 0;
    for(i=1;i<voices;i++) {
        if(chord_notes[track][i]>127)continue;
        note=(s32)chord_notes[track][i]+(s32)root-(s32)chord_root[track];
        if(event[1]==1)note=chord_notes[track][i];
        if(note<0 || note>(track<8?127:84))continue;
        batch[count]=copy_event(event);
        if(!batch[count])break;
        poly_voice_target(batch[count],track,i);
        batch[count][6]=(u32)note;count++;
    }
    if(i<voices || !poly_submit(batch,count,0,1)) {
        /* Never a partial chord; the master keeps its native behavior. */
        while(count>1)native_event_free(batch[--count]);
        if(event[1]==1)chord_generation[track]=0;
        return 0;
    }
    if(event[1]==1) {
        chord_root[track]=root;
        chord_generation[track]=poly_seq_config.generation;
    }else if(root==chord_root[track])chord_generation[track]=0;
    return 1;
}

static u32 poly_live_unison(u32 *event,u32 track,u32 voices,u32 origin)
{
    u32 note=event[6],voice,mask,off=event[1]==2;
    u32 *batch[4];
    if(!origin)return poly_live_chord(event,track,voices);
    if(off && (!live_mask[track] || live_notes[track][0]!=note)) {
        if(!event[0])native_event_free(event);
        return 1;
    }
    batch[0]=event;
    for(voice=1;voice<voices;voice++) {
        batch[voice]=copy_event(event);
        if(!batch[voice])break;
        poly_voice_target(batch[voice],track,voice);
    }
    if(voice<voices || !poly_submit(batch,voices,0,1)) {
        while(voice>1)native_event_free(batch[--voice]);
        if(!event[0])native_event_free(event);
        if(!off)return 1;
        for(voice=0;voice<voices;voice++)native_release_track(track+voice,0x7fffu);
    }
    if(off) {
        live_mask[track]=live_confirmed[track]=0;
        return 1;
    }
    mask=(1u<<voices)-1u;
    live_mask[track]=(u8)mask;
    live_confirmed[track]=(poly_native_held(track)[note>>5]>>(note&31u)&1u)?(u8)mask:0;
    seq_timed[track]=seq_untimed[track]=0;
    for(voice=0;voice<voices;voice++) {
        poly_forget_relation(track+voice);
        live_notes[track][voice]=(u8)note;
        live_age[track][voice]=++live_clock;
    }
    return 1;
}

static u32 poly_live_locked(u32 *event)
{
    u32 track=event[2],note=event[6],voices,origin;
    if(event[1]!=1 && event[1]!=2)return 0;
    poly_sync_locked();
    if(poly_seq_config.owners[track]<track) {
        /* A SLAVE plays only through its master. */
        if(!event[0])native_event_free(event);
        return 1;
    }
    if(poly_seq_config.states[track]<3 || note>127)return 0;
    voices=poly_seq_config.states[track]-1u;
    if(poly_unison_spread(track))return poly_live_unison(event,track,voices,poly_live_from_keys());
    if(event[1]==2) {
        u32 voice=poly_live_voice(track,note);
        if(voice<4) {
            live_mask[track]&=(u8)~(1u<<voice);
            live_confirmed[track]&=(u8)~(1u<<voice);
            if(voice)poly_voice_target(event,track,voice);
            return 0;
        }
    }
    origin=poly_live_from_keys();
    if(!origin)return poly_live_chord(event,track,voices);
    if(origin==2 && event[1]==1) {
        poly_live_press(event,track,voices);
        return 0;
    }
    if(!event[0])native_event_free(event);
    return 1;
}

/* This hook runs after the original native function has saved SR and raised
 * its IRQ mask. Recursive submissions take its original continuation only. */
u32 poly_preview_locked(u32 *event)
{
    u32 track=event[2];
    if(event[0]==0x504f4c59u) {
        if(event[1]==0x434f5059u) {
            event[5]=poly_copy_pending;poly_copy_pending=0;return 1;
        }
        if(event[1]==0x43464731u) {
            event[5]=poly_record_write_locked(event[2],event[3],event[4]);return 1;
        }
        if(event[1]==0x43464732u) {
            if(event[2]<POLY_MODE_TRACKS && event[3]<2 && poly_seq_config.states[event[2]]>=3)
                event[5]=poly_record_settings_locked(event[2],1,poly_seq_config.states[event[2]]-1,event[3],~0u);
            return 1;
        }
        if(event[1]==0x554e4953u) {
            if(poly_has_unison_api() && event[2]<POLY_MODE_TRACKS && event[3]<=POLY_UNISON_MAX &&
               poly_seq_config.states[event[2]]>=3)
                event[5]=poly_record_settings_locked(event[2],1,poly_seq_config.states[event[2]]-1,4,event[3]);
            return 1;
        }
        if(event[1]==0x414e4c47u) {poly_analog_publish_locked();return 1;}
        if(event[1]==0x53594e43u) {event[5]=poly_sync_locked();return 1;}
        if(event[1]==0x4c495645u && track<12) {poly_live_reap(track);return 1;}
    }
    if(preview_depth || track>=12)return 0;
    if(event[3]==2) {
        u32 handled;
        preview_depth=1;
        handled=poly_live_locked(event);
        preview_depth=0;
        return handled;
    }
    if(event[3]!=1 || event[11])return 0;
    poly_sync_locked();
    if(poly_seq_config.owners[track]==track &&
       (poly_seq_config.states[track]<3 ||
        (*(volatile u32 *)0x402d5aacu>=64 &&
         preview_generation[track]!=poly_seq_config.generation)))return 0;
    preview_depth=1;
    poly_preview(event);
    preview_depth=0;
    return 1;
}

u32 poly_sound_write_allowed(u32 track)
{
    volatile u32 allowed=0;
    poly_sync();
    if(track>=12)return 1;
    if(poly_seq_config.owners[track]==track) {
        allowed=1;
        if(poly_seq_config.states[track]>=3)poly_copy_mark(track);
    }
    return allowed;
}

#include "poly_ui.c"
