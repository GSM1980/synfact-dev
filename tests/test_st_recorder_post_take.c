/* Production post-take fast path, preview, core eligibility and PLAY policy.
 * Native output/EMAC is tested separately on analog-emu cfbench. */
#define main recorder_existing_suite_main
#include "test_st_recorder.c"
#undef main

static void seed_pcm(T channels)
{
    for(T i=0;i<64;i++)for(T c=0;c<channels;c++)
        sam_pcm[channels*i+c]=(short)(c?-(int)(2000+i):1000+i);
}
static void idle_take(T source,T channels,T state,T saving)
{
    short before[128];T clock,ticks,returns,norm_pos,skips;
    prepare(source,1,state,64,128,0);sam_stereo=sam_take_stereo=channels==2;
    sam_save_phase=saving;seed_pcm(channels);memcpy(before,sam_pcm,128*sizeof(short));
    sam_norm_pos=17;clock=sam_clock;ticks=sam_report[1];returns=sam_ret_clock;norm_pos=sam_norm_pos;
    skips=state==SAM_NORMALIZING?headroom_normalizing_skips:headroom_ready_skips;
    audio();
    assert(sam_clock==clock+32 && sam_frames==64 && sam_state==state);
    assert(sam_report[1]==ticks && sam_ret_clock==returns && sam_norm_pos==norm_pos);
    assert(!memcmp(before,sam_pcm,128*sizeof(short)));
    for(T i=0;i<64;i++)assert(!MAIN_BUS[i]);
    assert((state==SAM_NORMALIZING?headroom_normalizing_skips:headroom_ready_skips)==skips+1);
    sam_save_phase=0;
}
static void audition_take(T channels)
{
    prepare(0,0,SAM_READY,64,128,0);sam_stereo=sam_take_stereo=channels==2;
    seed_pcm(channels);sam_trim_start=4;sam_trim_end=50;sam_trim_valid=1;
    sam_keys_down=0;T ticks=sam_report[1],clock=sam_clock,skips=headroom_ready_skips;
    assert(key_event(12,3) && key_event(12,16)); /* actual FUNC+YES handler */
    assert(sam_play_pos==4 && sam_play_length==50);
    audio();assert(sam_play_pos==36);
    for(T i=0;i<32;i++)for(T c=0;c<2;c++)
        assert(MAIN_BUS[2*i+c]==-(int)sam_pcm[channels*(i+4)+(channels==2?c:0)]*2560);
    memset(MAIN_BUS,0,64*sizeof(int));audio();
    assert(sam_play_pos==50 && !sam_play_length);
    for(T i=0;i<32;i++)for(T c=0;c<2;c++) {
        int expected=i<14?-(int)sam_pcm[channels*(i+36)+(channels==2?c:0)]*2560:0;
        assert(MAIN_BUS[2*i+c]==expected);
    }
    assert(sam_clock==clock+64 && sam_report[1]==ticks && headroom_ready_skips==skips+2);
}
static void idle_lifecycle(void)
{
    static B modal[80];struct st_machine_ui_api ui={6};struct st_smz_api api={2,&ui};
    ui.host=&services;core_audio_idle_reset();assert(!initialize(&api));
    assert(initialize(&api)); /* exclusive owner */
    top(sam_view,sam_view);sam_view[0x30]=0;sam_clear();
    assert(!core_audio_idle_begin());
    prepare(0,0,SAM_READY,64,128,0);sam_stereo=sam_take_stereo=0;seed_pcm(1);
    seq_state=1;assert(!core_audio_idle_begin());
    seq_state=2;assert(core_audio_idle_begin() && core_audio_idle_active);
    assert(key_event(10,1) && key_event(10,9) && key_event(10,16));
    assert(seq_state==2 && core_audio_idle_begin());
    sam_keys_down=0;sam_trim_valid=0;assert(key_event(12,3) && key_event(12,16));
    memset(MAIN_BUS,0,64*sizeof(int));T clock=sam_clock;
    core_audio_idle_owner->mix(0,0,0); /* output double + production clock/preview */
    assert(sam_clock==clock+32 && sam_play_pos==32 && MAIN_BUS[0]==-1000*2560);
    sam_play_length=0;sam_state=SAM_NORMALIZING;assert(core_audio_idle_begin());
    /* Naming is an owned opaque overlay; assignment is nonopaque. */
    sam_name_view=modal;top(modal,modal);assert(core_audio_idle_begin());
    assert(key_event(10,1) && key_event(10,16));
    sam_name_view=0;top(base_view,sam_view);sam_assign_phase=SAM_ASSIGN_SHOWN;
    assert(core_audio_idle_begin() && key_event(10,1) && key_event(10,16));
    sam_assign_phase=SAM_ASSIGN_IDLE;top(base_view,base_view);assert(!core_audio_idle_begin());
    top(sam_view,sam_view);assert(core_audio_idle_begin());
    sam_save_phase=1;assert(core_audio_idle_begin());sam_save_phase=0;
    assert(key_event(10,1));sam_view[0x30]=1;
    assert(!core_audio_idle_begin() && key_event(10,16)); /* consume orphan release */
    sam_view[0x30]=0;sam_clear();assert(!core_audio_idle_begin());
    sam_state=SAM_ARMED;assert(!core_audio_idle_begin());sam_state=SAM_RECORDING;
    assert(!core_audio_idle_begin());sam_clear();
    assert(!key_event(10,1) && !key_event(10,16)); /* normal transport before capture */
    core_audio_idle_reset();assert(!core_audio_idle_begin());
}
static void retained_take_reopen(void)
{
    struct st_machine_ui_api ui={6};struct st_smz_api api={2,&ui};ui.host=&services;
    core_audio_idle_reset();assert(!initialize(&api));
    sam_state=SAM_READY;sam_frames=64;sam_take_stereo=0;seed_pcm(1);
    sam_trim_valid=1;sam_trim_start=5;sam_trim_end=53;sam_saved_id=0;
    short first=sam_pcm[0];sampler_dispose(sam_view);top(base_view,base_view);
    assert(!core_audio_idle_begin());
    seq_state=1;T stops=seq_stops;sampler_open(manager());
    assert(seq_state==2 && seq_stops==stops+1 && core_audio_idle_begin());
    assert(sam_frames==64 && sam_state==SAM_READY && sam_pcm[0]==first);
    assert(sam_trim_start==5 && sam_trim_end==53 && !sam_saved_id);
    assert(key_event(10,1) && key_event(10,16) && core_audio_idle_begin());
    /* A subsequent external START is stopped by UI maintenance while owned. */
    seq_state=1;recorder_ui_tick(manager());assert(seq_state==2 && core_audio_idle_begin());
    sam_keys_down=0;sam_trimmed=0;
    assert(key_event(13,3) && key_event(13,18)); /* FUNC+NO: discard */
    assert(!sam_frames && sam_state==SAM_EMPTY && !core_audio_idle_begin());
    assert(!key_event(10,1) && !key_event(10,16));
    core_audio_idle_reset();
}
static void manual_stop_limits(void)
{
    struct st_machine_ui_api ui={6};struct st_smz_api api={2,&ui};ui.host=&services;
    core_audio_idle_reset();assert(!initialize(&api));
    top(sam_view,sam_view);sam_view[0x30]=0;
    /* STOP is dispatched before native UI routing. It must enter reduced
     * audio immediately, even for a partial take and before the next UI tick. */
    for(T stereo=0;stereo<2;stereo++)for(T length=0;length<=8;length++)
    for(T transport=0;transport<3;transport++)for(T func=0;func<2;func++) {
        sam_clear();sam_stereo=stereo;sam_source=5;sam_post=1;sam_rlen=length;
        sam_keys_down=0;seq_state=transport;sam_start(0);buses();audio();audio();
        assert(sam_frames==64 && sam_frames<sam_limit && sam_state==SAM_RECORDING);
        short pcm[128];memcpy(pcm,sam_pcm,(stereo?128:64)*sizeof(short));
        T stops=seq_stops,frames=sam_frames;
        assert(key_event(11,1|(func<<1))); /* owned STOP; no native propagation */
        assert(seq_state==2 && seq_stops==stops+1 && !sam_seq_stop);
        assert(sam_state==SAM_NORMALIZING && sam_frames==frames && core_audio_idle_begin());
        assert(!memcmp(pcm,sam_pcm,(stereo?128:64)*sizeof(short)));
        assert(key_event(11,9|(func<<1)) && key_event(11,16|(func<<1)) && seq_stops==stops+1);
        T skips=headroom_normalizing_skips;audio();
        assert(headroom_normalizing_skips==skips+1 && core_audio_idle_begin());
        recorder_ui_tick(manager());assert(seq_stops==stops+1 && core_audio_idle_begin());
        assert(key_event(10,1) && key_event(10,16));
    }
    /* Empty / ARMED without samples keep the existing native STOP route. */
    sam_clear();sam_stereo=0;sam_rlen=8;
    assert(!key_event(11,1) && !key_event(11,16));
    sam_start(1);assert(!sam_frames && sam_state==SAM_ARMED);
    assert(!key_event(11,1) && !key_event(11,16) && sam_state==SAM_EMPTY);
    assert(!core_audio_idle_begin());sam_clear();core_audio_idle_reset();
}
int main(void)
{
    assert(VirtualAlloc((void *)0x80000000u,0x20000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    host=&services;gesture();T cases=0;
    for(T channels=1;channels<=2;channels++)for(T source=0;source<18;source++) {
        if(!sam_source_ok(source,channels==2))continue;
        idle_take(source,channels,SAM_NORMALIZING,0);
        idle_take(source,channels,SAM_READY,0);
        idle_take(source,channels,SAM_READY,1);
        cases+=3;
    }
    audition_take(1);audition_take(2);idle_lifecycle();retained_take_reopen();manual_stop_limits();
    /* Finishing at the frame limit takes the same fast path next block. */
    buses();prepare(0,0,SAM_RECORDING,0,32,0);sam_stereo=sam_take_stereo=0;audio();
    assert(sam_state==SAM_NORMALIZING && sam_frames==32);
    T skips=headroom_normalizing_skips;audio();assert(headroom_normalizing_skips==skips+1);
    /* Normalization deliberately blocks audition of partly processed PCM. */
    sam_keys_down=0;assert(key_event(12,3) && key_event(12,16));assert(!sam_play_length);
    /* A manual STOP freezes the take; the subsequent audio callback skips. */
    prepare(0,0,SAM_RECORDING,0,128,0);audio();assert(sam_frames==32);
    assert(key_event(11,1) && key_event(11,16));assert(sam_state==SAM_NORMALIZING);
    skips=headroom_normalizing_skips;audio();assert(headroom_normalizing_skips==skips+1);
    /* EMPTY re-enables the source/monitor work; closing retires it again. */
    sam_clear();sam_mon=1;T ticks=sam_report[1];audio();assert(sam_report[1]==ticks+1);
    sam_view[0x30]=1;ticks=sam_report[1];T clock=sam_clock;audio();
    assert(!sam_enabled && sam_report[1]==ticks && sam_clock==clock+32);
    sampler_dispose(sam_view);
    for(T i=0;i<64;i++)if(allocs[i])free_mock(allocs[i]);
    VirtualFree((void *)0x80000000u,0,MEM_RELEASE);
    printf("PASS: %u source/state fast paths; 108 partial manual STOP cases across all lengths, mono/stereo, transport and FUNC; mono/stereo FUNC+YES preview; exclusive core owner; STOP/READY/NORMALIZING/save eligibility; PLAY press/repeat/release blocked including modals; capture/exit restores normal mode. Native output tested separately.\n",cases);
    return 0;
}
