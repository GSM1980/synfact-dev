/* modulation-skip-st's decision logic on a native host with a simulated
 * MAIN RAM, code site and UI. The stock modulation is modelled by stock()
 * below (base + amount, per entry, in stock order); the module's real code
 * decides, replays and compares. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char ram[0x10000],site[6],writer[8],ui_drawn[24];static unsigned int paints,locked,writes;
#define RAM(a) (ram+((a)-0x80000000u))
static unsigned int r32(unsigned int a){unsigned char *p=RAM(a);return (unsigned)p[0]<<24|p[1]<<16|p[2]<<8|p[3];}
static unsigned int r16(unsigned int a){unsigned char *p=RAM(a);return (unsigned)p[0]<<8|p[1];}
static void w16(unsigned int a,unsigned int v){unsigned char *p=RAM(a);p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v;}
static void w32(unsigned int a,unsigned int v){w16(a,v>>16);w16(a+2,v&0xffff);}
static void fill(void *p,int a,int b,int c,int d,unsigned int color)
{(void)p;assert(a==18 && b==54 && c==127 && d==63 && !color);}
static void text(void *p,void *font,int x,int y,unsigned int a,const char *fmt,const char *s)
{(void)p;(void)font;assert(x==20 && y==55 && !a && !strcmp(fmt,"%s"));strcpy((char *)ui_drawn,s);paints++;}
static volatile unsigned char *code(unsigned int at)
{
    if(at>=0x400a30c4u && at<0x400a30ccu)return writer+(at-0x400a30c4u);
    assert(at>=0x40097b04u && at<0x40097b0au);return site+(at-0x40097b04u);
}
#define SKIP_HOST 1
#define SKIP_R32(a) r32(a)
#define SKIP_R16(a) r16(a)
#define SKIP_W16(a,v) w16(a,v)
#define SKIP_FILL fill
#define SKIP_TEXT text
#define SKIP_MEM(a) code(a)
#include "../mods/modulation-skip-st/main.c"
void modulation_skip_entry(void){} void modulation_skip_machine_entry(void){}
unsigned int modulation_skip_lock(void){assert(!locked);locked=1;return 0x2000;}
void modulation_skip_unlock(unsigned int sr){assert(locked && sr==0x2000);locked=0;}
void modulation_skip_code16(unsigned int at,unsigned int v){assert(locked);code(at)[0]=(unsigned char)(v>>8);code(at)[1]=(unsigned char)v;writes++;}
void modulation_skip_sync(void){assert(locked);}
static unsigned int runs,filtered_runs,filtered_mask;
/* The stock body's effect: read-modify-write of each entry's destination. */
static void entry(unsigned int base,unsigned int src,unsigned int source)
{
    unsigned int k,e,at;
    for(k=0;k<4;k++){
        e=r32(src+4*k);at=base+2u*(unsigned)(int)(short)(e&0xffff);
        w16(at,(e>>16)?(r16(at)+(e>>16)*source)&0xffff:zero_result(r16(at)));
    }
}
static void stock(void)
{
    unsigned int t,g;runs++;
    for(t=0;t<12;t++)entry(TRACK(t),RAW(t),source(t,0));
    for(t=0;t<13;t++)for(g=1;g<=5;g++)entry(TRACK(t),RAW(t)+0x10*g,source(t,g));
}
static void stock_filtered(unsigned int mask)
{
    unsigned int t,g;filtered_runs++;filtered_mask=mask;
    for(t=0;t<12;t++)if(mask&(1u<<t))entry(TRACK(t),RAW(t),source(t,0));
    for(t=0;t<13;t++)if(mask&(1u<<t))for(g=1;g<=5;g++)entry(TRACK(t),RAW(t)+0x10*g,source(t,g));
}
void modulation_skip_host_group(unsigned int row,int value,unsigned int entries)
{entry(row,entries,(unsigned int)value);}
/* One block: the conversion's effect (base values), then the modulation as
 * the trampoline does it. Returns the working image after the block. */
