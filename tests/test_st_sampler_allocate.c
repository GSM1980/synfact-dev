/* SPDX-License-Identifier: GPL-2.0-or-later
 * Production UI/input with native per-step lock doubles and raster assertions.
 * Reuse the browser fixture's UI boundaries, not a second menu implementation.
 */
#include <stdarg.h>
static void *allocate_track(void *);
static void *allocate_data(void *);
static unsigned int allocate_length(void *);
static int allocate_present(void *,unsigned int);
static int allocate_get(void *,unsigned int,unsigned int);
static void allocate_set(void *,unsigned int,unsigned int,unsigned int);
static void allocate_clear(void *,unsigned int,unsigned int);
static unsigned int allocate_selecting(void *);
static unsigned int allocate_clock(void);
static void allocate_notify(void *,const char *,...);
#define SP_ALLOCATE_NATIVE
#define SP_ALLOCATE_TRACK allocate_track
#define SP_ALLOCATE_DATA allocate_data
#define SP_ALLOCATE_LENGTH allocate_length
#define SP_ALLOCATE_PRESENT allocate_present
#define SP_ALLOCATE_GET allocate_get
#define SP_ALLOCATE_SET allocate_set
#define SP_ALLOCATE_CLEAR allocate_clear
#define SP_ALLOCATE_SELECTING allocate_selecting
#define SP_ALLOCATE_CLOCK allocate_clock
#define SP_ALLOCATE_NOTIFY allocate_notify
#define main browser_fixture_main
#include "test_st_sampler_browser.c"
#undef main
struct pattern {u32 length,present[64];int locks[64][8];};
static struct pattern patterns[2][13],before_patterns[2][13],expected[2][13];
static u32 current_pattern,selecting,sets,deletes,fail_set,pattern_model,pages[]={6};
static char notification[40];
static struct pattern *current(void){return &patterns[current_pattern][track];}
static void *allocate_track(void *app){assert(app==(void *)0x9876);return &pattern_model;}
static void *allocate_data(void *p){assert(p==&pattern_model);return current();}
static u32 allocate_length(void *p){assert(p==&pattern_model);return current()->length;}
static int allocate_present(void *p,u32 step){assert(p==&pattern_model && step<64);return current()->present[step]!=0;}
static int allocate_get(void *v,u32 step,u32 id){assert(v==view && step<64 && id==0x29d);return current()->locks[step][4];}
static void allocate_set(void *v,u32 step,u32 id,u32 value)
{
    assert(v==view && step<current()->length && current()->present[step] && id==0x29d);
    assert(value<=0x4000);sets++;if(sets==fail_set)return;
    current()->locks[step][4]=(int)value;
}
static void allocate_clear(void *v,u32 step,u32 id)
{assert(v==view && step<64 && id==0x29d);current()->locks[step][4]=-1;deletes++;}
static u32 allocate_selecting(void *p){assert(p==(void *)0x5678);return selecting;}
static u32 allocate_clock(void){return 0x4516;}
static void allocate_notify(void *p,const char *fmt,...)
{va_list a;assert(p==(void *)0x5678);va_start(a,fmt);vsnprintf(notification,sizeof(notification),fmt,a);va_end(a);}
static void setup(u32 page,u32 length,u32 grid_index)
{
    api.version=7;api.sample_name=sample_name;api.edit=edit;api.clear=clear;
    begin(69,page,35<<8);sp_browser_close(view);sp_allocate_reset();
    *(u32 *)(view+0x68)=0x9876;*(void **)(view+0x7c)=pages;
    pages[0]=6;grid=grid_index;track=4;selecting=current_pattern=sets=deletes=fail_set=0;notification[0]=0;
    for(u32 p=0;p<2;p++)for(u32 t=0;t<13;t++){
        struct pattern *pattern=&patterns[p][t];pattern->length=length;
        for(u32 s=0;s<64;s++){
            pattern->present[s]=(s%3)==0;
            for(u32 k=0;k<8;k++)pattern->locks[s][k]=k==4?-1:(int)(0x100+p*0x100+t*8+k+s);
        }
    }
    api.version=7;api.sample_name=sample_name;api.edit=edit;api.clear=clear;
    {struct st_smz_api services={3,&api};assert(!sp_input_init(&services));}
}
static int menu_key(u32 code,u32 flags)
{key[3]=code;key[4]=flags;return sp_input_key_pre(view,key,1);}
static void tap(u32 code){assert(menu_key(code,1));assert(menu_key(code,0));}
static void open_menu(void){tap(12);assert(sp_allocate_active(view));}
static void check_allocation(u32 length,u32 grid_index,u32 page,u32 active)
{
    u32 ordinal=0;setup(page,length,grid_index);track=active;
    memcpy(expected,patterns,sizeof(patterns));
    for(u32 s=0;s<length;s++)if(current()->present[s])
        expected[0][track].locks[s][4]=(int)(((ordinal++%(4u<<grid_index))+1)<<8);
    open_menu();assert(!sets);tap(12);
    assert(!sp_allocate_active(view) && sets==ordinal);
    assert(!memcmp(expected,patterns,sizeof(patterns)));
    assert(!strcmp(notification,ordinal?"LOCKS CREATED":"NO TRIGS"));
}
int main(int argc,char **argv)
{
    u32 cases=0;
    for(u32 page=0;page<2;page++)for(u32 g=0;g<5;g++)for(u32 len=1;len<=64;len++)
        for(u32 t=4;t<12;t++){check_allocation(len,g,page,t);cases++;}
    /* Dense patterns wrap after GRID; pre-existing NOTE/SLCE locks on placed
     * and empty steps coexist with unrelated parameters/tracks/patterns. */
    setup(1,64,0);for(u32 s=0;s<64;s++)current()->present[s]=1;
    open_menu();tap(12);for(u32 s=0;s<64;s++)assert(current()->locks[s][4]==(int)(((s%4)+1)<<8));
    setup(0,16,2);current()->locks[0][4]=0;current()->locks[3][4]=0x4000;
    current()->locks[1][4]=0x300;memcpy(expected,patterns,sizeof(patterns));
    {u32 n=0;for(u32 s=0;s<16;s++)if(current()->present[s])expected[0][track].locks[s][4]=(int)((++n)<<8);}
    open_menu();tap(12);assert(!memcmp(expected,patterns,sizeof(patterns)));
    /* RANDOM is bounded by GRID (never NOTE=0), consumes fresh RNG state on
     * every completed action, and remembers the choice on reopening. */
    for(u32 g=0;g<5;g++){
        u32 seen[65]={0},old_seed;setup(0,64,g);
        for(u32 s=0;s<64;s++)current()->present[s]=1;
        open_menu();tap(15);assert(sp_allocate.choice==1);
        for(u32 n=0;n<20;n++){
            memcpy(before_patterns,patterns,sizeof(patterns));old_seed=sp_allocate_random;tap(12);
            assert(!sp_allocate_active(view) && sp_allocate_random!=old_seed);
            for(u32 s=0;s<64;s++){
                u32 value=(u32)current()->locks[s][4];assert(!(value&255) && value>=256 && value<=(4u<<g)*256);
                seen[value>>8]++;before_patterns[0][track].locks[s][4]=(int)value;
            }
            assert(!memcmp(before_patterns,patterns,sizeof(patterns)));
            open_menu();assert(sp_allocate.choice==1);
        }
        for(u32 s=1;s<=(4u<<g);s++)assert(seen[s]);tap(13);
    }
    /* Opening YES, duplicate key-down and held repeat never apply locks.
     * Releasing an unrelated key cannot release a swallowed YES/NO edge. */
    setup(0,16,0);assert(menu_key(12,1) && sp_allocate_active(view));
    assert(menu_key(12,1) && menu_key(12,9) && !sets);assert(!menu_key(17,0));
    assert(menu_key(12,0));tap(13);assert(!sets && !sp_allocate_active(view));
    assert(!menu_key(13,0));open_menu();assert(!menu_key(1,1) && !sp_allocate_active(view));
    /* A write refusal rolls back only SLCE, restoring absent and existing
     * locks. The menu closes as requested and native feedback reports failure. */
    for(u32 fail=1;fail<=6;fail++){
        setup(0,16,0);current()->locks[0][4]=0;current()->locks[3][4]=0x4000;
        memcpy(before_patterns,patterns,sizeof(patterns));fail_set=fail;open_menu();tap(12);
        assert(!sp_allocate_active(view) && !memcmp(before_patterns,patterns,sizeof(patterns)));
        assert(!strcmp(notification,"COULD NOT APPLY"));
    }
    setup(0,16,0);for(u32 s=0;s<64;s++)current()->present[s]=0;
    open_menu();tap(12);assert(!sets && !strcmp(notification,"NO TRIGS"));
    /* Input context changes dismiss before applying. Encoder movement stays
     * modal; idle/draw leave no stale menu on another track or pattern. */
    for(u32 c=0;c<9;c++){
        setup(0,16,0);open_menu();
        switch(c){case 0:track++;break;case 1:*(u32 *)(view+0x90)=1;break;
        case 2:type=66;break;case 3:current_pattern=1;break;case 4:selecting=1;break;
        case 5:trig=1;break;case 6:recording=1;break;case 7:pages[0]=7;break;
        case 8:*(u32 *)view^=4;break;}
        assert(sp_allocate_idle(view) && !sp_allocate_active(view) && !sets);
    }
    setup(1,16,0);open_menu();{u32 ev[6]={0,0,0,4,1,0};assert(sp_input_encoder(view,ev,0x29c) && !sp_browser_active(view));}
    assert(!sp_slice_ui.idle(&api,&sp_slice,view,1));
    for(u32 c=0;c<9;c++){
        setup(0,16,0);
        switch(c){case 0:type=66;break;case 1:type=67;break;case 2:type=68;break;
        case 3:selecting=1;break;case 4:trig=1;break;case 5:recording=1;break;
        case 6:pages[0]=7;break;case 7:*(u32 *)(view+0x88)=0x10;break;
        case 8:*(u32 *)view=0x40241bd4;break;}
        assert(!menu_key(12,1) && !sp_allocate_active(view) && !sets);
    }
    setup(0,16,0);assert(!menu_key(12,3) && !sp_allocate_active(view));
    /* A browser YES accepts the sample and never also opens allocation. */
    setup(0,16,0);sp_browser_begin(&api,view,69,0);tap(12);
    assert(!sp_browser_active(view) && !sp_allocate_active(view));
    setup(0,16,0);open_menu();background();memcpy(left_before,pixels,sizeof(pixels));
    sp_slice_ui.draw(&api,&sp_slice,view,pixels,0);
    for(u32 y=0;y<64;y++)for(u32 x=0;x<24;x++)assert(pixels[y][x]==left_before[y][x]);
    assert(pixels[32][27] && !pixels[23][27]);tap(15);background();sp_slice_ui.draw(&api,&sp_slice,view,pixels,0);
    assert(!pixels[32][27] && pixels[23][27]);
    if(argc>1){
        FILE *out=fopen(argv[1],"wb");assert(out);fprintf(out,"P5\n128 64\n255\n");
        for(int y=63;y>=0;y--)for(int x=0;x<128;x++)fputc(pixels[y][x]?255:0,out);fclose(out);
    }
    printf("PASS: SLICE allocate menu; %u linear track/page/grid/length cases, random range/fresh draws, native lock-only edits, rollback, YES execution closes/reopen retains choice, key edges, context gates and modal raster\n",cases);
    return 0;
}
