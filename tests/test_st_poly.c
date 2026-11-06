/* Production digital POLY on a 32-bit host with explicit native doubles.
 * No target firmware function, MIDI port or CPU emulator is executed.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <syntakt/host.h>
typedef unsigned int T;typedef unsigned char B;
static T poly_test_origin;
static void *poly_test_function(T);
#define POLY_HOST_TEST 1
#define POLY_CALL(address,type) ((type)poly_test_function(address))
#include "../mods/poly-st/main.c"

#define SEQ ((B *)0x44000000u)
#define KIT ((B *)0x43000000u)
#define PATTERN ((B *)0x45000000u)
static T events[128][20],used[128],queue[256][20],queued,releases,publishes,cancelled;
static T fail_after,allocations,refcount=1,trig_calls,model_notifies,draws,immediate_calls;
static T policy[13],increments[7],native_layout[11],tracked=0;
static T rec_step,rec_track,trig_flags=0x10201u;  /* +24h of a note trig */
static B rec_model[0x500];
static B view[512],model[64],header[256];
static char labels[512];

T *native_event_alloc(void)
{
    T i;allocations++;
    if(fail_after && allocations>=fail_after)return 0;
    for(i=0;i<128;i++)if(!used[i]){used[i]=1;memset(events[i],0,80);return events[i];}
    return 0;
}
void native_event_copy(T *d,const T *s)
{memcpy(d,s,80);if(!s[0] && s[17])(*(T *)s[17])++;}
void native_event_free(T *e)
{
    T i=(T)(e-events[0])/20;
    assert(i<128 && used[i]);used[i]=0;
    if(e[17]){assert(*(T *)e[17]);(*(T *)e[17])--;}
}
static void sink(T *e)
{assert(queued<256);memcpy(queue[queued++],e,80);if(!e[0])native_event_free(e);}
void native_enqueue(T *e,T time){(void)time;sink(e);}
void native_immediate(T *e){immediate_calls++;if(!poly_preview_locked(e))sink(e);}
void native_cancel_track(T *e,T t){(void)e;(void)t;cancelled++;}
void native_release_track(T t,T n){assert(t<12 && n==0x7fff);releases++;}
void native_publish_sound(const volatile B *s,T t)
{T i,owner=poly_seq_config.owners[t];assert(t<12 && s>=KIT+0x2a && s<KIT+0x2a+12*0x123);publishes++;
 for(i=0;i<0x8e;i++)((B *)0x800021ccu)[t*0x8eu+i]=s[i];
 if(t>=8 && owner<t && s==KIT+0x2a+owner*0x123)
     *(B *)(0x80002f50u+t)=*(B *)(0x80002f50u+owner);
 else *(B *)(0x80002f50u+t)=s[0xa2];}
static T projections;
void poly_fast_publish(B *w,const volatile B *o){projections++;poly_publish(w,o);}
void poly_fast_publish_span(B *w,const volatile B *o,T span)
{T t;for(t=1;t<12;t++)if(o[t]<t)assert(t>=(span>>8) && t<(span&255));
 poly_fast_publish(w,o);}
void poly_fast_publish_one(B *w,const volatile B *o,T span)
{assert((span&255)==(span>>8)+1);poly_fast_publish_span(w,o,span);}
void poly_native_shared(void){}void poly_mod_shared(void){}
void poly_native_all(void){}void poly_mod_all(void){}