static unsigned short base_value[0x800];
static void block(unsigned char *out)
{
    unsigned int i,d;
    for(i=0;i<0x754/2;i++)w16(0x80006348u+2*i,base_value[i]);
    d=modulation_skip_decide(WORKING);
    if(d==RUN_STOCK)stock();
    else if(d==RUN_AND_CHECK){stock();modulation_skip_after(WORKING);}
    else if(d==RUN_FILTERED){stock_filtered(modulation_skip_run_mask);modulation_skip_after(WORKING);}
    else if(d==RUN_SPARSE){filtered_runs++;filtered_mask=modulation_skip_run_mask;
                            modulation_skip_sparse(WORKING);modulation_skip_after(WORKING);}
    memcpy(out,RAM(0x80006348u),0x1000);
}
static void reference(unsigned char *out)
{
    unsigned int i;
    for(i=0;i<0x754/2;i++)w16(0x80006348u+2*i,base_value[i]);
    stock();memcpy(out,RAM(0x80006348u),0x1000);
}
int main(void)
{
    unsigned int i,t,b,f;static unsigned char a[0x1000],r[0x1000];
    struct st_machine_ui_api ui;struct st_smz_api api;unsigned char ctrl[64]={0};
    memset(&ui,0,sizeof ui);ui.version=7;api.version=1;api.ui=&ui;
    for(i=0;i<0x800;i++)base_value[i]=(unsigned short)(i*7);
    for(t=0;t<13;t++)for(i=0;i<24;i++)w32(RAW(t)+4*i,i%4?0:((i+t)<<16)|((i*5+t)%0x47));
    for(t=0;t<13;t++){w16(0x80004670u+2*t,3+t);for(i=1;i<5;i++)w16(source_base[i]+2*t,i+t);w32(source_base[5]+4*t,(i+t)<<8);}
    /* Refused: other bytes at either site, nothing written, stock path, header shown. */
    memcpy(writer,"\x4f\xef\xff\xf4\x48\xd7\x0c\x04",8);
    memcpy(site,"\x24\x6f\x00\x40\x26\x0b",6);
    assert(!initialize(&api) && modulation_skip_mode==REFUSED && !writes && !strcmp(line,"MSKIP NO1 H-- S-- E000"));
    assert(modulation_skip_decide(WORKING)==RUN_STOCK && shown());
    memcpy(site,"\x24\x6f\x00\x40\x26\x0a",6);writer[7]=5;
    assert(!initialize(&api) && modulation_skip_mode==REFUSED && !writes && !strcmp(line,"MSKIP NO2 H-- S-- E000"));
    writer[7]=4;
    /* Installed: both jmp abs.l under one exclusion, ON, no header. */
    assert(!initialize(&api) && modulation_skip_mode==ON && writes==6 && site[0]==0x4e && site[1]==0xf9);
    assert(writer[0]==0x4e && writer[1]==0xf9 && writer[6]==0x0c && !shown());
    /* ON: identical to stock in every block; skips once inputs repeat;
     * changes of each input kind force a real run. */
    runs=0;
    for(b=0;b<200;b++){
        if(b==50)w16(0x80004670u+2*4,99);                      /* velocity */
        if(b==80){w32(RAW(7)+0x24,0x00110005u);modulation_skip_machine_dirty[7]=1;modulation_skip_machine_writes+=2;}   /* via 400a30c4 */
        if(b==110)base_value[(TRACK(2)-0x80006348u)/2+((20*5+2)%0x47)]^=0x55;  /* an active destination's base */
        if(b==140)w16(0x800046f8u+2*9,40);                     /* source table */
        block(a);
        { unsigned int keep=runs;reference(r);runs=keep; }
        /* reference() overwrote working with the same result; compare. */
        assert(!memcmp(a,r,0x1000));
    }
    assert(modulation_skip_errors==0 && modulation_skip_skips>150 && modulation_skip_blocks==200);
    assert(filtered_runs>0 && modulation_skip_partial_runs==filtered_runs);
    /* A saved zero-depth velocity assignment may still name FREQ/RES. Editing
     * FREQ must leave the matrix skipped; another active assignment to FREQ
     * must restore the dependency, and clearing it must remove it again. */
    w32(RAW(2)+4,0x0000001cu);w32(RAW(2)+8,0x0000001du);
    modulation_skip_machine_dirty[2]=1;modulation_skip_machine_writes+=2;block(a);
    base_value[(TRACK(2)-WORKING)/2+28]^=0x55;count=0;t=runs;block(a);
    assert(runs==t);
    {unsigned int keep=runs;reference(r);runs=keep;}assert(!memcmp(a,r,0x1000));
    base_value[(TRACK(2)-WORKING)/2+28]=0xa5a5u;count=0;t=runs;block(a);
    assert(runs==t && r16(TRACK(2)+56u)==0x7fffu);
    {unsigned int keep=runs;reference(r);runs=keep;}assert(!memcmp(a,r,0x1000));
    base_value[(TRACK(2)-WORKING)/2+28]=0xffffu;count=0;t=runs;block(a);
    assert(runs==t && r16(TRACK(2)+56u)==0u);
    {unsigned int keep=runs;reference(r);runs=keep;}assert(!memcmp(a,r,0x1000));
    base_value[(TRACK(2)-WORKING)/2+28]=0x4000u;
    w32(RAW(2)+12,0x0001001cu);modulation_skip_machine_dirty[2]=1;modulation_skip_machine_writes+=2;block(a);
    base_value[(TRACK(2)-WORKING)/2+28]^=0x33;count=0;t=runs;f=filtered_runs;block(a);
    assert(runs==t+1 || (filtered_runs==f+1 && (filtered_mask&(1u<<2))));
    {unsigned int keep=runs;reference(r);runs=keep;}assert(!memcmp(a,r,0x1000));
    w32(RAW(2)+12,0x0000001cu);modulation_skip_machine_dirty[2]=1;modulation_skip_machine_writes+=2;block(a);
    base_value[(TRACK(2)-WORKING)/2+28]^=0x77;count=0;t=runs;block(a);
    assert(runs==t);
    {unsigned int keep=runs;reference(r);runs=keep;}assert(!memcmp(a,r,0x1000));
    /* Machine data changed by an unknown writer: found within 64 blocks, ON turns off. */
    w32(RAW(5)+0x34,0x00220007u);
    for(b=0;b<64 && modulation_skip_mode==ON;b++){block(a);reference(r);}
    assert(modulation_skip_errors==1 && modulation_skip_mode==OFF && shown());
    modulation_skip_errors=0;modulation_skip_mode=ON;valid_mask=0;block(a);
    /* A wrong replay is found at the next check and turns ON off. */
    for(t=0;t<13;t++)for(i=0;i<used[t];i++)if(post[t][i]){post[t][i]^=1;goto changed_on;}
changed_on:
    count=63;block(a);reference(r);
    assert(modulation_skip_errors==1 && modulation_skip_mode==OFF);
    assert(modulation_skip_decide(WORKING)==RUN_STOCK);
    /* An active signed destination outside its row can alias another track:
     * the filtered kernel must refuse it and run the complete stock body. */
    modulation_skip_mode=ON;w32(RAW(3)+8,0x0002ffffu);
    modulation_skip_machine_dirty[3]=1;modulation_skip_machine_writes+=2;t=runs;block(a);
    assert(runs==t+1 && (unsafe_mask&(1u<<3)));
    {unsigned int keep=runs;reference(r);runs=keep;}assert(!memcmp(a,r,0x1000));
    w32(RAW(3)+8,0x00020009u);modulation_skip_machine_dirty[3]=1;modulation_skip_machine_writes+=2;
    /* VERIFY never skips and counts a difference without stopping. */
    modulation_skip_mode=VERIFY;valid_mask=0;
    block(a);block(a);for(t=0;t<13;t++)for(i=0;i<used[t];i++)if(post[t][i]){post[t][i]^=1;goto changed_verify;}
changed_verify:
    t=modulation_skip_skips;block(a);
    assert(modulation_skip_skips==t && modulation_skip_errors==2 && modulation_skip_mode==VERIFY);
    /* Header: page 3 of 3, percentages per window. */
    ticks=0;for(i=0;i<239;i++)skip_tick(ctrl);paints=0;skip_draw(0,0);assert(!paints);
    skip_tick(ctrl);skip_draw(0,0);assert(paints==1 && !strncmp((char *)ui_drawn,"MSKIP VER",9) && ctrl[0x20]);
    puts("PASS: refusal, install, ON equal to stock with skips, input/base changes rerun, periodic check "
         "turns ON off, VERIFY never skips, header page");
    return 0;
}
