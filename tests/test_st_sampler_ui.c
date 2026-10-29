/* SPDX-License-Identifier: GPL-2.0-or-later; native C, no target execution. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <syntakt/machines.h>
typedef st_u32 u32;
static u32 dial_value,bitmap_index,blits;static char painted[24];
static void fill(void *c,int x,int y,int xx,int yy,u32 v){(void)c;(void)v;assert(xx>=x && yy>=y);}
static void pixel(void *c,int x,int y,u32 v){(void)c;(void)x;(void)y;(void)v;}
static void textf(void *c,void *f,int x,int y,u32 flags,const char *fmt,...)
{va_list a;(void)c;(void)f;(void)x;(void)y;(void)flags;va_start(a,fmt);snprintf(painted,sizeof(painted),"%s",va_arg(a,const char *));va_end(a);}
static void native_dial(void *m,u32 id,u32 raw,u32 a,u32 b,void *ctx,int x,int y)
{(void)m;(void)a;(void)b;(void)ctx;(void)x;(void)y;assert(id==0x13);dial_value=raw;}
static void *bitmap(void *v,u32 i,u32 a){assert((u32)v==0x444e0cc8u && !a);bitmap_index=i;return (void *)1;}
static void blit(void *c,void *b,int x,int y,u32 f){(void)c;(void)x;(void)y;(void)f;assert(b==(void *)1);blits++;}
#define ST_UI_PRIMITIVES
#define FILL fill
#define FRAME fill
#define PIXEL pixel
#define TEXT textf
#define FONT ((void *)0)
#define ST_UI_NATIVE_DIAL native_dial
#define ST_UI_BITMAP bitmap
#define ST_UI_BLIT blit
#include "../mods/samplers-st/main.c"
#include "../mods/samplers-st/ui.c"
#define SP_INPUT_NATIVE
static u32 input_type=66,input_track,input_raw,input_locked,input_edits,input_trig,input_held,input_knob,input_max=3,input_min,input_func;
static u32 input_policy_words[5],input_increments[7],input_policy_calls,input_increment_calls;
static u32 input_snap_active,input_snap_counter,input_snap_arms,input_snap_checks;
static int input_increment_zero;
static int input_delta,input_pressed;
static unsigned char input_view[0x100];
static u32 input_id(void){return 0x1f9u+(input_type-64)*32+input_knob;}
static u32 input_type_get(void *v){assert(v==input_view);return input_type;}
static u32 input_track_get(void *v){assert(v==(void *)0x99);return input_track;}
static u32 input_get(void *v,u32 id,unsigned char *locked)
{assert(v==input_view && id==input_id());*locked=(unsigned char)input_locked;return input_raw;}
static int input_event(void *e,u32 a,u32 b){assert(e==(void *)0x77 && a==1 && b==(input_func && input_knob==0?2u:1u));return input_delta;}
static u32 input_press(void *e){assert(e==(void *)0x77);return input_pressed;}
static u32 input_trig_get(void *v){assert(v==(void *)0x88);return input_trig;}
static u32 input_key_get(u32 key){assert(key==1 || key==2 || key==0x12);return key==1?input_func:input_held;}
static u32 input_policy(u32 id)
{assert(id==0x1f1);input_policy_calls++;input_policy_words[1]=1;input_policy_words[2]=2;input_policy_words[4]=(u32)input_increments;return (u32)input_policy_words;}
static int input_increment(void *state,u32 knob,int delta,void *inc,u32 pressed)
{assert(state==input_view+0x94 && !knob && pressed==(u8)input_pressed && !memcmp(inc,input_increments,sizeof(input_increments)));input_increment_calls++;return input_increment_zero?0:delta;}
static void input_snap_arm(void *state,u32 knob)
{assert(state==input_view+0x94 && !knob);input_snap_counter=4;input_snap_arms++;}
static u32 input_snap_blocked(void *state,u32 knob)
{assert(state==input_view+0x94 && !knob);input_snap_checks++;return input_snap_counter?0xffu:0;}
static int input_snap_lookup(int note,int delta,const void *list)
{
    const u32 *sentinel=list,*node=(const u32 *)sentinel[delta>0?0:1];
    assert(delta);
    for(;node!=sentinel;node=(const u32 *)node[delta>0?0:1])
        if(delta>0?(int)node[2]>note:(int)node[2]<note)return (int)node[2];
    return note;
}
static void input_edit(void *v,u32 id,int delta,u32 knob)
{assert(v==input_view && id==input_id() && knob==input_knob);input_raw+=delta;assert(input_raw>=input_min*256 && input_raw<=input_max*256);input_edits++;if(input_trig)input_locked=1;}
#define SP_INPUT_TYPE input_type_get
#define SP_INPUT_TRACK input_track_get
#define SP_INPUT_GET input_get
#define SP_INPUT_DELTA input_event
#define SP_INPUT_PRESSED input_press
#define SP_INPUT_TRIG input_trig_get
#define SP_INPUT_HELD input_key_get
#define SP_INPUT_POLICY input_policy
#define SP_INPUT_INCREMENT input_increment
#define SP_INPUT_SNAP_ARM input_snap_arm
#define SP_INPUT_SNAP_BLOCKED input_snap_blocked
#define SP_INPUT_SNAP_ACTIVE input_snap_active
#define SP_INPUT_SNAP_LOOKUP input_snap_lookup
#include "../mods/samplers-st/input.c"
static int input_turn(int delta)
{input_delta=delta;return sp_input_encoder(input_view,(void *)0x77,input_id());}
static void verify_mode_input(void)
{
    static const u32 cases[][3]={
        {66,1,3},{67,1,3},{67,4,2},{67,5,3},{67,6,3},
        {68,1,3},{68,6,3},{69,1,3},{69,4,64},{69,6,4}};
    struct st_machine_ui_api ui={0};struct st_smz_api api={3,&ui};u32 t,p,k,n;
    ui.version=7;ui.edit=input_edit;
    assert(sp_input_init(0));api.version=2;assert(sp_input_init(&api));api.version=3;
    ui.version=6;assert(sp_input_init(&api));ui.version=7;assert(!sp_input_init(&api));
    assert(sp_sampler_input_module.count==3);
    assert(sp_input_events[0].event==(ST_EV_ENC|ST_EV_VIEW));
    assert(sp_input_events[1].event==(ST_EV_KEY|ST_EV_VIEW));
    assert(sp_input_events[2].event==(ST_EV_KEY|ST_EV_VIEW|ST_EV_PRE));
    *(u32 *)(input_view+0x88)=0x14;*(void **)(input_view+0x74)=(void *)0x99;
    *(void **)(input_view+0x6c)=(void *)0x88;
    for(k=0;k<sizeof(cases)/sizeof(cases[0]);k++)for(p=0;p<2;p++){
        input_type=cases[k][0];input_knob=cases[k][1];input_max=cases[k][2];
        *(u32 *)(input_view+0x90)=p;
        for(input_pressed=0;input_pressed<2;input_pressed++)for(int dir=-1;dir<=1;dir+=2){
            sp_input_reset(0,0,0);input_raw=dir<0?input_max*256:0;input_edits=0;
            for(n=0;n<input_max;n++){
                u32 before=input_raw;
                for(u32 step=0;step<15;step++){assert(input_turn(dir));assert(input_raw==before && input_edits==n);}
                assert(input_turn(dir));assert(input_raw==before+dir*256 && input_edits==n+1);
            }
            assert(input_turn(dir*1000000));assert(input_raw==(dir<0?0:input_max*256));
            for(n=0;n<15;n++){assert(input_turn(-dir));assert(input_raw==(dir<0?0:input_max*256));}
            assert(input_turn(-dir));assert(input_raw==(dir<0?0x100:(input_max-1)*256));
        }
        sp_input_reset(0,0,0);input_raw=0;input_locked=0;input_edits=0;
        input_trig=0xff;input_held=1;
        assert(input_turn(15) && !input_edits);
        assert(input_turn(1) && input_edits==2 && input_raw==0x100 && input_locked);
        input_trig=input_held=0;
    }
    /* Nonselected controls, including SLICE LEN, retain their native policy. */
    for(t=66;t<=69;t++)for(n=0;n<8;n++){
        u32 handled=0;for(k=0;k<sizeof(cases)/sizeof(cases[0]);k++)
            if(cases[k][0]==t && cases[k][1]==n)handled=1;
        if(!handled){input_type=t;input_knob=n;assert(!input_turn(4));}
    }
    /* A partial SEG movement cannot spill into BARS or MODE on the same page. */
    input_type=67;input_knob=4;input_max=2;input_raw=0;input_pressed=0;
    sp_input_reset(0,0,0);assert(input_turn(15) && input_raw==0);
    input_knob=6;input_max=3;assert(input_turn(1) && input_raw==0);
    assert(input_turn(15) && input_raw==0x100);
    input_type=66;input_knob=1;input_max=3;input_pressed=0;*(u32 *)(input_view+0x90)=0;
    /* Batched steps, remainder, reversal and saturating extreme event arithmetic. */
    sp_input_reset(0,0,0);input_raw=0;assert(input_turn(17) && input_raw==0x100);
    assert(input_turn(15) && input_raw==0x200);assert(input_turn(-15) && input_raw==0x200);
    assert(input_turn(-1) && input_raw==0x100);
    assert(input_turn(0x7fffffff) && input_raw==0x300);
    assert(input_turn(-0x7fffffff-1) && input_raw==0);
    /* A partial movement never leaks between views of the control or new values. */
    for(t=0;t<9;t++){
        sp_input_reset(0,0,0);input_raw=0;input_locked=0;input_edits=0;
        assert(input_turn(15) && !input_edits);
        switch(t){
        case 0:input_track++;break;
        case 1:*(u32 *)(input_view+0x90)^=1;break;
        case 2:input_raw=0x100;break;
        case 3:input_locked=1;break;
        case 4:input_pressed^=1;break;
        case 5:sp_input_reset(input_view,0,0);break; /* post-native KEY */
        case 6:assert(!sp_input_encoder(input_view,0,input_id()+1));break;
        case 7:input_func^=1;break;
        case 8:input_type=input_type==66?67:66;break;
        }
        assert(input_turn(1) && !input_edits);assert(input_turn(15) && input_edits==1);
    }
    input_func=0;
    /* Trig/page-lock seeding retains the existing native edit transaction. */
    sp_input_reset(0,0,0);input_raw=0;input_locked=0;input_edits=0;input_trig=0xff;input_held=1;
    assert(input_turn(15) && !input_edits);assert(input_turn(1) && input_edits==2 && input_raw==0x100 && input_locked);
    input_trig=input_held=0;
    *(u32 *)(input_view+0x88)=0;assert(!input_turn(4));*(u32 *)(input_view+0x88)=0x14;
    *(u32 *)(input_view+0x90)=2;assert(!input_turn(4));*(u32 *)(input_view+0x90)=0;
    input_type=65;assert(!input_turn(4));input_type=66;
    /* SLCE's full NOTE..64 range remains reachable in a batched event. */
    input_type=69;input_knob=4;input_max=64;input_raw=0;sp_input_reset(0,0,0);
    assert(input_turn(1024) && input_raw==0x4000);assert(input_turn(-1024) && input_raw==0);
    /* The previous four-step movement and a zero event cannot change a choice.
     * Fast batches use the same distance as slow turning, also with FUNC held. */
    for(input_func=0;input_func<2;input_func++){
        input_raw=0;input_edits=0;sp_input_reset(0,0,0);
        assert(input_turn(4) && !input_edits);assert(input_turn(0) && !input_edits);
        assert(input_turn(12) && input_raw==0x100 && input_edits==1);
        assert(input_turn(32) && input_raw==0x300 && input_edits==2);
    }
    input_func=0;
    puts("PASS: ten SP discrete controls require sixteen raw steps across their full ranges/both pages, pressed/FUNC/batched events, locks, limits and context resets; other controls decline");
}
static int sample_name(u32 slot,char *out){if(slot!=127)return 0;strcpy(out,"POOL 128");return 1;}
static u32 reference_octave(u32 raw,int direction,int extended)
{
    static const int notes[]={-60,-48,-36,-24,-12,0,12,24};
    int note=((int)raw-0x4000)>>8,result=note,first=extended?0:3;
    if(direction>0){for(int i=first;i<8;i++)if(notes[i]>note){result=notes[i];break;}}
    else {for(int i=7;i>=first;i--)if(notes[i]<note){result=notes[i];break;}}
    return (u32)(0x4000+256*result);
}
static void verify_tune_input(void)
{
    const struct core_machine_def *machines[]={&sp_one_shot,&sp_werp,&sp_slice};
    const struct st_machine_ui *uis[]={&sp_one_ui,&sp_werp_ui,&sp_slice_ui};
    static const u32 octaves[]={0x0400,0x1000,0x1c00,0x2800,0x3400,0x4000,0x4c00,0x5800};
    struct st_machine_ui_api ui={0};struct st_smz_api api={3,&ui};u32 i,p,j,raw,comparisons=0;
    ui.version=7;ui.edit=input_edit;assert(!sp_input_init(&api));
    input_knob=0;input_min=4;input_max=88;input_trig=input_held=0;
    for(i=0;i<3;i++){
        const struct core_machine_param *parameter=machines[i]->parameter;
        assert(parameter->alias==0x13 && parameter->min==0x0400 && parameter->max==0x5800 && parameter->initial==0x4000);
        assert(uis[i]->policy(0)==0x1f1);input_type=machines[i]->type;
        for(raw=0x0400;raw<=0x5800;raw++)for(int dir=-1;dir<=1;dir+=2){
            assert(sp_input_octave(raw,dir,parameter)==reference_octave(raw,dir,1));
            if(raw>=0x2900 || (raw>=0x2800 && dir>0))
                assert(sp_input_octave(raw,dir,parameter)==reference_octave(raw,dir,0));
            comparisons++;
        }
        for(p=0;p<2;p++)for(input_pressed=0;input_pressed<2;input_pressed++){
            *(u32 *)(input_view+0x90)=p;input_func=1;input_raw=0x5800;input_locked=0;
            input_snap_active=0;input_snap_counter=0;input_edits=0;
            raw=input_snap_arms;
            /* Native FUNC entry arms before checking the suppression gate. */
            assert(input_turn(-1) && input_raw==0x5800 && !input_edits);
            assert(input_snap_active && input_snap_arms==raw+1 && input_snap_counter==4);
            for(j=7;j>0;j--){
                /* Gate blocks regardless of encoder magnitude, until native
                 * periodic processing expires it (modeled explicitly here). */
                assert(input_turn(-1000) && input_raw==octaves[j]);
                input_snap_counter=0;
                assert(input_turn(-1) && input_raw==octaves[j-1] && input_snap_counter==4);
            }
            input_snap_counter=0;raw=input_snap_arms;
            assert(input_turn(-1) && input_raw==0x0400 && input_snap_arms==raw+1);
            for(j=1;j<8;j++){
                input_snap_counter=0;
                assert(input_turn(1000) && input_raw==octaves[j] && input_snap_counter==4);
            }
            input_snap_counter=0;assert(input_turn(1) && input_raw==0x5800 && input_snap_counter==4);
            input_snap_counter=0;input_raw=0x27ff;assert(input_turn(1) && input_raw==0x2800);
            input_snap_counter=0;input_raw=0x3401;assert(input_turn(-1) && input_raw==0x2800);
            input_snap_counter=0;input_raw=0x3fff;assert(input_turn(1) && input_raw==0x4000);
            input_snap_counter=0;input_raw=0x4001;assert(input_turn(-1) && input_raw==0x3400);
            /* Zero increments return before changing FUNC or the snap gate. */
            input_edits=0;raw=input_snap_checks;assert(input_turn(0) && !input_edits && input_snap_checks==raw);
            input_increment_zero=1;assert(input_turn(1000) && !input_edits && input_snap_checks==raw);input_increment_zero=0;
            input_raw=0x1c00;input_trig=0xff;input_held=1;input_edits=0;input_locked=0;input_snap_counter=0;
            assert(input_turn(-1) && input_raw==0x1000 && input_locked && input_edits==2);
            assert(input_turn(1) && input_raw==0x1000 && input_edits==2);
            input_snap_counter=0;assert(input_turn(1) && input_raw==0x1c00 && input_edits==4);
            input_trig=input_held=0;
            input_func=0;input_raw=0x3456;input_edits=0;
            raw=input_policy_calls;assert(!input_turn(1) && input_raw==0x3456 && !input_edits && input_policy_calls==raw);
        }
    }
    input_type=68;input_func=1;assert(!input_turn(1));
    assert(sp_repitch.parameter[0].alias==0x1f1);
    input_func=0;input_min=0;
    assert(input_policy_calls==input_increment_calls && comparisons==129030);
    puts("PASS: FUNC+TUNE uses native policy/increment, FUNC transition, snap gate/re-arm and ordered-list lookup; extended -60..+24 list, 129030 range vectors, both pages/pressed/locks; ordinary tuning and REPITCH decline");
}