static T original(T op,const T *a)
{
    T *e,i;B *d;
    switch(op){
    case ST_NATIVE_TRIG:
        trig_calls++;e=(T *)a[4];if(!e)return 0;
        for(i=0;i<20;i++)e[i]=0;
        e[0]=1;e[1]=1;e[2]=a[0];e[3]=1;e[6]=SEQ[a[0]*0x499+0x3c5];e[9]=trig_flags;e[18]=a[0];
        if(a[5])*(T *)a[5]=1000;return (T)e;
    case ST_NATIVE_ENQUEUE:native_enqueue((T *)a[0],a[1]);return 1;
    case ST_NATIVE_HEADER_LOAD:memcpy((void *)a[0],(void *)a[1],256);return 1;
    case ST_NATIVE_HEADER_RESET:d=(B *)a[0];if(!a[1])memset(d,0,256);return 0;
    case ST_NATIVE_PAGE_LAYOUT:return (T)native_layout;
    default:assert(0);return 0;
    }
}
static T track_mock(T m){assert(m==(T)model);return tracked;}
static T pattern_mock(T app){assert(app==0x1234u);return (T)PATTERN;}
static B *header_mock(void *m){assert(m==PATTERN+0x2c);return header;}
static void notify_mock(void *m,T zero){assert(m==PATTERN+0x2c && !zero);model_notifies++;}
static void *new_mock(T bytes){return malloc(bytes);}
static void delete_mock(void *p){free(p);}
static T event_check(void *e,T mask){(void)e;assert(mask==0xfffffffdu);return 1;}
static T position_mock(T *e){return e[1];}
static T policy_mock(T id){assert(id==0x13u);return (T)policy;}
static int delta_mock(T *e,T a,T b){(void)a;(void)b;return (int)e[2];}
static T pressed_mock(void *e){(void)e;return 0;}
static int increment_mock(void *v,T pos,int delta,void *inc,T down)
{assert(v==view+0x94 && pos<8 && inc && !down);return delta;}
static void dirty_mock(void *v){assert(v==view);}
static T key_mock(T *e){return e[0];}
static T zero_mock(void *v,T pos){(void)v;(void)pos;return 0;}
static T get_mock(void *v,T id,B *locked){assert(v==view);*locked=0;return id==0x15 || id==0x14?60:68;}
static void text_mock(void *ctx,void *font,int x,int y,T flags,const char *fmt,const char *s)
{(void)ctx;(void)font;(void)x;(void)y;(void)flags;(void)fmt;strcat(labels,s);strcat(labels,";");draws++;}
static void shape_mock(void *ctx,int x,int y,int xx,int yy,T color)
{(void)ctx;(void)x;(void)y;(void)xx;(void)yy;(void)color;draws++;}
static void toggle_mock(void *v,T on,void *ctx,int x,int y,T flag)
{(void)v;(void)on;(void)ctx;(void)x;(void)y;(void)flag;draws++;}
static void tail_mock(void *v,void *ctx,T a){(void)v;(void)ctx;(void)a;}
static void widget_mock(void *v,void *ctx,int x,int y,T id,T value,T state,T lock,T turning,T hide)
{(void)v;(void)ctx;(void)x;(void)y;(void)id;(void)value;(void)state;(void)lock;(void)turning;(void)hide;}
static void page_mock(void *v,void *ctx,T a,T b,T c){(void)v;(void)ctx;(void)a;(void)b;(void)c;}
static void blit_mock(void *ctx,void *bmp,int x,int y,T flag){(void)ctx;(void)bmp;(void)x;(void)y;(void)flag;}
static void rec_position(T clock,T track,B *out){(void)clock;assert(track<12);memset(out,0,4);}
static B *rec_model_mock(T track){assert(track==(T)PATTERN+(rec_track+1)*0x70);return rec_model;}
static void *poly_test_function(T address)
{
    switch(address){
    case 0x4001ff74:return track_mock;case 0x40103788:return new_mock;case 0x40103794:return delete_mock;
    case 0x40019000:return pattern_mock;case 0x400f660a:return event_check;case 0x400f6632:return position_mock;
    case 0x4007b9b2:return policy_mock;case 0x400f66a2:return delta_mock;case 0x400f66c0:return pressed_mock;
    case 0x400f68d2:return increment_mock;case 0x400ffc5e:return dirty_mock;case 0x400f95c8:return key_mock;
    case 0x400f68aa:case 0x400f6abc:return zero_mock;case 0x40039160:return get_mock;
    case 0x400f8a0c:return text_mock;case 0x400f7df2:case 0x400f7bd6:case 0x400f8104:return shape_mock;
    case 0x40070362:return toggle_mock;case 0x4003a1c2:return page_mock;case 0x400f8dac:return blit_mock;
    case 0x4008da6e:return rec_position;
    case 0x40115f78:return memcpy;
    default:fprintf(stderr,"unexpected native function %08x\n",address);abort();
    }
}
static const struct st_machine_host_api services={.version=2,.native_original=original};
static void reset(void)
{
    T i;
    memset(SEQ,0,0x22000);memset(KIT,0,0x1600);
    memset(events,0,sizeof events);memset(used,0,sizeof used);queued=releases=publishes=cancelled=0;
    fail_after=allocations=trig_calls=rec_track=0;refcount=1;poly_test_origin=0;
    memset((void *)&poly_seq_config,0,sizeof poly_seq_config);
    poly_seq_config.generation=1;for(i=0;i<12;i++)poly_seq_config.owners[i]=(B)i;
    poly_has_groups=poly_analog_active=0;poly_publish_end=1;
    cache_sequence=0;cache_midi_mask=0xffffffffu;preview_depth=0;
    memset(prep_generation,0,sizeof prep_generation);memset(live_generation,0,sizeof live_generation);
    memset(chord_generation,0,sizeof chord_generation);memset(preview_generation,0,sizeof preview_generation);
    memset(rec_chords,0,sizeof rec_chords);memset(live_age,0,sizeof live_age);live_clock=0;
    memset(poly_modes,0,8);memset(poly_next_modes,0,8);poly_shared_mask=poly_copy_pending=0;
    *(T *)0x800030b8=0;
    for(i=0;i<12;i++)poly_lfo_map[i]=poly_post_map[i]=(B)i;
    *(B **)0x43bd7430u=SEQ;*(B **)0x43bd743cu=KIT;
    *(T *)0x444de434u=0;*(T *)0x444de43cu=64;*(T *)0x402d575cu=0xffffffffu;
    *(T *)0x43bd7424u=0;*(T *)0x43bd73bcu=0;*(T *)0x444e13ecu=0;
    for(i=0;i<12;i++){
        B *tr=SEQ+i*0x499;tr[0x3c5]=60;tr[0x3c6]=68;tr[0x3c7]=71;tr[0x3c8]=76;
        memset(tr+0x2c0,255,256);*(T *)(0x43bd73c8u+i*4)=0;
        *(B *)(0x43bd74efu+i)=0;*(B *)(0x43bd74fcu+i)=0;
    }
    poly_host=&services;
}
static void drain(void){T i;for(i=0;i<128;i++)assert(!used[i]);queued=0;}
static void live(T note,T on,T origin)
{T e[20]={1,on,0,2,0,0,note};poly_test_origin=origin;native_immediate(e);}
static void play_codec(void)
{
    B raw[7],states[12],modes[8],decoded[12],plays[8],owners[12];T rank,crc,i;
    for(rank=0;rank<poly_counts[8];rank++) {
        raw[0]=0x50;raw[1]=0x52;raw[2]=(B)(rank>>16);raw[3]=(B)(rank>>8);raw[4]=(B)rank;
        crc=poly_crc(raw);raw[5]=(B)(crc>>8);raw[6]=(B)crc;
        assert(poly_decode_record(raw,states,modes) && poly_resolve(states,owners));
        assert(poly_rank(states,modes,0)==rank);
        assert(poly_decode(raw,decoded,plays));
        for(i=0;i<8;i++)assert(decoded[i]==states[i] && plays[i]==(modes[i]==1?1:0));
        memcpy(modes,plays,8);poly_encode(raw,states,modes);
        assert(poly_decode(raw,decoded,plays));
        assert(!memcmp(states,decoded,8) && !memcmp(modes,plays,8));
    }
    /* Every single-bit corruption is rejected, including the new tag. */
    for(i=0;i<56;i++){raw[i/8]^=(B)(1u<<(i%8));assert(!valid(raw));raw[i/8]^=(B)(1u<<(i%8));}
    printf("PASS: %u ranked PLAY/topology records, legacy round trips and CRC corruption\n",poly_counts[8]);
}

