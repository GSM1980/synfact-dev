/* Production recorder/capture/normalizer/editor/save jobs on a native host.
 * Native UI, RAM buses and sparse high-zone IO are explicit doubles; no target
 * firmware routines, CPU emulator, MIDI or physical device are executed. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <windows.h>
#include <syntakt/host.h>
typedef unsigned int T;typedef unsigned char B;
static void *alloc_mock(T);static void free_mock(void *);
static T push_mock(void *,void *,T);static void drop_mock(void *),close_mock(void *),dirty_mock(void *),opaque_mock(void *,T);
static void ctor_mock(void *,const char *),dtor_mock(void *),cleanup_mock(void *);
static T manager_key_mock(void *,const void *),name_key_mock(void *,const void *);
static void *qstr_mock(void *,const char *,void *);static void qdrop_mock(void *);
static void fill_mock(void *,int,int,int,int,T),pixel_mock(void *,int,int,T);
static void text_mock(void *,void *,int,int,T,const char *,...);
static void toggle_mock(void *,T,void *,int,int,T);
static B *brain_mock(void);static T machine_mock(void *,T,T,T,T);
static B *model_mock(void *,T);static void parameter_mock(void *,T,T,T,T,T);
static T availability_mock(void *,T);static void track_ctor_mock(void *,void *,T,void *);
static void name_ctor_mock(void *,void *,void *,T,T,T,void *);
static T native_table[19],tempo_value=14400,seq_state=2,seq_stops;
static void seq_stop_mock(void);
#define REC_NATIVE
#define MALLOC alloc_mock
#define FREE free_mock
static int read_mock(T,T,void *),write_mock(T,T,const void *);static T sectors_mock(void);
#define MMC_READ read_mock
#define MMC_WRITE write_mock
#define CARD_SECTORS sectors_mock()
#define UI_PUSH push_mock
#define UI_DROP drop_mock
#define UI_CLOSE close_mock
#define UI_DIRTY dirty_mock
#define UI_OPAQUE opaque_mock
#define QSTR_INIT qstr_mock
#define WAVE_FILL fill_mock
#define WAVE_PIXEL pixel_mock
#define WAVE_TEXT text_mock
#define MAIN_BUS ((int *)0x8000dfd0u)
#define REC_VIEW_DTOR dtor_mock
#define REC_VIEW_CTOR ctor_mock
#define REC_VIEW_TABLE native_table
#define REC_KEY_MANAGER manager_key_mock
#define REC_MANAGER_CLEANUP cleanup_mock
#define REC_NAME_KEY name_key_mock
#define REC_TOGGLE toggle_mock
#define REC_BPM tempo_value
#define REC_SEQ_STATE seq_state
#define REC_SEQ_STOP seq_stop_mock
#define REC_BRAIN brain_mock
#define REC_SET_MACHINE machine_mock
#define REC_TRACK_MODEL model_mock
#define REC_SET_PARAM parameter_mock
#define REC_AVAILABILITY availability_mock
#define REC_TRACK_CTOR track_ctor_mock
#define REC_QSTR_DTOR qdrop_mock
#define REC_NAME_CTOR name_ctor_mock
#include "../mods/recorder-st/main.c"

static B app[1024],nodes[4][16],base_view[80],browser[80],pixels[128*64];
static B *name_view_mock,*track_view_mock,*closed;
static struct fn_capture name_callback,track_callback;
static T irq_depth,io_reads,io_writes,alloc_calls,dirty_calls,disposes,manager_keys;
static T assigned_track,assigned_slot,machine_calls,param_calls,preview_stops,apply_calls;
static int in_irq,io_fail,verify_fail,pool_fail;
static u16 record[128];static void *allocs[64];
static B *disk;static const T disk_bytes=16u*ST_HZ_WINDOW;
static B *manager(void){return app+0x40;}
static void top(B *view,B *opaque)
{*(void **)(nodes[0]+8)=opaque;*(void **)(nodes[1]+8)=view;*(void **)(manager()+0x1c)=nodes[0];*(void **)(manager()+0x18)=nodes[1];}
static void *alloc_mock(T n)
{
    T i;void *p;assert(!in_irq && !irq_depth && n && n<=0x800000u);
    for(i=0;i<64 && allocs[i];i++){}assert(i<64);
    p=VirtualAlloc((void *)(0x47000000u+i*0x800000u),n,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(p);allocs[i]=p;alloc_calls++;return p;
}
static void free_mock(void *p)
{T i;assert(!in_irq);for(i=0;i<64 && allocs[i]!=p;i++){}assert(i<64 && p);assert(VirtualFree(p,0,MEM_RELEASE));allocs[i]=0;}
static T sectors_mock(void){return (ST_HZ_FIRST+16u)<<10;}
static int read_mock(T lba,T n,void *p)
{
    T off;assert(!in_irq && !irq_depth && lba>=(ST_HZ_FIRST<<10) && n<=4096 && !(n&511));
    off=(lba-(ST_HZ_FIRST<<10))*512u;assert(off<=disk_bytes && n<=disk_bytes-off);io_reads++;
    if(io_fail)return -1;memcpy(p,disk+off,n);if(verify_fail && sam_save_phase==2)((B *)p)[n-1]^=1;return 0;
}
static int write_mock(T lba,T n,const void *p)
{
    T off;assert(!in_irq && !irq_depth && lba>=(ST_HZ_FIRST<<10) && n<=4096 && !(n&511));
    off=(lba-(ST_HZ_FIRST<<10))*512u;assert(off<=disk_bytes && n<=disk_bytes-off);io_writes++;
    if(io_fail)return -1;memcpy(disk+off,p,n);return 0;
}
static void pool_read_mock(const B *p,u16 *out){(void)p;memcpy(out,record,sizeof(record));}
static void pool_write_mock(B *p,const u16 *in){(void)p;memcpy(record,in,sizeof(record));}
static int ready_mock(void){return 1;}
static int apply_mock(const u16 *before)
{apply_calls++;if(pool_fail){memcpy(record,before,sizeof(record));return -1;}return 1;}
static void stop_mock(void){preview_stops++;}
static void seq_stop_mock(void){assert(!in_irq && !irq_depth && seq_state==1);seq_state=2;seq_stops++;}
extern T core_crc_update(T,const B *,T);
static int view_event_mock(T event,void *v,void *b,T id)
{assert(event==ST_EV_DRAW);recorder_draw(v,b,id);return 0;}
static const struct st_storage_io storage={read_mock,write_mock,sectors_mock,alloc_mock,free_mock};
static const struct st_machine_host_api services={2,&storage,core_hzfs_open,core_hzfs_close,
    core_crc_update,pool_read_mock,pool_write_mock,ready_mock,apply_mock,stop_mock,view_event_mock};
T recorder_irq_lock(void){assert(!in_irq && !irq_depth);irq_depth=1;return 0x2000;}
void recorder_irq_unlock(T sr){assert(irq_depth==1 && sr==0x2000);irq_depth=0;}
static T push_mock(void *m,void *pair,T flag)
{
    B *v=*(B **)pair;assert(m==manager() && !flag);
    top(v,v==track_view_mock?sam_view:v);return 1;
}
static void drop_mock(void *p){(void)p;}
static void close_mock(void *p){closed=p;((B *)p)[0x30]=1;}
static void cleanup_mock(void *p)
{assert(p==manager());if(closed==browser)top(base_view,base_view);else top(sam_view,sam_view);closed=0;}
static void dirty_mock(void *p){assert(!in_irq && p);dirty_calls++;}
static void opaque_mock(void *p,T on){assert(p && on==1);}
static void ctor_mock(void *v,const char *s){assert(!strcmp(s,"RECORDER"));*(T **)v=native_table+2;}
static void dtor_mock(void *p){assert(p);disposes++;}
static T manager_key_mock(void *m,const void *ev){assert(m==manager() && !(be32((B *)ev+16)&1));manager_keys++;return 1;}
static T name_key_mock(void *p,const void *ev){assert(p==sam_name_view);(void)ev;return 1;}
static void *qstr_mock(void *p,const char *s,void *alloc){*(const char **)p=s;(void)alloc;return p;}
static void qdrop_mock(void *p){(void)p;}
static void pixel_mock(void *ctx,int x,int y,T on)
{B *p=ctx;if(x>=0 && x<128 && y>=0 && y<64)p[128*y+x]=!!on;}
static void fill_mock(void *ctx,int x,int y,int xx,int yy,T on)
{assert(!in_irq && xx>=x && yy>=y);for(int a=x;a<=xx;a++)for(int b=y;b<=yy;b++)pixel_mock(ctx,a,b,on);}
static void text_mock(void *ctx,void *f,int x,int y,T flags,const char *format,...)
{(void)ctx;(void)f;(void)x;(void)y;(void)flags;(void)format;assert(!in_irq);}
static void toggle_mock(void *a,T value,void *ctx,int x,int y,T flag)
{assert(!a && (value==0 || value==256) && !flag);fill_mock(ctx,x,y,x+17,y+15,!!value);}
static B *brain_mock(void){return app;}
static B sound[0xa4];static T model_table[16];static T *model=model_table;
static B *sound_mock(void *m){assert(m==&model);return sound;}
static T machine_mock(void *kit,T track,T type,T a,T b)
{assert(kit==app+0x130 && track<8 && type==66 && a==1 && b==1);sound[0xa2]=66;assigned_track=track;machine_calls++;return 1;}
static B *model_mock(void *kit,T track){assert(kit==app+0x130 && track==assigned_track);model_table[0x28/4]=(T)sound_mock;return (B *)&model;}
static void parameter_mock(void *p,T id,T value,T track,T a,T b)
{assert(((T *)p)[4]==(T)&model && id==0x23c && track==assigned_track && !a && b==1);assigned_slot=value>>8;param_calls++;}
static T availability_mock(void *p,T machine){assert(!p && machine==66);return 0xff;}
static void track_ctor_mock(void *v,void *fn,T mask,void *title)
{assert(mask==0xff && title);track_view_mock=v;track_callback=*(struct fn_capture *)fn;}
static void name_ctor_mock(void *v,void *title,void *name,T a,T max,T b,void *fn)
{assert(title && name && !a && max==16 && !b);name_view_mock=v;name_callback=*(struct fn_capture *)fn;}
static T key_event(T key,T flags)
{B ev[24]={0};put32(ev+12,key);put32(ev+16,flags);return recorder_key(app,ev);}
static T encoder_event(T id,int delta)
{B ev[24]={0};put32(ev+12,id);put32(ev+16,(T)delta);return recorder_encoder(app,ev);}
static void audio(void){in_irq=1;recorder_audio();in_irq=0;}
static void gesture(void)
{
    top(base_view,base_view);sam_clock=0xfffff000u;
    assert(!key_event(5,3) && !sam_sound_pending); /* FUNC+SOUND stays native */
    assert(!key_event(5,1) && !key_event(5,16));sam_clock+=16800u;
    assert(key_event(5,1) && sam_sound_release); /* inclusive window, clock wrap */
    sam_sound_pending=sam_sound_release=sam_sound_down=0;
    sam_clock=100;assert(!key_event(5,1) && !key_event(5,16));sam_clock+=16801;
    assert(!key_event(5,1) && !sam_sound_release && !key_event(5,16));
    assert(!key_event(10,1) && !sam_sound_pending); /* another key cancels gesture */
    top(base_view,base_view);sam_clock=48000;
    assert(!key_event(5,1));top(browser,browser);assert(!key_event(5,16));
    sam_clock+=16000;assert(key_event(5,1) && !sam_view);assert(key_event(5,9) && !sam_view);
    assert(key_event(5,16) && sam_view && sam_foreground() && manager_keys==1 && sam_report[0]==1);
    assert(!sam_sound_pending && !sam_sound_release && sam_pcm && !irq_depth);
    assert(sam_iface_table[2+2]==native_table[2+2] && sam_iface_table[2+11]==native_table[2+11]);
    assert(sam_iface_table[2+4]==native_table[2+4]); /* shared complete-view DRAW */
    assert(key_event(5,1) && key_event(5,16));
    assert(!key_event(10,1)); /* PLAY retains native transport */
    sam_state=SAM_EMPTY;sam_source=5;sam_post=0;sam_mon=0;
    assert(encoder_event(7,1) && sam_source==6);assert(encoder_event(7,-999) && !sam_source);
    assert(encoder_event(5,-1) && sam_rlen==7);assert(encoder_event(5,99) && sam_rlen==8);
    assert(encoder_event(8,1) && sam_mon==1);assert(encoder_event(1,1) && sam_post==1);
    top(base_view,sam_view);T old=sam_thr;assert(!encoder_event(6,1) && sam_thr==old);top(sam_view,sam_view);
    sam_state=SAM_ARMED;old=sam_rlen;assert(encoder_event(5,-1) && sam_rlen==old);
    assert(encoder_event(8,-1) && !sam_mon);sam_state=SAM_EMPTY;
}
static void buses(void)
{
    int *in=(int *)0x80003b50u,*tracks=(int *)0x80003d50u,*analog=(int *)0x80003950u,*usb=(int *)0x80004760u;
    int *delay=(int *)0x8000dcd0u,*reverb=(int *)0x8000ddd0u;
    for(T i=0;i<32;i++){
        in[i]=(int)(i-16)*65536;in[32+i]=(31-(int)i)*65536;
        for(T t=0;t<8;t++)tracks[t*32+i]=(int)(t*123+i-400)*65536;
        for(T t=0;t<4;t++)analog[t*32+i]=(int)(t*173+i-300)*65536;
        usb[20*i]=(int)(i-20)*65536;usb[20*i+1]=(15-(int)i)*65536;
        delay[2*i]=(int)i*32768;delay[2*i+1]=-(int)i*32768;
        reverb[2*i]=(int)(31-i)*32768;reverb[2*i+1]=-(int)(31-i)*32768;
    }
    for(T b=0;b<4;b++){
        T addresses[]={0x8000e138u,0x8000e110u,0x8000e188u,0x8000e160u};int *g=(int *)addresses[b];
        for(T i=0;i<10;i++)g[i]=(int)(i+b+1)*1024;
    }
}
static void prepare(T source,T post,T state,T frames,T limit,T threshold)
{
    sam_clock=4096;sam_ret_clock=4096;sam_enabled=1;sam_source=sam_take_source=source;
    sam_post=sam_take_post=post;sam_state=state;sam_frames=frames;sam_limit=limit;
    sam_peak=0;sam_last_source=18;sam_trigger=threshold;sam_pcm_peak=0;sam_mon=1;
    sam_play_length=0;headroom_meter_phase=24;headroom_meter_signature=source|(post<<8);
    for(T b=0;b<2;b++)for(T i=0;i<64;i++)sam_ret[b][i]=(int)(i+b)-32;
    memset(MAIN_BUS,0,64*sizeof(int));memset(sam_pcm,0,128);
}
static void capture(void)
{
    short expected[64];int mix[64];T frame,state,peak,clock,pcm_peak,reads=io_reads,writes=io_writes,alloc=alloc_calls;
    buses();
    for(T source=0;source<18;source++)for(T post=0;post<2;post++)for(T mode=1;mode<=2;mode++){
        prepare(source,post,mode,0,27,0x70000);in_irq=1;sampler_base_tick();in_irq=0;
        memcpy(expected,sam_pcm,sizeof(expected));memcpy(mix,MAIN_BUS,sizeof(mix));
        frame=sam_frames;state=sam_state;peak=sam_peak;clock=sam_clock;pcm_peak=sam_pcm_peak;
        prepare(source,post,mode,0,27,0x70000);in_irq=1;headroom_tick();in_irq=0;
        assert(!memcmp(expected,sam_pcm,sizeof(expected)) && !memcmp(mix,MAIN_BUS,sizeof(mix)));
        assert(sam_frames==frame && sam_state==state && sam_peak==peak && sam_clock==clock && sam_pcm_peak==pcm_peak);
    }
    prepare(5,0,SAM_RECORDING,0,32,0);audio();assert(sam_frames==32 && sam_state==SAM_NORMALIZING);
    prepare(0,0,SAM_RECORDING,SAM_MAX_FRAMES-4,SAM_MAX_FRAMES,0);audio();assert(sam_frames==SAM_MAX_FRAMES && sam_state==SAM_NORMALIZING);
    prepare(0,0,SAM_EMPTY,0,32,0);sam_mon=0;headroom_meter_phase=23;T skip=headroom_ui_meter_skipped_blocks;
    audio();audio();assert(headroom_ui_meter_skipped_blocks==skip+1); /* prime, then sampled */
    sam_state=SAM_READY;sam_frames=1;T count=sam_report[1];audio();assert(sam_report[1]==count);
    sam_state=SAM_NORMALIZING;count=sam_report[1];audio();assert(sam_report[1]==count);
    assert(io_reads==reads && io_writes==writes && alloc_calls==alloc);
}
static void normalize(void)
{
    T sizes[]={1,2,125,126,127,255,256,257,1024,3071,3072,3073,4096,10000};
    short expected[10000],low[126],high[126];
    for(T mode=0;mode<4;mode++)for(T k=0;k<sizeof(sizes)/sizeof(sizes[0]);k++){
        T n=sizes[k],peak=0,gain,calls=0;
        for(T i=0;i<n;i++){
            int v=mode==0?0:mode==1?(int)(i%3)-1:mode==2?(i&1?-32768:32767):(int)((i*617u)%60001u)-30000;
            sam_pcm[i]=(short)v;T m=v<0?0u-(T)v:(T)v;if(m>peak)peak=m;
        }
        gain=peak?(29204u*65536u)/peak:65536u;memset(low,0,sizeof(low));memset(high,0,sizeof(high));
        for(T i=0;i<n;i++){
            expected[i]=(short)(((T)(int)sam_pcm[i]*gain)>>16);T bin=i*126u/n;
            if(expected[i]<low[bin])low[bin]=expected[i];if(expected[i]>high[bin])high[bin]=expected[i];
        }
        sam_frames=n;sam_state=SAM_NORMALIZING;sam_pcm_peak=peak;sam_norm_init=0;sam_norm_pos=0;sam_norm_shown=0;sam_enabled=1;
        normalize_fast();assert(!sam_norm_pos);sam_norm_shown=1;
        while(sam_state==SAM_NORMALIZING){T before=sam_norm_pos;normalize_fast();assert(sam_norm_pos>before && sam_norm_pos-before<=3072);assert(++calls<10);}
        assert(sam_state==SAM_READY && sam_wave_ready && sam_trim_start==0 && sam_trim_end==n);
        assert(!memcmp(expected,sam_pcm,2*n) && !memcmp(low,sam_wave_low,sizeof(low)) && !memcmp(high,sam_wave_high,sizeof(high)));
        for(T i=0;i<n;i++)assert(sam_pcm[i]<=29204 && sam_pcm[i]>=-29204);
    }
    for(T r=0;r<8;r++){sam_rlen=r;tempo_value=14400;assert(sam_record_limit()==6000u*(1u<<r));}
    sam_rlen=8;assert(sam_record_limit()==SAM_MAX_FRAMES);
    sam_rlen=3;tempo_value=14399;assert(sam_record_limit()==(T)((86400000ull*8u)/14399));tempo_value=14400;
    /* Maximum 60-second take, using the real tick worker. Each callback is
     * hard-bounded to 32 original 3072-frame quotas; native IRQ clock also
     * enforces the soft four-ms budget on hardware. Closed UI does no work. */
    for(T i=0;i<SAM_MAX_FRAMES;i++)sam_pcm[i]=(short)(i&1?-16000:16000);
    sam_frames=SAM_MAX_FRAMES;sam_state=SAM_NORMALIZING;sam_pcm_peak=16000;
    sam_norm_init=sam_norm_pos=0;sam_norm_shown=1;T ticks=0,gain=(29204u*65536u)/16000u;
    while(sam_state==SAM_NORMALIZING){T before=sam_norm_pos;recorder_ui_tick(manager());assert(sam_norm_pos>before && sam_norm_pos-before<=32u*3072u);assert(++ticks<=30);}
    for(T i=0;i<SAM_MAX_FRAMES;i++)assert(sam_pcm[i]==(short)(((T)(i&1?-16000:16000)*gain)>>16));
    assert(sam_detail_frames==SAM_MAX_FRAMES && sam_trim_end==SAM_MAX_FRAMES);
}
/* A take that fills its steps stops a playing sequencer once, before the
 * normalization; manual ends, unlimited takes and a stopped sequencer do not. */
