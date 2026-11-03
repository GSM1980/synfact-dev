/* Actual System info C; only the native clock/RAM/UI boundaries are doubled. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
static unsigned int snapshots,ram_calls,invalidations,draws;
static unsigned int core_pool_busy[1]={1},core_pool_query_owner[1];
static void core_clock_snapshot(unsigned int *out)
{snapshots++;out[0]=snapshots*100000u;out[1]=snapshots*20000u;out[2]=0;}
static void core_additem(void *menu,const void *row){(void)menu;(void)row;}
static void fw_invalidate(void *view){(void)view;invalidations++;}
static void fw_fillrect(void *b,int a,int c,int d,int e,unsigned int f)
{(void)b;(void)a;(void)c;(void)d;(void)e;(void)f;draws++;}
static void fw_bitmap_ctor(void *b,unsigned int w,unsigned int h,const void *i,const void *m)
{(void)b;(void)w;(void)h;(void)i;(void)m;}
static void fw_bitmap_dtor(void *b){(void)b;}
static void fw_blit(void *a,const void *b,int x,int y,unsigned int m)
{(void)a;(void)b;(void)x;(void)y;(void)m;}
static void si_label(void){}
/* The mod lists (host API 12): zero-terminated 40-byte records, core first. */
static unsigned char core_list[3*40],bank_list[2*40];
static const unsigned char *bank_ptr;
#define SI_CORE_INFO core_list
#define SI_BANK_INFO bank_ptr
#define ST_SYSTEM_INFO_MODULE 1
#include "../out/system-info-tests/system-info-under-test.c"
static unsigned char ctrl[64];
static unsigned int step(void)
{ctrl[0x20]=0;si_tick(ctrl);return ctrl[0x20];}
static void put_be(unsigned char *p,unsigned int v){p[0]=v>>24;p[1]=v>>16;p[2]=v>>8;p[3]=v;}
static int key(unsigned int k,unsigned int flags){unsigned char e[24];memset(e,0,sizeof e);put_be(e+12,k);put_be(e+16,flags);return si_key(0,e);}
static int enc(unsigned int which,int delta){unsigned char e[24];memset(e,0,sizeof e);put_be(e+12,which);put_be(e+16,(unsigned int)delta);return si_enc(0,e);}
/* Opens the list, presses YES on METERS, then NO closes it (press and release). */
static void toggle_meters(void){struct capture toggle={0};si_select(&toggle);key(KEY_YES,1);key(KEY_NO,1);key(KEY_NO,0);}
int main(void)
{
    unsigned int i,paints=0,before,generation;struct capture toggle={0};
    assert(VirtualAlloc((void *)0x10000000u,0x10000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)==(void *)0x10000000u);
    memcpy(core_list,"core",4);memcpy(core_list+24,"1.1",3);
    memcpy(bank_list,"arp-st",6);memcpy(bank_list+24,"1.1",3);bank_ptr=bank_list;
    assert(!step() && !snapshots);toggle_meters();assert(enabled && !list_open);
    /* First paint is immediate, even though the first measurement is later. */
    assert(step() && !strcmp(line,"CPU M --% CPU A --%"));
    for(i=2;i<=1800;i++)paints+=!!step();
    assert(snapshots==60 && ram_calls==60 && paints==30 && !page);
    assert(values[0]==80 && !strcmp(line,"CPU M 80% CPU A --%"));
    ctrl[0x20]=7;si_tick(ctrl);assert(ctrl[0x20]==7); /* Keep native invalidations. */
    before=draws;si_draw(0,0);assert(draws>before);before=draws;
    si_draw(0,0);assert(draws>before); /* Never suppress a requested native draw. */
    page=0;values[0]=values[1]=100;assert(format());
    assert(!strcmp(line,"CPU M 100% CPU A 100%") && strlen(line)<sizeof(line));
    assert(!format());page=1;values[2]=values[3]=100;assert(format());
    assert(!strcmp(line,"RAM M 100% RAM A 100%"));
    /* Polling/acceptance of the AUDIO query still occurs between UI updates. */
    core_pool_busy[0]=0;step();generation=pending;assert(generation && core_pool_query_owner[0]==2);
    step();assert(pending==generation && audio_age==1);
    WIN[40]=REPLY_MAGIC;WIN[41]=generation;WIN[6]=generation<<16|1u;
    for(i=0;i<5;i++)WIN[42+i]=100+i;
    step();assert(!pending && !core_pool_query_owner[0] && audio[0]==100 && audio[4]==104);
    step();assert(pending && pending!=generation);
    /* OFF requests one removal paint and cancels its own mailbox lease. */
    toggle_meters();assert(!enabled && !pending && !core_pool_query_owner[0]);
    assert(step());before=snapshots;
    for(i=0;i<120;i++)assert(!step());
    assert(snapshots==before);before=draws;si_draw(0,0);assert(draws==before);
    core_pool_busy[0]=1;toggle_meters();assert(step());
    assert(!strcmp(line,"CPU M --% CPU A --%"));
    /* The list: METERS, the core, then each bank mod; ENC A and the arrows move the cursor,
     * YES on METERS toggles it, NO closes. Its screen paints. */
    {char text_line[48];
    si_select(&toggle);assert(list_open && rows()==3 && cursor==0);
    assert(enabled);row_text(0,text_line);assert(!strcmp(text_line,"METERS ON"));
    row_text(1,text_line);assert(!strcmp(text_line,"CORE 1.1"));
    row_text(2,text_line);assert(!strcmp(text_line,"ARP-ST 1.1"));
    assert(enc(1,1)==1 && cursor==1);assert(key(KEY_DOWN,1)==1 && cursor==2);
    assert(key(KEY_DOWN,1)==1 && cursor==2);assert(enc(1,-1)==1 && cursor==1);
    assert(key(KEY_YES,1)==1 && enabled);       /* YES off METERS changes nothing */
    before=draws;si_draw(0,0);assert(draws>before);
    assert(key(KEY_NO,1)==1 && !list_open);assert(key(KEY_NO,0)==1);assert(key(KEY_YES,0)==0);
    assert(enc(1,1)==0);
    }
    puts("PASS: 1800 ticks / 60 unchanged measurement windows / 30 redraw requests; identical text suppressed; page flips, first paint, OFF cleanup, 100%, AUDIO polling and native redraws retained");
}