static void play_modes(void)
{
    B original_state[80],working[0x754+13*0x7c],saved[7],states[12],modes[8];
    T i,mode,e[20],time,result,q=1,crc,rank,pub[1]={(T)working};
    reset();assert(poly_record_write(0,1,4));
    for(i=0;i<80;i++)((B *)0x444da77cu)[i]=original_state[i]=(B)(i+7);
    assert(poly_mode_write(0,1) && poly_modes[0]==1 && valid(SEQ+0x21381));
    assert(poly_owner_map[1]==0 && poly_lfo_map[1]==1 && poly_post_map[1]==1);
    assert(!poly_native(ST_NATIVE_OWNERS,&q,&result));
    assert(!memcmp((void *)0x444da77cu,original_state,80));
    memset((void *)0x444da77cu,0x66,80);memset(original_state,0x66,80);
    memcpy(saved,SEQ+0x21381,7);cache_sequence=0;assert(poly_sync() && poly_modes[0]==1);
    memset(working,0,sizeof working);working[0x1c+0x8e +0x20]=0x55;
    assert(!poly_native(ST_NATIVE_PUBLISH,pub,&result));assert(working[0x1c+0x8e +0x20]==0x55);
    assert(poly_mode_write(0,0) && SEQ[0x21382]==0x51 && poly_lfo_map[1]==0);
    assert(!memcmp((void *)0x444da77cu,original_state,80));
    for(mode=2;mode<=3;mode++) {
        assert(!poly_mode_write(0,mode)); /* no private command can select retired modes */
        assert(poly_decode(saved,states,modes));modes[0]=(B)mode;
        rank=poly_rank(states,modes,0);
        SEQ[0x21381]=0x50;SEQ[0x21382]=0x52;SEQ[0x21383]=(B)(rank>>16);
        SEQ[0x21384]=(B)(rank>>8);SEQ[0x21385]=(B)rank;
        crc=poly_crc(SEQ+0x21381);SEQ[0x21386]=(B)(crc>>8);SEQ[0x21387]=(B)crc;
        assert(poly_sync() && poly_modes[0]==0 && poly_seq_config.states[0]==5);
        live(60,1,2);live(64,1,2);assert(queued==2 && queue[0][2]!=queue[1][2]);drain();
        live(60,2,1);live(64,2,1);assert(queued==2);drain();
        memset(e,0,sizeof e);e[0]=1;assert(poly_trig(0,SEQ,(T)KIT,0,e,&time)==e);
        assert(poly_sequence_enqueue(e,time) && queued==4);drain();
    }
    assert(poly_mode_write(0,1) && poly_modes[0]==1);
    assert(!poly_mode_write(1,1) && !poly_mode_write(8,1) && !poly_mode_write(0,4));
}

static void copy_on_change(void)
{
    T i,n,args[3]={0},result=0;B removed[0x123],working[0x754+13*0x7c];
    reset();*(T *)0x800030b8=(T)KIT;
    for(i=0;i<0x123;i++)KIT[0x2a+i]=(B)(i*3);KIT[0x2a+0xa2]=65;
    memset(KIT+0x2a+0x123,0x55,0x123*3);
    assert(poly_record_write(0,1,3));
    assert(!memcmp(KIT+0x2a,KIT+0x2a+0x123,0x123));
    assert(!memcmp(KIT+0x2a,KIT+0x2a+2*0x123,0x123));
    assert(KIT[0x2a+3*0x123]==0x55);
    poly_copy_ui();assert(!poly_copy_pending);n=publishes;
    assert(poly_mode_write(0,1));
    args[0]=(T)working;memset(working,0x73,sizeof working);
    for(i=0;i<1500;i++) {
        poly_frame();poly_copy_ui();
        assert(!poly_native(ST_NATIVE_MODULATION,0,&result));
        assert(!poly_native(ST_NATIVE_LFO,args,&result));
        assert(!poly_native(ST_NATIVE_PUBLISH,args,&result));
    }
    assert(publishes==n && !poly_copy_pending);
    /* An edit is propagated after the native UI setter has finished. */
    assert(poly_sound_write_allowed(0) && poly_copy_pending);
    KIT[0x2a+20]^=0x7f;poly_copy_ui();assert(!poly_copy_pending && publishes==n+2);
    assert(!memcmp(KIT+0x2a,KIT+0x2a+0x123,0x123));n=publishes;
    assert(poly_record_write(0,1,4));assert(publishes==n+1); /* only new voice */
    poly_copy_ui();memcpy(removed,KIT+0x2a+3*0x123,0x123);n=publishes;
    assert(poly_record_write(0,1,2));poly_copy_ui();assert(publishes==n); /* no restore */
    assert(!memcmp(removed,KIT+0x2a+3*0x123,0x123));
    assert(poly_record_write(0,0,2));poly_copy_ui();assert(publishes==n);
}

