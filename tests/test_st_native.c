/* Shared native transactions and pre/post KEY contexts, actual core C. */
#include <assert.h>
#include <stdio.h>
#include <windows.h>
#include <syntakt/native.h>
typedef unsigned int u32;
#define EV(n) const unsigned char core_##n##_table[6]={0};const u32 ev_##n[]={0};
EV(tick) EV(draw) EV(key) EV(enc) EV(settings) EV(render_in) EV(render_out)
EV(fs_route) EV(fs_write) EV(fs_written) EV(main_mix)
const u32 ev_native[]={0};
const struct st_machine_ui_api core_machine_ui_api={6};
static u32 publications,pre,post,native_calls,replays;
static u32 note_consumed;
extern int core_event_native(u32,const u32 *,u32 *);
void *fw_malloc(u32 n){return VirtualAlloc((void *)0x47000000u,n,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);}
void fw_free(void *p){assert(VirtualFree(p,0,MEM_RELEASE));}
void core_event_publish(const u32 *a)
{
    u32 track=2,result=99;
    assert(a[0]==(u32)(core_key_table+2) && a[1] && a[2] && a[3]==a[1] && a[4] && a[5]);
    {u32 k;for(k=6;a[k];k+=2)*(u32 *)a[k]=a[k+1];assert(k==6+2*((ST_NATIVE_OPERATION_LIMIT+3)/4+1));}
    *(const u32 **)a[2]=(const u32 *)a[3];*(const u32 **)a[4]=(const u32 *)a[5];
    /* Simulate an IRQ immediately when the publisher unmasks, before bind
     * returns. Its native services must already be visible in the C list. */
    assert(core_event_native(ST_NATIVE_SOUND_ALLOWED,&track,&result) && result==0);
    assert(native_calls==2);native_calls=0;publications++;
}
void core_machine_input_reset(void){}
#include "../mods/core-st/modules.c"
u32 core_stock_trig(u32 a,u32 b,u32 c,u32 d,u32 e,u32 f){replays++;return a+b+c+d+e+f;}
u32 core_stock_header_load(u32 a,u32 b){replays++;return a^b;}
void core_stock_header_reset(u32 a,u32 b){assert(a==7 && b==8);replays++;}
const u32 *core_stock_page_layout(u32 page){return (const u32 *)(page+4);}
static u32 native_timer;
#define CORE_NATIVE_TIMER() (native_timer+=3)
#include "../mods/core-st/native.c"
static int interest_open(void){u32 i;for(i=0;i<4*NATIVE_WORDS;i++)if(i<ST_NATIVE_OPERATION_COUNT?core_native_interest[i]!=1:i==ST_NATIVE_OPERATION_COUNT+1 && !core_native_interest[i])return 0;return 1;}
static int interest_closed(void){u32 i;for(i=0;i<4*NATIVE_WORDS;i++)if(core_native_interest[i])return 0;return 1;}
static int initialize(const struct st_smz_api *a){assert(a->version==3 && a->ui->version==6);return 0;}
static int first(u32 op,const u32 *a,u32 *out){(void)op;(void)a;(void)out;native_calls++;return 0;}
static int second(u32 op,const u32 *a,u32 *out)
{native_calls++;if(op==ST_NATIVE_SOUND_ALLOWED){assert(a[0]==2);*out=0;return 1;}
 if(op==ST_NATIVE_NOTE_ACCEPT){*out=note_consumed;return 1;}return 0;}