static void step_stop(void)
{
    top(sam_view,sam_view);sam_clear();sam_rlen=2;tempo_value=14400;seq_state=1;seq_stops=0;
    sam_start(0);assert(sam_seq_stop && sam_state==SAM_RECORDING && sam_limit==24000);
    recorder_ui_tick(manager());assert(!seq_stops && sam_seq_stop);
    sam_frames=sam_limit;sam_state=SAM_NORMALIZING;recorder_ui_tick(manager());
    assert(seq_stops==1 && seq_state==2 && !sam_seq_stop);
    seq_state=1;recorder_ui_tick(manager());assert(seq_stops==1);
    sam_clear();sam_start(0);sam_frames=100;assert(key_event(12,1) && key_event(12,16));
    assert(sam_state==SAM_NORMALIZING);recorder_ui_tick(manager());assert(seq_stops==1 && !sam_seq_stop);
    sam_clear();sam_rlen=8;sam_start(0);assert(!sam_seq_stop);
    sam_frames=sam_limit;sam_state=SAM_NORMALIZING;recorder_ui_tick(manager());assert(seq_stops==1);
    sam_clear();sam_rlen=2;seq_state=2;sam_start(0);
    sam_frames=sam_limit;sam_state=SAM_NORMALIZING;recorder_ui_tick(manager());assert(seq_stops==1);
    /* Audition on the preview bus keeps the previous digital level: x5/4. */
    sam_state=SAM_READY;sam_frames=32;sam_trim_valid=0;sam_pcm[0]=1000;
    memset(MAIN_BUS,0,64*sizeof(int));sam_audition();audio();assert(MAIN_BUS[0]==-1000*2560 && MAIN_BUS[1]==-1000*2560);
    sam_clear();sam_rlen=8;
}
static void editor_save(void)
{
    T n=1000;sam_frames=n;sam_state=SAM_READY;sam_wave_ready=1;sam_trim_valid=0;sam_trim_init();
    for(T i=0;i<n;i++)sam_pcm[i]=(short)((int)i*30-15000);
    assert(encoder_event(1,5) && encoder_event(3,-5));assert(sam_trim_start>0 && sam_trim_end<n && sam_trim_start<sam_trim_end);
    T first=sam_trim_start,last=sam_trim_end;assert(encoder_event(2,8) && encoder_event(2,8));assert(sam_zoom[0]==16);
    sam_audition();assert(sam_play_pos==first && sam_play_length==last);
    sam_state=SAM_READY;sam_enabled=1;T p=sam_play_pos;audio();assert(sam_play_pos==p+32);
    memset(pixels,0,sizeof(pixels));recorder_draw(sam_view,pixels,0);assert(sam_report[2] && pixels[16*128]);
    assert(key_event(12,1) && sam_trimmed);assert(key_event(12,16));
    assert(key_event(12,1) && sam_name_wait);assert(key_event(12,16) && sam_name_view==name_view_mock);
    B *name=(B *)"REC TEST";sam_name_done(&name_callback,&name,0);top(sam_view,sam_view);sam_yes_down=0;
    recorder_ui_tick(manager());assert(sam_assign_phase==SAM_ASSIGN_SHOWN && track_view_mock && !sam_tail_is_recorder());
    assert(!key_event(24,1)); /* native track popup owns the event */
    sam_assign_done(&track_callback,3);top(sam_view,sam_view);
    for(T i=0;i<128;i++)record[i]=i<64?(u16)(i+1):0;
    for(T i=0;i<50 && (sam_assign_phase || sam_save_phase);i++)recorder_ui_tick(manager());
    assert(!sam_save_phase && !sam_assign_phase && sam_saved_id==1 && record[64]==0x8001 && assigned_slot==64 && assigned_track==3);
    assert(machine_calls==1 && param_calls==1 && apply_calls==1);
    for(T i=0;i<64;i++)assert(record[i]==i+1);
    struct st_hzfs fs;struct st_sample_entry e;
    assert(core_hzfs_open(&fs,&storage)==1 && core_hzfs_count(&fs)==1 && core_hzfs_entry(&fs,0,&e)==1);
    assert(e.frames==last-first && !strcmp(e.name,"REC TEST"));core_hzfs_close(&fs);
    B *body=disk+(e.first-ST_HZ_FIRST)*ST_HZ_WINDOW;
    assert(be32(body)==0x53414d50u && be32(body+8)==last-first);
    for(T i=0;i<last-first;i++)assert((short)((T)body[64+2*i]<<8|body[65+2*i])==sam_pcm[first+i]);
    assert(~core_crc_update(0,body,e.bytes)==e.crc);
    /* Failure paths preserve the RAM take and stock slots, never assign. */
    sam_saved_id=0;sam_trimmed=1;text_set(sam_save_name,"BAD WRITE",16);sam_save_start=first;sam_save_frames=last-first;
    sam_save_phase=1;sam_save_step();assert(sam_save_phase==2);
    assert(!key_event(11,1) && !key_event(11,16) && key_event(11,3) && sam_save_phase==2); /* native STOP */
    verify_fail=1;sam_save_step();verify_fail=0;
    assert(!sam_save_phase && sam_trimmed && sam_pcm && sam_frames==n && machine_calls==1);
    assert(core_hzfs_open(&fs,&storage)==1 && core_hzfs_count(&fs)==1);core_hzfs_close(&fs);
    sam_saved_id=1;for(T i=64;i<128;i++)record[i]=0x8002;sam_save_add();assert(!strcmp(sam_save_message,"SAVED / POOL FULL"));
    for(T i=0;i<64;i++)assert(record[i]==i+1);
    record[64]=0;pool_fail=1;sam_save_add();pool_fail=0;assert(!record[64] && !strcmp(sam_save_message,"SAVED / AUDIO ERROR"));
    T old_writes=io_writes;assert(sam_write(HZ_INDEX_A-1,512,body)<0 && io_writes==old_writes);
    sam_trimmed=0;assert(key_event(13,1) && key_event(13,16) && sam_view[0x30]);
    u8 *view=sam_view;sampler_dispose(view);assert(!sam_view && !sam_enabled && sam_pcm);recorder_ui_tick(manager());
}
static void storage_arbitration(void)
{
    T info[32]={0},out[4]={0};B header[32]={0},status[12]={1};T reads=io_reads,writes=io_writes;
    info[3]=0x02400000u;assert(!recorder_fs_write(0,info,out) && !recorder_transfer_active);
    info[3]=0x30000000u;assert(!recorder_fs_write(0,info,out) && recorder_transfer_active);
    assert(!recorder_save_acquire() && !recorder_save_lease);
    sam_save_phase=1;sam_save_step();assert(!sam_save_phase && !strcmp(sam_save_message,"TRANSFER BUSY"));
    assert(io_reads==reads && io_writes==writes);
    put32(header+13,8);put32(header+21,65);recorder_fs_written(0,header,status);assert(recorder_transfer_active);
    put32(header+21,ST_HZ_RAW_FIRST+ST_HZ_FIRST);status[0]=0;recorder_fs_written(0,header,status);assert(recorder_transfer_active);
    status[0]=1;recorder_fs_written(0,header,status);assert(!recorder_transfer_active);
    assert(recorder_save_acquire() && recorder_save_lease);
    assert(recorder_fs_write(0,info,out)==1 && !((B *)out)[0] && out[1] && !recorder_transfer_active);
    recorder_save_release();assert(!recorder_save_lease && !irq_depth);
    /* Closing a recorder in the middle of a chunked save releases its lease
     * and scratch, keeps the take, and never assigns a partially saved file. */
    top(base_view,base_view);sampler_open(manager());sam_state=SAM_READY;sam_frames=10;
    sam_save_frames=10;sam_save_start=0;text_set(sam_save_name,"CANCELLED",16);
    sam_save_phase=1;sam_save_step();assert(sam_save_phase==2 && recorder_save_lease);
    u8 *v=sam_view;sampler_dispose(v);assert(!sam_save_phase && !recorder_save_lease && sam_pcm && sam_frames==10);
    assert(!sam_save_index && !sam_save_io && !sam_save_check);
    sam_state=SAM_NORMALIZING;sam_norm_pos=0;recorder_ui_tick(manager());assert(!sam_norm_pos);
}
int main(void)
{
    assert(VirtualAlloc((void *)0x80000000u,0x20000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    disk=calloc(1,disk_bytes);assert(disk);
    struct st_machine_ui_api ui={0};struct st_smz_api api={2,&ui};
    assert(initialize(&api));ui.version=5;ui.host=&services;assert(initialize(&api));ui.version=6;assert(!initialize(&api));
    assert(recorder_module.count==7 && core_hzfs_ensure(&storage)==1);
    gesture();capture();normalize();step_stop();editor_save();storage_arbitration();assert(!irq_depth);
    for(T i=0;i<64;i++)if(allocs[i])free_mock(allocs[i]);free(disk);VirtualFree((void *)0x80000000u,0,MEM_RELEASE);
    puts("PASS: SOUND double tap/native release; shared key/encoder/draw/tick/mix; 72 capture differentials; 56 normalization cases; trim/zoom; step-length sequencer STOP; preview bus level; naming/assignment; real HZFS save/CRC/verify/errors; slots 1-64 protected; no IRQ allocation/IO");
    return 0;
}