static void topology(void)
{
    T code,i,j,count=0,valids=0;B states[12]={0},owners[12];
    for(code=0;code<65536;code++){
        T n=code;
        for(i=0;i<8;i++){states[i]=(B)(n&3u);n>>=2;states[i]=states[i]?states[i]+2:0;}
        if(!poly_resolve(states,owners))continue;
        /* Bounds are cached during configuration, never found in the IRQ. */
        for(T modes=0;modes<256;modes++) {
            T first=12,end=12;
            for(T t=0;t<8;t++)poly_modes[t]=(B)((modes>>t)&1);
            poly_play_maps(owners);
            for(T t=1;t<12;t++)if(poly_post_map[t]<t){if(first==12)first=t;end=t+1;}
            assert(poly_publish_span==((first<<8)|end));
            assert(poly_publish_entry==(end==first+1?poly_fast_publish_one:poly_fast_publish_span));
            for(T analog=0;analog<4;analog++) {
                B all[12],decoded[12],raw[7],plays[8];
                memcpy(all,states,12);
                if(analog==1)all[8]=3;
                if(analog==2)all[8]=4;
                if(analog==3)all[9]=3;
                poly_encode(raw,all,poly_modes);
                assert(valid(raw) && poly_decode(raw,decoded,plays));
                assert(!memcmp(all,decoded,12));
                for(T k=0;k<8;k++)if(all[k]>=3)assert(plays[k]==poly_modes[k]);
            }
        }
        valids++;
        for(i=0;i<8;i++)if(states[i]>=3){for(j=1;j<states[i]-1u;j++)assert(owners[i+j]==i);i+=states[i]-2u;}
        for(i=8;i<12;i++)assert(owners[i]==i);
        count++;
    }
    assert(valids==108 && count==108);
    for(i=0;i<8;i++){memset(states,0,sizeof states);states[i]=6;assert(!poly_resolve(states,owners));}
    assert(poly_note(60,68)==64 && poly_note(127,65)==128 && poly_note(0,63)==128 && poly_note(60,64)==128);
}
static void groups_and_notes(void)
{
    T n,v,event[20],time,generation;B copy[256],bad[256];
    for(v=2;v<=4;v++){
        reset();assert(poly_record_write(0,1,v));assert(poly_seq_config.states[0]==v+1);
        for(n=0;n<v;n++)live(60+n,1,2);
        assert(queued==v);for(n=0;n<v;n++)assert(queue[n][2]==n && queue[n][6]==60+n);drain();
        for(n=0;n<v;n++)live(60+n,2,1);assert(queued==v);drain();
        live(60,1,0);assert(queued==v);for(n=0;n<v;n++)assert(queue[n][2]==n);drain();
        live(60,2,0);assert(queued==v);drain();
        memset(event,0,sizeof event);event[0]=1;
        assert(poly_trig(0,SEQ,(T)KIT,0,event,&time)==event);
        assert(poly_sequence_enqueue(event,time));assert(queued==v);drain();
        generation=poly_seq_config.generation;assert(poly_sync() && generation==poly_seq_config.generation);
        assert(!poly_record_write(1,1,2));assert(!poly_record_write(7,1,2));
        memcpy(copy,SEQ+0x21360,256);assert(valid(copy+0x21));memcpy(bad,copy,256);bad[0x27]^=1;
        memset(header,0xaa,256);assert(poly_header_load(header,copy));assert(!memcmp(header+0x21,copy+0x21,7));
        assert(poly_header_load(header,bad));for(n=0;n<7;n++)assert(!header[0x21+n]);
        memcpy(header,copy,256);poly_header_reset(header,1);assert(valid(header+0x21));
        poly_header_reset(header,0);for(n=0;n<7;n++)assert(!header[0x21+n]);
        assert(poly_record_write(0,0,v));assert(publishes==v-1);for(n=0;n<8;n++)assert(poly_owner_map[n]==n);
    }
    reset();assert(poly_record_write(0,1,2));live(60,1,2);live(62,1,2);live(64,1,2);
    assert(queued==3 && queue[2][2]==0);drain();live(60,2,1);assert(!queued);
    live(64,2,1);assert(queued==1 && queue[0][2]==0);drain();live(62,2,1);drain();
    reset();assert(poly_record_write(0,1,4));*(unsigned short *)(KIT+0x1550)=2;
    assert(poly_sync() && poly_owner_map[1]==1 && valid(SEQ+0x21381));
    assert(!poly_record_write(0,1,4));*(unsigned short *)(KIT+0x1550)=0;
    assert(poly_sync() && poly_owner_map[3]==0);
    reset();assert(poly_record_write(0,1,4));fail_after=allocations+2;
    live(60,1,0);assert(queued==1 && queue[0][2]==0);drain();
    {
        T borrowed[20]={1,1,0,2,0,0,60},bucket[5]={0};
        reset();assert(poly_record_write(0,1,4));borrowed[17]=(T)&refcount;
        native_immediate(borrowed);assert(queued==4 && refcount==1);drain();
        fail_after=allocations+2;native_immediate(borrowed);assert(queued==1 && refcount==1);drain();
        fail_after=0;bucket[2]=(T)borrowed;*(T *)0x444de434u=(T)bucket;
        cancelled=0;assert(poly_record_write(0,0,4));assert(cancelled==4);
        *(T *)0x444de434u=0;
    }
}
/* 1.1: master steps in a row take the group's voices least recently started
 * first, so they overlap; chords take as many; the first-step statics count. */
