/* SPDX-License-Identifier: GPL-2.0-or-later; production core/SMZ dispatch, native host. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../mods/core-st/machines.h"
#include <syntakt/host.h>
const struct st_machine_host_api core_machine_host_api={0};

typedef st_u32 u32;
volatile u32 core_machine_active=1;
extern const struct core_machine_def sp_one_shot,sp_werp,sp_repitch,sp_slice;
extern const struct st_machine_ui sp_one_ui,sp_werp_ui,sp_repitch_ui,sp_slice_ui;
int core_machine_direct_format(u32,u32,char *);
static const struct core_machine_def *const machines[]={&sp_one_shot,&sp_werp,&sp_repitch,&sp_slice};
static const struct st_machine_ui *const pages[]={&sp_one_ui,&sp_werp_ui,&sp_repitch_ui,&sp_slice_ui};
const struct core_machine_def *core_machine_find(u32 type)
{return type>=66 && type<=69?machines[type-66]:0;}
const struct core_machine_def *core_machine_parameter(u32 id,u32 *index)
{
    const struct core_machine_def *m;
    if(id<0x1f9 || id>=0x3f9)return 0;
    *index=(id-0x1f9)&31;m=core_machine_find(64+(id-0x1f9)/32);
    return m && *index<m->params?m:0;
}
u32 core_machine_slot(u32 i){return i;}
const struct st_machine_ui *core_machine_ui(const struct core_machine_def *m)
{return m?pages[m->type-66]:0;}
const struct st_machine_wave *core_pool_wave(u32 slot){(void)slot;return 0;}
void core_pool_wave_idle(u32 slot,u32 budget){(void)slot;(void)budget;}
int core_pool_sample_name(u32 slot,char *out)
{
    if(slot==0)strcpy(out,"KICK");
    else if(slot==127)strcpy(out,"LAST SAMPLE");
    else return 0;
    return 1;
}
const struct st_machine_cursor *core_pool_cursor(u32 track,u32 slot){(void)track;(void)slot;return 0;}
void *fw_malloc(u32 n){return malloc(n);}
void fw_invalidate(void *v){(void)v;}
u32 fw_view_machine(void *v){(void)v;return 66;}
void core_machine_dispatch_delta(void *v,u32 id,int delta,u32 knob)
{(void)v;(void)id;(void)delta;(void)knob;assert(0);}

int main(void)
{
    static const char *const modes[]={"REVERSE","REVERSE LOOP","FORWARD LOOP","FORWARD"};
    char out[24];u32 i,j,base;
    for(i=0;i<4;i++){
        const struct core_machine_def *m=machines[i];
        base=0x1f9+(m->type-64)*32;
        assert(!strcmp(m->parameter[3].label,"SAMP"));
        assert(!strcmp(m->parameter[3].name,"SAMP"));
        for(j=0;j<128;j++){
            assert(core_machine_direct_format(base+3,j<<8,out));
            assert(!strcmp(out,"SAMP"));
            assert(!strcmp(core_machine_format(base+3,j<<8),j==0?"KICK":j==127?"LAST SAMPLE":"NO SAMPLE"));
        }
        for(j=0;j<4;j++)assert(!strcmp(core_machine_format(base+1,j<<8),modes[j]));
        assert(core_machine_direct_format(base,0x0400,out) && !strcmp(out,"-60.00"));
        assert(!core_machine_format(base+2,0x7f00)); /* ordinary numeric fallback */
    }
    for(j=0;j<4;j++)assert(!strcmp(core_machine_format(0x1f9+3*32+5,j<<8),modes[j])); /* WERP MODE */
    assert(!core_machine_format(0,0));
    assert(!core_machine_direct_format(0,0,out));
    /* A v1 core's shorter service prefix remains readable by the new bank. */
    {
        struct v1_api {u32 version;const struct st_machine_wave *(*wave)(u32);
            void (*wave_idle)(u32,u32);int (*sample_name)(u32,char *);
            const struct st_machine_cursor *(*cursor)(u32,u32);} old={1,0,0,core_pool_sample_name,0};
        assert(!strcmp(sp_one_ui.format((const struct st_machine_ui_api *)&old,&sp_one_shot,3,0),"KICK"));
    }
    puts("PASS: all four samplers keep SAMP for 128 slots; popup names samples/empty slots and all PLAY modes; WERP MODE and numeric fallback");
    return 0;
}

unsigned int fw_parameter_selecting(void *model){(void)model;return 0;}

int core_event_view(unsigned int op,void *v,void *e,unsigned int id){(void)op;(void)v;(void)e;(void)id;return 0;}

unsigned int core_native_view(unsigned int op,void *v,unsigned int n){(void)op;(void)v;(void)n;return 0;}