static int before(void *v,void *e,u32 phase){assert(v==(void *)1 && e==(void *)2 && phase==1);pre++;return 1;}
static void after(void *v,void *e,u32 phase){assert(v==(void *)1 && e==(void *)2 && !phase);post++;}
int main(void)
{
    const struct st_smz_subscription subs[]={
        {ST_EV_KEY|ST_EV_VIEW|ST_EV_PRE,(void (*)(void))before},
        {ST_EV_KEY|ST_EV_VIEW,(void (*)(void))after},
        {ST_EV_NATIVE,(void (*)(void))first},
        {ST_EV_NATIVE,(void (*)(void))second}};
    const struct st_smz_module m={"NATIVE",initialize,subs,4};
    u32 a[]={1,2,3,4,5,6},v;
    assert(interest_open());core_native_initialize();assert(interest_closed());   /* fail open, then none */
    assert(core_native(ST_NATIVE_TRIG,a)==21 && replays==1);
    assert(core_native(ST_NATIVE_HEADER_LOAD,a)==3 && replays==2);
    assert(core_native(ST_NATIVE_MACHINE_ALLOWED,a)==1);
    assert(core_native(ST_NATIVE_PAGE_NAV,a)==0xffffffffu);
    assert(core_modules_bind(&m,1) && publications==1 && core_module_subscriptions==4 && interest_open());
    assert(core_event_view(ST_EV_KEY,(void *)1,(void *)2,1)==1 && pre==1 && !post);
    assert(!core_event_view(ST_EV_KEY,(void *)1,(void *)2,0) && pre==1 && post==1);
    {static u32 profile[2*ST_NATIVE_OPERATION_COUNT];u32 before=native_calls;
     core_native_profile(profile);a[0]=2;assert(!core_native(ST_NATIVE_SOUND_ALLOWED,a));
     assert(profile[ST_NATIVE_OPERATION_COUNT+ST_NATIVE_SOUND_ALLOWED]==1 && native_calls==before+2);
     core_native_profile(0);native_calls=before;}                         /* optional profile: same result */
    a[0]=2;assert(!core_native(ST_NATIVE_SOUND_ALLOWED,a) && native_calls==2);
    assert(core_native(ST_NATIVE_MACHINE_ALLOWED,a)==1 && native_calls==4);
    assert(!core_event_native(ST_NATIVE_PAGE_GUARD,a,&v) && native_calls==6);
    a[0]=7;a[1]=8;core_native(ST_NATIVE_HEADER_RESET,a);assert(replays==3);
    /* Host API 5: the union of the subscribers' interests; others replay stock. */
    core_native_interest_set((void (*)(void))first,1u<<ST_NATIVE_TRIG);
    assert(interest_open());                                                  /* second still wants all */
    core_native_interest_set((void (*)(void))second,1u<<ST_NATIVE_SOUND_ALLOWED);
    for(v=0;v<4*NATIVE_WORDS;v++)assert(core_native_interest[v]==(v==ST_NATIVE_TRIG || v==ST_NATIVE_SOUND_ALLOWED));
    native_calls=0;a[0]=2;assert(!core_native(ST_NATIVE_SOUND_ALLOWED,a) && native_calls==2);
    native_calls=0;a[0]=1;a[1]=2;assert(core_native(ST_NATIVE_HEADER_LOAD,a)==(1u^2u) && !native_calls && replays==4); /* stock replay */
    assert(core_native(ST_NATIVE_MACHINE_ALLOWED,a)==1 && !native_calls);
    /* TYPE track filter: bits 24..31 per subscriber, 0/0xff = every cursor. */
    core_native_interest_set((void (*)(void))first,1u<<ST_NATIVE_TYPE|0x06000000u);
    assert(core_native_interest[ST_NATIVE_TYPE] && core_native_interest[18]==6 && !core_native_interest[19]);
    core_native_interest_set((void (*)(void))second,1u<<ST_NATIVE_TYPE|0x80000000u);
    assert(core_native_interest[18]==0x86 && !core_native_interest[19]);
    core_native_interest_set((void (*)(void))second,1u<<ST_NATIVE_TYPE);assert(core_native_interest[19]==1);
    core_native_interest_set((void (*)(void))first,~0u);core_native_interest_set((void (*)(void))second,~0u);assert(interest_open());
    /* Host API 6: one shared page view class, else every view. */
    core_native_page_view_set((void (*)(void))first,0x1234);assert(!core_native_page_view);
    core_native_page_view_set((void (*)(void))second,0x1234);assert(core_native_page_view==0x1234);
    {u32 other=0x5678,same=0x1234;native_calls=0;                       /* stock results, no dispatch */
     assert(!core_native_view(ST_NATIVE_PAGE_SYNC,&other,0) && core_native_view(ST_NATIVE_PAGE_NAV,&other,1)==~0u && !native_calls);
     core_native_view(ST_NATIVE_PAGE_SYNC,&same,0);assert(native_calls==2);}
    core_native_page_view_set((void (*)(void))second,0x5678);assert(!core_native_page_view);
    core_native_interest_set((void (*)(void))second,1u<<ST_NATIVE_TRIG);assert(core_native_page_view==0x1234);
    core_native_interest_set((void (*)(void))second,~0u);core_native_page_view_set((void (*)(void))first,0);
    core_native_page_view_set((void (*)(void))second,0);assert(!core_native_page_view && interest_open());
    /* New notifications do not overrun legacy host API 4 profiler buffers. */
    {u32 guarded[38]={0},before=native_calls;guarded[0]=guarded[37]=0x12345678;
     core_native_profile(guarded+1);assert(!core_native(ST_NATIVE_NOTE_ACCEPT,a));
     assert(!core_native(ST_NATIVE_SOUND_META,a));
     assert(native_calls==before+4 && guarded[0]==0x12345678 && guarded[37]==0x12345678);
     for(v=1;v<37;v++)assert(!guarded[v]);core_native_profile(0);
     assert(!core_native(ST_NATIVE_OPERATION_LIMIT,a));}
    core_native_interest_set((void (*)(void))first,1u<<ST_NATIVE_TRIG);
    core_native_interest_set((void (*)(void))second,1u<<ST_NATIVE_NOTE_ACCEPT);
    assert(core_native_interest[20] && !core_native_interest[18] && !core_native_interest[19]);
    note_consumed=1;assert(core_native(ST_NATIVE_NOTE_ACCEPT,a)==1);
    note_consumed=0;assert(!core_native(ST_NATIVE_NOTE_ACCEPT,a));
    puts("PASS: per-operation interest (fail open, atomic with the list, union, stock replay); shared native list order/consume/stock fallback; API 3 negotiation; consuming pre-KEY isolated from legacy post-KEY; no additional input registry");return 0;
}