static T step_voices(T step,T *notes)
{
    T e[20],time,i,mask=0;
    memset(e,0,sizeof e);e[0]=1;
    assert(poly_trig(0,SEQ,(T)KIT,step,e,&time)==e && poly_sequence_enqueue(e,time));
    for(i=0;i<queued;i++){
        assert(queue[i][2]==queue[i][18] && queue[i][2]<4 && !(mask>>queue[i][2]&1u));
        mask|=1u<<queue[i][2];if(notes)notes[i]=queue[i][6];
    }
    drain();return mask;
}
static void sequence_voices(void)
{
    T v,step,notes[4],time;
    for(v=2;v<=4;v++) {
        reset();assert(poly_record_write(0,1,v));
        SEQ[0x3c6]=SEQ[0x3c7]=SEQ[0x3c8]=64;          /* NOT2..NOT4 OFF: single notes */
        for(step=0;step<3*v;step++)assert(step_voices(step,notes)==1u<<step%v && notes[0]==60);
    }
    reset();assert(poly_record_write(0,1,4));SEQ[0x3c6]=SEQ[0x3c7]=SEQ[0x3c8]=64;
    SEQ[0x2c0+0x40+1]=67;SEQ[0x2c0+0x80+1]=71;      /* step 1: 60, 63, 67 */
    assert(step_voices(0,0)==1);
    assert(step_voices(1,notes)==14 && notes[0]==60 && notes[1]==63 && notes[2]==67);
    assert(step_voices(2,0)==1 && step_voices(1,0)==14 && step_voices(3,0)==1);
    /* The chord's root takes the oldest voice: 1 after a note on 0. */
    reset();assert(poly_record_write(0,1,4));SEQ[0x3c6]=SEQ[0x3c7]=SEQ[0x3c8]=64;
    SEQ[0x2c0+0x40+1]=67;assert(step_voices(0,0)==1);
    memset(queue,0,sizeof queue);
    {T e[20]={1},i;
     assert(poly_trig(0,SEQ,(T)KIT,1,e,&time)==e && poly_sequence_enqueue(e,time) && queued==2);
     assert(queue[0][2]==1 && queue[0][6]==60 && queue[1][2]==2 && queue[1][6]==63);
     for(i=0;i<2;i++)assert(!queue[i][10]);drain();}
    /* First step after STOP: voices 0 and 1 prepared, next notes go to 2, 3, 0. */
    reset();assert(poly_record_write(0,1,4));SEQ[0x3c6]=SEQ[0x3c7]=SEQ[0x3c8]=64;
    SEQ[0x2c0+0x40]=67;
    assert(poly_trig(0,SEQ,(T)KIT,0,(T *)PREP_EVENT(0),&time));
    assert(PREP_EVENT(1)[1] && PREP_EVENT(1)[6]==63 && !PREP_EVENT(2)[1]);
    assert(step_voices(1,0)==4 && step_voices(2,0)==8 && step_voices(3,0)==1);
    /* A trigless lock trig reaches every voice in order and moves no voice. */
    trig_flags=0x10001u;
    {T e[20]={1},i;
     assert(poly_trig(0,SEQ,(T)KIT,4,e,&time)==e && poly_sequence_enqueue(e,time) && queued==1);
     assert(queue[0][2]==0);drain();    /* voice 0 started last (step 3) */
     SEQ[0x2c0+0x40+5]=67;SEQ[0x2c0+0x80+5]=71;SEQ[0x2c0+0xc0+5]=76;
     assert(poly_trig(0,SEQ,(T)KIT,5,e,&time)==e && poly_sequence_enqueue(e,time) && queued==4);
     for(i=0;i<4;i++)assert(queue[i][2]==i);drain();}
    trig_flags=0x10201u;assert(step_voices(6,0)==2);
    trig_flags=0x10001u;assert(step_voices(7,0)==2);trig_flags=0x10201u;assert(step_voices(8,0)==4);
    /* Analog: the slave voice still binds the master's kit Sound. */
    reset();assert(poly_record_write(8,1,3));SEQ[8*0x499+0x3c6]=SEQ[8*0x499+0x3c7]=64;
    for(step=0;step<6;step++) {
        T e[20]={1};
        assert(poly_trig(8,SEQ,(T)KIT,step,e,&time)==e && poly_sequence_enqueue(e,time) && queued==1);
        assert(queue[0][2]==8+step%3 && queue[0][10]==(step%3?(T)KIT+0x2a+8*0x123:0));drain();
    }
    printf("PASS: sequencer notes rotate over 2/3/4 voices, chords take the oldest voices, first-step statics, analog Sounds\n");
}
static void ui(void)
{
    T evt[4]={0},i,*pages,vtable[16]={0};
    reset();memset(view,0,sizeof view);memset(model,0,sizeof model);memset(header,0,sizeof header);
    tracked=0;*(T *)view=0x401c97fcu;*(T *)(0x401c97fcu+0x88)=0x4003792c;
    *(T *)(0x401c97fcu+0xb8)=(T)tail_mock;*(void **)(view+0x74)=model;
    *(T *)(0x401c97fcu+0xb4)=(T)widget_mock;
    *(T *)(view+0x88)=0x12;*(T *)(view+0x68)=0x1234;
    pages=malloc(8);pages[0]=2;pages[1]=3;
    *(T **)(view+0x7c)=pages;*(T **)(view+0x80)=pages+2;*(T **)(view+0x84)=pages+2;
    vtable[0x28/4]=(T)header_mock;vtable[0x10/4]=(T)notify_mock;*(T **)(PATTERN+0x2c)=vtable;
    policy[4]=(T)increments;
    poly_ui_sync(view);pages=*(T **)(view+0x7c);assert(pages[2]==3 && *(T **)(view+0x80)==pages+3);
    *(T *)(view+0x90)=2;assert(poly_ui_third(view));evt[2]=1;
    assert(poly_ui_encoder(view,evt));assert(poly_seq_config.states[0]==3 && model_notifies==1 && valid(header+0x21));
    evt[1]=1;assert(poly_ui_encoder(view,evt));assert(poly_seq_config.states[0]==4);
    evt[1]=4;assert(!poly_ui_encoder(view,evt));assert(poly_ui_control(view,4)==0x15);
    labels[0]=0;assert(poly_ui_draw(view,0));assert(strstr(labels,"POLY;") && strstr(labels,"VOICES;"));
    *(T *)(KIT+0x2a+0x10a)=0x12345678;*(T *)(KIT+0x2a+0x10e)=0x87654321;
    evt[1]=2;evt[2]=(T)-1;assert(poly_ui_encoder(view,evt) && poly_modes[0]==1);
    assert(poly_ui_encoder(view,evt) && poly_modes[0]==1); /* left bound */
    labels[0]=0;assert(poly_ui_draw(view,0));
    assert(strstr(labels,"PLAY;") && strstr(labels,"POLY;") && !strstr(labels,"PRIO;") && !strstr(labels,"MONO;") && !strstr(labels,"LEG;"));
    evt[2]=1;assert(poly_ui_encoder(view,evt) && poly_modes[0]==0);
    assert(poly_ui_encoder(view,evt) && poly_modes[0]==0); /* right bound */
    assert(valid(header+0x21) && !memcmp(header+0x21,SEQ+0x21381,7));
    evt[1]=3;assert(poly_ui_encoder(view,evt) && poly_modes[0]==0); /* D is empty */
    evt[2]=(T)-1;assert(poly_ui_encoder(view,evt) && poly_modes[0]==0);
    assert(*(T *)(KIT+0x2a+0x10a)==0x12345678 && *(T *)(KIT+0x2a+0x10e)==0x87654321);
    evt[2]=1;
    tracked=1;labels[0]=0;assert(poly_ui_draw(view,0));assert(strstr(labels,"TRACK IN;") && strstr(labels,"SLAVE MODE;"));
    /* Slave: only the other parameter page keys (19..23) are blocked. */
    for(i=19;i<=23;i++){evt[0]=i;assert(poly_ui_block_key(view,evt));}
    evt[0]=1;assert(!poly_ui_block_key(view,evt)); /* FUNC */
    evt[0]=2;assert(!poly_ui_block_key(view,evt));
    evt[0]=12;assert(!poly_ui_block_key(view,evt));evt[0]=16;assert(!poly_ui_block_key(view,evt));
    evt[0]=24;assert(!poly_ui_block_key(view,evt)); /* trig keys */
    *(T *)(view+0x88)=20;evt[0]=20;assert(!poly_ui_block_key(view,evt));*(T *)(view+0x88)=0x12;
    evt[1]=4;assert(poly_ui_encoder(view,evt));
    tracked=0;evt[1]=0;evt[2]=(T)-1;assert(poly_ui_encoder(view,evt));
    assert(poly_seq_config.states[0]==1 && !poly_ui_slave(view));
    evt[1]=1;evt[2]=1;assert(poly_ui_encoder(view,evt));assert(poly_seq_config.states[0]==1);
    assert(!poly_ui_control(view,4));labels[0]=0;assert(poly_ui_draw(view,0));
    assert(strstr(labels,"POLY;") && !strstr(labels,"VOICES;"));
    tracked=8;poly_ui_sync(view);assert(*(T **)(view+0x80)==pages+2 && !*(T *)(view+0x90));
    for(i=0;i<8;i++)assert(poly_ui_control(view,i)==0xffffffffu);
    /* Analog TRIG bank [1,3] extends only for masters 9/10. */
    pages[0]=1;tracked=8;poly_ui_sync(view);pages=*(T **)(view+0x7c);
    assert(*(T **)(view+0x80)==pages+3);*(T *)(view+0x90)=2;
    evt[1]=0;evt[2]=1;assert(poly_ui_encoder(view,evt));
    assert(poly_seq_config.states[8]==3 && poly_ui_control(view,4)==0x14);
    evt[1]=1;assert(poly_ui_encoder(view,evt));assert(poly_seq_config.states[8]==4);
    assert(poly_ui_encoder(view,evt) && poly_seq_config.states[8]==4);
    for(tracked=9;tracked<=10;tracked++) {
        T result=0,args[1]={(T)view};
        assert(poly_ui_slave(view) && poly_native(ST_NATIVE_PAGE_GUARD,args,&result) && result==1);
        labels[0]=0;assert(poly_ui_draw(view,0) && strstr(labels,"SLAVE MODE;"));
        args[0]=tracked;result=99;assert(poly_native(ST_NATIVE_SOUND_ALLOWED,args,&result) && !result);
        result=99;assert(poly_native(ST_NATIVE_MACHINE_ALLOWED,args,&result) && !result);
    }
    tracked=8;poly_ui_sync(view);*(T *)(view+0x90)=2;evt[1]=0;evt[2]=(T)-1;assert(poly_ui_encoder(view,evt));
    tracked=9;evt[2]=1;assert(poly_ui_encoder(view,evt));
    assert(poly_seq_config.states[9]==3);evt[1]=1;assert(poly_ui_encoder(view,evt) && poly_seq_config.states[9]==3);
    for(tracked=10;tracked<12;tracked++) {
        poly_ui_sync(view);assert(!poly_ui_third(view) && *(T **)(view+0x80)==pages+2);
    }
    tracked=8;pages[0]=26;poly_ui_sync(view);assert(*(T **)(view+0x80)==pages+2);
    free(pages);
}
static void recording_and_publication(void)
{
    T vt[16]={0},notes[4]={60,64,67,72},i,t,j,seed=0x12345678u;
    B states[12]={5,0,0,0,5,0,0,0},owners[12];
    B before[0x754+8*0x7c],expected[sizeof before],working[sizeof before];
    reset();assert(poly_record_write(0,1,4));
    *(T *)0x43bd7424u=1;*(T *)0x444e13ecu=0x46000100u;*(B *)(0x46000100u+0x169)=1;
    *(T *)0x444e13f4u=0x1234u;vt[0x28/4]=(T)rec_model_mock;*(T **)(PATTERN+0x70)=vt;
    memset(rec_model,255,sizeof rec_model);
    for(i=0;i<4;i++)live(notes[i],1,2);
    assert(rec_chords[0].dirty && rec_chords[0].count==4);
    rec_model[0x2c0]=72;poly_rec_ui();
    assert(!rec_chords[0].dirty && rec_model[0x2c0]==60 && rec_model[0x300]==68 && rec_model[0x340]==71 && rec_model[0x380]==76);
    assert(SEQ[0x2c0]==60 && SEQ[0x300]==68 && SEQ[0x340]==71 && SEQ[0x380]==76);drain();
    /* Scalar publication never overwrites holes or the saved source Sounds. */
    assert(poly_resolve(states,owners));
    for(i=0;i<sizeof before;i++){seed=seed*1664525u+1013904223u;before[i]=(B)(seed>>24);}
    memcpy(expected,before,sizeof before);memcpy(working,before,sizeof before);
    for(t=1;t<8;t++)if(owners[t]<t){
        T owner=owners[t],d=0x754+t*0x7c,s=0x754+owner*0x7c;
        memcpy(expected+2*t,before+2*owner,2);
        memcpy(expected+0x1c+t*0x8e,before+0x1c+owner*0x8e,142);
        memcpy(expected+d,before+s,100);expected[d+100]=before[s+100];
        for(j=104;j<123;j++)expected[d+j]=before[s+j];
    }
    poly_publish_end=8;poly_publish(working,owners);assert(!memcmp(working,expected,sizeof working));
    assert(working[0x754+0x7c+101]==before[0x754+0x7c+101]);
}
static void inactive_native(void)
{
    T result=0xfaceu,args[6]={0,(T)SEQ,(T)KIT,0,0,0},before,commands,i,event[20]={1,1,0,1};B pq[7];
    reset();projections=0;before=trig_calls;poly_seq_config.pending[0].event=123;
    assert(!poly_native(ST_NATIVE_TRIG,args,&result) && result==0xfaceu && trig_calls==before);
    assert(!poly_seq_config.pending[0].event);
    args[0]=(T)event;
    assert(!poly_native(ST_NATIVE_ENQUEUE,args,&result) && !queued);
    assert(!poly_native(ST_NATIVE_PUBLISH,args,&result) && !projections);
    /* Repeated OFF frames retain the bounded cache check, without commits,
     * queue commands, event allocation or working projections. */
    before=poly_seq_config.generation;commands=immediate_calls;
    for(i=0;i<1500;i++){poly_frame();assert(!poly_native(ST_NATIVE_PUBLISH,args,&result));}
    assert(poly_seq_config.generation==before && immediate_calls==commands && !allocations && !projections && !queued);
    /* A newly loaded PQ topology is recognized at the very first trig. */
    assert(poly_record_write(0,1,2));assert(poly_grouped());memcpy(pq,SEQ+0x21381,7);
    assert(poly_record_write(0,0,2));assert(!poly_grouped());
    memcpy(SEQ+0x21381,pq,7);args[0]=0;
    assert(poly_native(ST_NATIVE_TRIG,args,&result) && poly_grouped() && poly_owner_map[1]==0);
}
/* Host API 5: POLY narrows its native interest without groups, never dropping
 * IMMEDIATE (its private SYNC/CFG commands), and widens it with groups. */
