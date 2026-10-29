/* Generic API v4 services, actual production C on a native host. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../mods/core-st/machines.h"
#include <syntakt/host.h>
const struct st_machine_host_api core_machine_host_api={0};
typedef st_u32 u32;
static unsigned char view[256];static u32 type=65,calls,keys,dispatches,clears,native_queries;
static struct core_machine_def machine={65,28};
volatile u32 core_machine_active=1;
static u32 action_layouts[4][11]={{0,0,0x219,0x21c},{0,0,0x231},{0},{0}};
u32 core_machine_engine(const struct core_machine_def *m){(void)m;return 0x219;}
u32 *core_machine_layout(const struct core_machine_def *m,u32 p){assert(m==&machine && p<4);return action_layouts[p];}
const struct core_machine_def *core_machine_find(u32 id){return id==65?&machine:0;}
const struct core_machine_def *core_machine_parameter(u32 id,u32 *i)
{if(id<0x219 || id>0x234)return 0;*i=id-0x219;return &machine;}
u32 core_machine_slot(u32 i){return i+0x12;}
const struct st_machine_ui *core_machine_ui(const struct core_machine_def *m){(void)m;return 0;}
const struct st_machine_wave *core_pool_wave(u32 n){(void)n;return 0;}
void core_pool_wave_idle(u32 n,u32 b){(void)n;(void)b;}
int core_pool_sample_name(u32 n,char *s){(void)n;(void)s;return 0;}
const struct st_machine_cursor *core_pool_cursor(u32 t,u32 n){(void)t;(void)n;return 0;}
void *fw_malloc(u32 n){return malloc(n);}
void fw_free(void *p){free(p);}
void fw_reset_increments(void *v){(void)v;assert(0);}
void fw_invalidate(void *v){(void)v;}
u32 fw_view_machine(void *v){assert(v==view);native_queries++;return type;}
#include "../mods/core-st/machine-ui.c"
#include "../mods/core-st/machine-pages.c"
int (*core_parameter_provider)(unsigned int,unsigned int *,void *,unsigned int *);   /* machines.c (host API 7), none here */
void core_machine_dispatch_delta(void *v,u32 id,int delta,u32 knob)
{assert(v==view && core_machine_edit_id==id && delta==-256 && knob==3);dispatches++;}
static void clear_mock(void *v,u32 id,u32 knob)
{assert(v==view && core_machine_edit_id==id && knob==3);clears++;}
static int enc(void *v,void *e,u32 id)
{assert(v==view && !e && id==0x21c);calls++;return 7;}
static void key_mock(void *v,void *e){assert(v==view && !e);keys++;}
int main(void)
{
    u32 table[32]={0};const struct st_machine_ui_api *a=&core_machine_ui_api;
    u32 action[11]={0,0,0x219,0x21c},other[11]={0,0,0x219};
    struct st_machine_action_codec codec={0x21c,0,0,0},bad={0,0,0,0};
    table[0x68/4]=table[0x6c/4]=(u32)clear_mock;*(u32 **)view=table;
    assert(a->version==7 && a->host==&core_machine_host_api && !a->bind_input(63,enc,key_mock) && !a->bind_input(80,enc,key_mock));
    assert(!a->bind_input(65,enc,key_mock)); /* retired API refuses safely */
    assert(!a->bind_codec(&bad) && a->bind_codec(&codec));
    core_machine_action_select(action);assert(core_machine_action_codec==&codec);
    core_machine_action_select(other);assert(!core_machine_action_codec);
    core_machine_action_select(0);assert(!core_machine_action_codec);
    assert(!native_queries && !calls && !keys);
    *(u32 *)(view+0x88)=0x14;type=65;
    {
        u32 pages[4]={6,6,6,6},old;
        *(u32 *)view=0x401ca0dcu;
        *(u32 **)(view+0x7c)=pages;*(u32 **)(view+0x80)=*(u32 **)(view+0x84)=pages+4;
        assert(!core_machine_action_begin(view) && core_machine_action_codec==&codec);
        *(u32 *)(view+0x90)=1;old=core_machine_action_begin(view);
        assert(old && !core_machine_action_codec);
        core_machine_action_end(old);assert(core_machine_action_codec==&codec);
        core_machine_action_end(0);assert(!core_machine_action_codec);
    }
    *(u32 **)view=table;
    core_machine_edit_id=0x1234;
    a->edit(view,0x21c,-256,3);a->edit(view,0x230,-256,3);a->edit(view,0,-256,3);
    assert(dispatches==2 && core_machine_edit_id==0x1234);
    a->clear(view,0x230,0,3);a->clear(view,0x230,1,3);a->clear(view,0,0,3);
    assert(clears==2 && core_machine_edit_id==0x1234);
    core_machine_input_reset();assert(!core_machine_action_codec && !calls && !keys);
    core_machine_action_select(action);assert(!core_machine_action_codec);
    puts("PASS: API v4 rejects retired input registration; explicit hidden edits/clear; native dispatch scope; codec lifetime");return 0;
}

unsigned int core_native(unsigned int op,const unsigned int *a){(void)op;(void)a;return op==10?0xffffffffu:0;}
unsigned int core_native_view(unsigned int op,void *v,unsigned int b){(void)v;(void)b;return op==10?0xffffffffu:0;}

unsigned int fw_parameter_selecting(void *model){(void)model;return 0;}

int core_event_view(unsigned int op,void *v,void *e,unsigned int id){(void)op;(void)v;(void)e;(void)id;return 0;}