int main(void)
{
    const struct core_machine_def *machines[]={&sp_one_shot,&sp_werp,&sp_repitch,&sp_slice};
    struct st_machine_ui_api api={0};struct st_machine_dial d={0};u32 i,j,previous;
    api.version=2;api.format_context=ST_UI_FORMAT_POPUP;api.sample_name=sample_name;
    for(i=0;i<4;i++){
        const struct core_machine_def *m=machines[i];assert(m->type==66+i && m->category==11);
        assert(!strcmp(format(&api,m,0,0x0400),"-60.00"));
        assert(!strcmp(format(&api,m,0,0x5800),"+24.00"));
        previous=0;for(j=0x0400;j<=0x5800;j+=64){d.raw=j;assert(dial(&api,m,0,&d));assert(dial_value>=previous);previous=dial_value;}
        d.raw=0x2800;dial(&api,m,0,&d);assert(dial_value>0 && dial_value<0x4000);
        d.raw=0x4000;dial(&api,m,0,&d);assert(dial_value==0x4000);
        for(j=0;j<4;j++){d.raw=j<<8;assert(dial(&api,m,1,&d));assert(bitmap_index==j);}
        d.raw=0;dial(&api,m,3,&d);assert(!strcmp(painted,"1"));
        d.raw=0x7f00;dial(&api,m,3,&d);assert(!strcmp(painted,"128"));
        assert(!strcmp(format(&api,m,3,0x7f00),"POOL 128"));
        assert(!format(&api,m,2,0x7f00)); /* native BR, 0..127 */
    }
    assert(blits==16);
    assert(!strcmp(format(&api,&sp_one_shot,4,(101<<8)|241),"101.94"));
    assert(!strcmp(format(&api,&sp_one_shot,5,0x7800),"120.00"));
    assert(one_policy(4)==0x1f1 && one_policy(6)==0x1f1 && one_policy(3)==0xffffffffu);
    assert(repitch_policy(5)==0x1f1 && repitch_policy(6)==0xffffffffu);
    assert(!strcmp(format(&api,&sp_slice,4,0),"NOTE"));
    assert(!strcmp(format(&api,&sp_slice,4,0x4000),"64"));
    assert(!strcmp(format(&api,&sp_werp,4,0),"1/32"));
    assert(!strcmp(format(&api,&sp_werp,4,0x0200),"1/8"));
    assert(!strcmp(format(&api,&sp_werp,6,0),"1"));
    assert(!strcmp(format(&api,&sp_werp,6,0x0300),"8"));
    assert(!strcmp(format(&api,&sp_repitch,6,0x0300),"8"));
    assert(!strcmp(format(&api,&sp_slice,6,0),"4"));
    assert(!strcmp(format(&api,&sp_slice,5,0x3f00),"64"));
    assert(!strcmp(format(&api,&sp_slice,6,0x0400),"64"));
    assert(sp_one_ui.pages==2 && sp_one_ui.layout[8]==0 && sp_one_ui.layout[15]==7);
    verify_mode_input();
    verify_tune_input();
    puts("PASS: sampler TUNE normalization, Q8 decimals, native PLAY bitmaps, pool 1..128, discrete controls and duplicated SYN2 controls");
    return 0;
}