static T interest_fn,interest_mask,interest_calls;
static void interest_mock(void (*fn)(void),unsigned int mask){interest_fn=(T)fn;interest_mask=mask;interest_calls++;}
static void page_view_mock(void (*fn)(void),unsigned int v){assert(fn==(void (*)(void))poly_native && v==0x401c97fc);}
static void native_interest_changes(void)
{
    static struct st_machine_host_api services5;
    const T always=POLY_NATIVE_ALWAYS,grouped=POLY_NATIVE_GROUPED;B pq[7];
    services5=services;services5.version=5;services5.native_interest=interest_mock;
    reset();poly_host=&services5;poly_interest=0;interest_calls=0;
    poly_native_interest();
    assert(interest_calls==1 && interest_fn==(T)poly_native && interest_mask==always);
    assert(always&(1u<<ST_NATIVE_IMMEDIATE) && !(always&(1u<<ST_NATIVE_TYPE)) && !(always&grouped));
    poly_native_interest();assert(interest_calls==1);                         /* unchanged: no call */
    assert(poly_record_write(0,1,2) && poly_grouped() && interest_mask==(always|grouped));
    assert(!(interest_mask&(1u<<ST_NATIVE_TYPE))); /* direct native type projection */
    assert(!(interest_mask&((1u<<ST_NATIVE_MODULATION)|(1u<<ST_NATIVE_LFO))));
    *(B *)0x80002f50=65;*(B *)0x80002f51=1;*(B *)0x80002f52=2;
    assert(poly_mode_write(0,1) && !(interest_mask&((1u<<ST_NATIVE_PUBLISH)|(1u<<ST_NATIVE_MODULATION)|(1u<<ST_NATIVE_LFO)|(1u<<ST_NATIVE_OWNERS))));
    assert(poly_mode_write(0,0) && (interest_mask&(1u<<ST_NATIVE_PUBLISH)));
    memcpy(pq,SEQ+0x21381,7);
    assert(poly_record_write(0,0,2) && !poly_grouped() && interest_mask==always);
    assert(interest_calls==5);
    poly_host=&services;poly_interest=0;
}
/* Analog topologies and all event routes use the same production allocator. */
static void analog(void)
{
    B states[12]={0},owners[12],raw[7],saved[7],copy[256],decoded[12];
    T t,v,i,result,args[6],event[20],time,commands,before;
    assert(poly_max_voices(8)==3 && poly_max_voices(9)==2 && !poly_max_voices(10) && !poly_max_voices(11));
    for(t=8;t<10;t++)for(v=2;v<=poly_max_voices(t);v++) {
        memset(states,0,sizeof states);states[t]=(B)(v+1);
        assert(poly_resolve(states,owners));
        for(i=1;i<v;i++)assert(owners[t+i]==t && poly_owner(owners,t+i)==t);
        record_encode(states,raw);assert(raw[1]==(t==9?0x59:v==2?0x5c:0x5e) && valid(raw));
        record_states(raw,decoded);assert(!memcmp(states,decoded,12));
        memcpy(copy+0x21,raw,7);poly_header_load(header,copy);assert(!memcmp(header+0x21,raw,7));
        header[0x27]^=1;assert(!valid(header+0x21));
        reset();*(B *)(0x80002f50u+t)=35;
        assert(poly_record_write(t,1,v));assert(poly_has_groups && poly_analog_active && poly_publish_end==t+v);
        assert(publishes==v-1);
        for(i=t+1;i<t+v;i++)assert(*(B *)(0x80002f50u+i)==35 && !poly_sound_write_allowed(i));
        /* Same on-disk bytes as old Synfact; first TRIG loads them. */
        memcpy(saved,SEQ+0x21381,7);assert(poly_record_write(t,0,v));
        memcpy(SEQ+0x21381,saved,7);memset(event,0,sizeof event);event[0]=1;
        assert(poly_trig(t,SEQ,(T)KIT,0,event,&time)==event);
        assert(poly_sequence_enqueue(event,time) && queued==v);
        for(i=0;i<v;i++)assert(queue[i][2]==t+i && queue[i][18]==t+i);drain();
        for(i=0;i<v;i++) {
            T e[20]={1,1,t,2,0,0,60+i};poly_test_origin=2;native_immediate(e);
        }
        assert(queued==v);for(i=0;i<v;i++)assert(queue[i][2]==t+i);drain();
        for(i=0;i<v;i++) {
            T e[20]={1,2,t,2,0,0,60+i};poly_test_origin=1;native_immediate(e);
        }
        assert(queued==v);drain();
        {T e[20]={1,1,t,2,0,0,60};poly_test_origin=0;native_immediate(e);assert(queued==v);drain();}
        /* Preview at a held step, then matching releases. */
        *(T *)0x402d575cu=0;{T e[20]={1,1,t,1,0,0,60};native_immediate(e);assert(queued==v);drain();
        e[1]=2;native_immediate(e);assert(queued==v);drain();}*(T *)0x402d575cu=0xffffffffu;
        /* First-step statics are filled from the master as well. */
        assert(poly_trig(t,SEQ,(T)KIT,0,(T *)PREP_EVENT(t),&time));
        assert(poly_trig(t+1,SEQ,(T)KIT,0,(T *)PREP_EVENT(t+1),&time));
        assert(PREP_EVENT(t+1)[2]==t+1 && PREP_EVENT(t+1)[6]==64);
        /* Frame and publication never read/repair machine changes. UI does. */
        before=publishes;commands=immediate_calls;*(B *)(0x80002f50u+t)=37;
        for(i=0;i<1500;i++)poly_frame();assert(publishes==before && immediate_calls==commands);
        poly_tick(0);assert(publishes==before+v-1 && immediate_calls==commands+1);
        poly_tick(0);assert(immediate_calls==commands+1);
        /* Suspension keeps the PQ and restores each slave's saved Sound. */
        for(i=t;i<t+v;i++) {
            *(unsigned short *)(KIT+0x1550)=(unsigned short)(1u<<i);
            assert(poly_sync() && !poly_grouped() && !poly_analog_active && !memcmp(saved,SEQ+0x21381,7));
            assert(!poly_record_write(t,1,v));
            *(unsigned short *)(KIT+0x1550)=0;assert(poly_sync() && poly_owner_map[t+1]==t);
        }
        assert(poly_record_write(t,0,v));assert(SEQ[0x21382]==0x51 && !poly_grouped());
    }
    for(t=8;t<10;t++) {
        T vt[16]={0};reset();rec_track=t;assert(poly_record_write(t,1,2));
        *(T *)0x43bd7424u=1;*(T *)0x444e13ecu=0x46000100u;*(B *)(0x46000100u+0x169)=1;
        *(T *)0x444e13f4u=0x1234u;vt[0x28/4]=(T)rec_model_mock;*(T **)(PATTERN+(t+1)*0x70)=vt;
        memset(rec_model,255,sizeof rec_model);
        for(i=0;i<2;i++){T e[20]={1,1,t,2,0,0,60+i*4};poly_test_origin=2;native_immediate(e);}
        assert(rec_chords[t].dirty && rec_chords[t].count==2);rec_model[0x2c0]=64;poly_rec_ui();
        assert(!rec_chords[t].dirty && rec_model[0x2c0]==60 && rec_model[0x300]==68);
        assert(SEQ[t*0x499+0x2c0]==60 && SEQ[t*0x499+0x300]==68);drain();
    }
    /* CRC-valid malformed flags must fail independently of the checksum. */
    for(i=0;i<8;i++) {
        T crc=65535,j,k;memset(raw,0,7);raw[0]=0x50;raw[1]=(B)(0x58+i);
        for(j=0;j<5;j++){crc^=(T)raw[j]<<8;for(k=0;k<8;k++)crc=((crc<<1)^((crc&32768)?0x1021:0))&65535;}
        raw[5]=(B)(crc>>8);raw[6]=(B)crc;
        assert(valid(raw)==(i==0 || i==1 || i==4 || i==6));
    }
    memset(states,0,12);states[8]=3;states[9]=3;assert(!poly_resolve(states,owners));
    states[8]=4;assert(!poly_resolve(states,owners));
    for(t=10;t<12;t++){memset(states,0,12);states[t]=3;assert(!poly_resolve(states,owners));}
    memset(states,0,12);states[7]=3;assert(!poly_resolve(states,owners));
    reset();assert(!poly_record_write(10,1,2) && !poly_record_write(11,1,2));
    assert(!poly_record_write(8,1,4) && !poly_record_write(9,1,3));
    assert(poly_record_write(8,1,3) && !poly_record_write(9,1,2));
    assert(poly_record_write(8,0,3) && poly_record_write(9,1,2) && !poly_record_write(8,1,2));
    reset();assert(poly_record_write(8,1,3));*(unsigned short *)(KIT+0x1550)=1u<<9;
    assert(poly_sync() && !poly_grouped());assert(poly_record_write(0,1,2));
    assert(SEQ[0x21382]==0x5e && valid(SEQ+0x21381));
    *(unsigned short *)(KIT+0x1550)=0;assert(poly_sync() && poly_owner_map[10]==8 && poly_owner_map[1]==0);
    /* 84 is valid; 85+ and negative chord notes are omitted. */
    reset();assert(poly_record_write(8,1,3));
    SEQ[8*0x499+0x3c5]=80;SEQ[8*0x499+0x3c6]=68;SEQ[8*0x499+0x3c7]=69;
    assert(poly_trig(8,SEQ,(T)KIT,0,event,&time) && poly_sequence_enqueue(event,time));
    assert(queued==2 && queue[0][6]==80 && queue[1][6]==84);drain();
    assert(poly_chord_note(SEQ,8,0,2,80)==-1);
    {T e[20]={1,1,8,2,0,0,80};poly_test_origin=0;native_immediate(e);assert(queued==2 && queue[1][6]==84);drain();}
    /* Analog TYPE never subscribes to digital per-track dispatch bits. */
    poly_native_interest();assert(!(poly_interest&0xff000000u));
    args[0]=0x80002f59u;assert(!poly_native(ST_NATIVE_TYPE,args,&result));
}

/* Distinct saved slave Sounds must survive, but never be chosen by a
 * retargeted keyboard/MIDI/chord/preview/first-step note. */
static void analog_event_sounds(void)
{
    T master,voices,k,pass,origin,note,kind,e[20],time,sound,explicit_sound=(T)KIT+0x1580;
    for(master=8;master<=9;master++)for(voices=2;voices<=poly_max_voices(master);voices++) {
        reset();sound=(T)KIT+0x2a+master*0x123;
        for(k=8;k<=10;k++)KIT[0x2a+k*0x123+0xa2]=(B)(8+k-8);
        assert(poly_record_write(master,1,voices));
        for(origin=1;origin<=2;origin++)for(pass=0;pass<3;pass++)for(k=0;k<voices;k++) {
            memset(e,0,sizeof e);e[0]=1;e[1]=1;e[2]=master;e[3]=2;e[6]=60+k;
            poly_test_origin=origin;native_immediate(e);
            assert(queued==1 && queue[0][2]==master+k && queue[0][6]==60+k);
            assert(queue[0][10]==(k?sound:0));drain();
            e[1]=2;e[2]=master;e[18]=master;e[10]=0;native_immediate(e);
            assert(queued==1 && queue[0][2]==master+k);drain();
        }
        for(kind=1;kind<=2;kind++) {
            memset(e,0,sizeof e);e[0]=1;e[1]=1;e[2]=master;e[3]=kind;e[6]=60;
            *(T *)0x402d575cu=0;poly_test_origin=0;native_immediate(e);
            assert(queued==voices);
            for(k=0;k<voices;k++)assert(queue[k][10]==(k?sound:0));drain();
        }
        /* Native sequencer mock also emits a null Sound, to cover fallback. */
        assert(poly_trig(master,SEQ,(T)KIT,0,e,&time) && poly_sequence_enqueue(e,time));
        for(k=0;k<voices;k++)assert(queue[k][10]==(k?sound:0));drain();
        assert(poly_trig(master,SEQ,(T)KIT,0,(T *)PREP_EVENT(master),&time));
        for(k=1;k<voices;k++)assert(PREP_EVENT(master+k)[10]==sound);
        /* Explicit/pool Sounds keep their native references; no forced kit fallback. */
        for(note=0;note<3;note++) {
            memset(e,0,sizeof e);e[10]=note==0?explicit_sound:0;e[11]=note==1?explicit_sound:0;
            poly_voice_target(e,master,1);
            assert(e[10]==(note==0?explicit_sound:note==1?0:sound));
            assert(e[11]==(note==1?explicit_sound:0));
        }
        for(k=8;k<=10;k++)assert(KIT[0x2a+k*0x123+0xa2]==8+k-8);
    }
    memset(e,0,sizeof e);poly_voice_target(e,0,1);assert(!e[10] && !e[11]);
}

int main(void)
{
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    _set_error_mode(_OUT_TO_STDERR);
    setbuf(stdout,0);
    void *ram=VirtualAlloc((void *)0x40000000u,0x08000000u,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    void *bus=VirtualAlloc((void *)0x80000000u,0x10000u,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(ram==(void *)0x40000000u && bus==(void *)0x80000000u);
    {struct st_machine_ui_api ui={6};struct st_smz_api api={3,&ui};
     struct st_machine_host_api h=services;
     ui.host=&services;assert(poly_initialize(&api));ui.version=7;assert(!poly_initialize(&api));
     h.version=8;h.native_interest=interest_mock;h.native_page_view=page_view_mock;
     ui.host=&h;assert(!poly_initialize(&api));}
    play_codec();play_modes();copy_on_change();topology();groups_and_notes();sequence_voices();ui();recording_and_publication();inactive_native();native_interest_changes();analog();analog_event_sounds();
    puts("PASS: analog event/Sound/UI regressions and 110592 mixed topology/mode/record cases; 108 POLY topologies, 2/3/4 voices, live/chord/sequence paths, stealing/off, rollback, CRC persistence, MIDI suspension, page/controls/slave guards, native interest by group state");
    return 0;
}
