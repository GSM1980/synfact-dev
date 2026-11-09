/* Native-host tests of the production UI/receiver; no CPU emulation. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syntakt/modules.h>
typedef unsigned int T;
static unsigned short values[28],fm_ext[128],FM_OFFSETS[32];
static unsigned char locked_values[28],screen[128*64],view[0x100];
static T get_calls,pix_calls,held_trig,held_page,held_selection,edits,clears,invalidated,key_code=0x2b,key_down=1;
static int event_delta;
static T event_pressed,increment_calls,delta_calls,edit_ids[8];static int edit_deltas[8];
static T getter(void *v,T id,unsigned char *locked)
{
    T n;(void)v;
#ifdef FM_REFERENCE
    n=id>=0x239?id-0x239+24:id-0x201;
#else
    n=id-0x219;
#endif
    assert(n<28);get_calls++;*locked=locked_values[n];return values[n];
}
static T machine_type=65;
static T type(void *v){(void)v;return machine_type;}
/* The native graphics context as PIX (400f7140) reads it: clip size at
 * +4/+8, words per column at +12, the column-major 1-bit plane at +16 (bit 31
 * = row 0 of each word). The UI writes colour-1 pixels into it directly; the
 * doubles below draw into the same plane. Frames are compared as bytes. */
static T guarded[16+128*2+16];       /* zero guards catch writes past the plane */
#define plane (guarded+16)
#define PLANE_BYTES (128*2*sizeof(T))
static struct {T unused,width,height,words;T *bits;} frame={0,128,64,2,plane};
static void guards_clear(void)
{T i;for(i=0;i<16;i++)assert(!guarded[i] && !guarded[16+256+i]);}
_Static_assert(sizeof(T *)==4,"32-bit host: the UI reads the context as 32-bit words");
static void pixel(void *ctx,int x,int y,T color)
{T *p,bit;assert(ctx==&frame);if(x<0 || x>=128 || y<0 || y>=64)return;
 p=plane+2*x+(y>>5);bit=0x80000000u>>(y&31);
 if(color==0xffffffffu)*p^=bit;else if(color)*p|=bit;else *p&=~bit;}
/* The UI's own PIX calls are counted; the doubles below draw directly. */
static void pixel_call(void *ctx,int x,int y,T color){pix_calls++;pixel(ctx,x,y,color);}
static void draw_fill(void *c,int x0,int y0,int x1,int y1,T color)
{int x,y;for(y=y0;y<=y1;y++)for(x=x0;x<=x1;x++)pixel(c,x,y,color);}
static void rect(void *c,int x0,int y0,int x1,int y1,T color)
{draw_fill(c,x0,y0,x1,y0,color);draw_fill(c,x0,y1,x1,y1,color);draw_fill(c,x0,y0,x0,y1,color);draw_fill(c,x1,y0,x1,y1,color);}
static void text_stub(void *a,void *c,int x,int y,T align,const char *s,const char *f)
{(void)a;(void)c;(void)x;(void)y;(void)align;(void)s;(void)f;assert(0);}
static void toggle(void *a,T v,void *c,int x,int y,T mode)
{/* Shared native renderer is borrowed unchanged. Compare its exact arguments
   * through a deterministic test glyph; firmware bitmap execution is excluded. */
 assert(!a && !mode);rect(c,x+1,y,x+17,y+16,1);
 if(v)draw_fill(c,x+4,y+3,x+14,y+13,1);}
static void indicator(void *v,void *ctx,T pages,T page,T last)
{(void)v;(void)ctx;assert(pages==4 && page<4 && last==7);}
#define GET getter
#define TYPE type
#define PIX pixel_call
#define FILL draw_fill
#define RECT rect
#define TEXT text_stub
#define TOGGLE toggle
#define FM_PAGE_INDICATOR indicator
static T policy_words[5],increments[7];
static T expected_policy=0x13,expected_knob=3,expected_id=0x21c,held_func;
static T policy_mock(T id){assert(id==expected_policy);policy_words[4]=(T)increments;return (T)policy_words;}
static int delta_mock(void *e,T a,T b){(void)e;(void)a;(void)b;delta_calls++;return event_delta;}
static T pressed_mock(void *e){(void)e;return event_pressed;}
static int increment_mock(void *v,T k,int delta,void *inc,T p)
{assert(v==view+0x94 && k==expected_knob && p==event_pressed && !memcmp(inc,increments,sizeof(increments)));increment_calls++;return delta;}
static T trig_mock(void *c){(void)c;return held_trig;}
static T held_mock(T k){assert(k==1 || k==2 || k==0x12);return k==1?held_func:k==2?held_selection:held_page;}
/* The native FUNC snap gate (400f6ad4 arm, 400f6b02 blocked, shared flag). */
static unsigned char snap_active;static T snap_arms,snap_blocked;
static void snap_arm_mock(void *s,T k){assert(s==view+0x94 && k==expected_knob);snap_arms++;}
static T snap_blocked_mock(void *s,T k){assert(s==view+0x94 && k==expected_knob);return snap_blocked;}
static T key_mock(void *e){(void)e;return key_code;}
static T down_mock(void *e){(void)e;return key_down;}
static void invalidate_mock(void *v){assert(v==view);invalidated++;}
static T set_value,codec_registered;
static void set_mock(void *p,T id,T raw,T track,T f7,T f6)
{(void)p;assert(id==0x230 && track==4 && f7==7 && f6==6);set_value=raw;}
#define FM_POLICY policy_mock
#define FM_EVENT_DELTA delta_mock
#define FM_EVENT_PRESSED pressed_mock
#define FM_INCREMENT increment_mock
#define FM_TRIG_HELD trig_mock
#define FM_KEY_HELD held_mock
#define FM_SNAP_ARM snap_arm_mock
#define FM_SNAP_BLOCKED snap_blocked_mock
#define FM_SNAP_ACTIVE snap_active
#define FM_EVENT_KEY key_mock
#define FM_EVENT_DOWN down_mock
#define FM_INVALIDATE invalidate_mock
#define FM_SET_VALUE set_mock
#ifdef FM_REFERENCE
#include "../out/fm-tests/reference-ui.c"
#else
#include "../mods/fm-tone-st/ui.c"
#endif
#include "../mods/fm-tone-st/main.c"
#include "../out/fm-tests/reference-controls.h"
static const unsigned short *extended_mock(const void *p){return p;}
#define FM_EXTENDED extended_mock
#include "../mods/fm-tone-st/receive.c"

static void edit_mock(void *v,T id,int delta,T knob)
{assert(v==view && id==expected_id && knob==expected_knob && edits<8);edit_ids[edits]=id;edit_deltas[edits]=delta;values[id-0x219]+=delta;edits++;}
static void clear_mock(void *v,T id,T page,T knob){(void)v;(void)id;(void)page;(void)knob;assert(0);}
static int bind_mock(T t,int (*enc)(void *,void *,T),void (*k)(void *,void *))
{(void)t;(void)enc;(void)k;assert(0);return 0;}
static int codec_mock(const struct st_machine_action_codec *c){(void)c;assert(0);return 0;}
/* Reference state for one stored RATIO B: legacy fast (0x204) / slow (0x218)
 * digits; the donor algorithm reads them through the production getter. */
static T combined(T fast,T slow){fast>>=8;slow>>=8;return FM_RATIO_STEP*(19*(slow>18?18:slow)+(fast>18?18:fast));}
#ifndef FM_REFERENCE
static int ref_fast,ref_slow;static T ref_edits;
static void reference_dispatch(void *v,T id,int delta,T knob)
{assert(v==view && (id==0x204u || id==0x218u) && knob==3);if(id==0x204u)ref_fast+=delta;else ref_slow+=delta;ref_edits++;}
#include "../out/fm-tests/reference-ratio.h"
static void verify_ratio_reference(void)
{
    const int deltas[]={0,-1,1,-128,128,-256,256,-768,768,-131072,131072};
    T pair,pressed,mode,j,result,a,count,comparisons=0;
    *(T *)(view+0x88)=0x14;*(T *)(view+0x90)=0;machine_type=65;
    for(pair=0;pair<=360;pair++)for(pressed=0;pressed<2;pressed++)for(mode=0;mode<3;mode++)
    for(j=0;j<sizeof(deltas)/sizeof(deltas[0]);j++){
        event_delta=deltas[j];event_pressed=pressed;held_trig=mode?0xff:0;
        held_selection=mode==1;held_page=mode==2;
        values[3]=pair*FM_RATIO_STEP;edits=increment_calls=delta_calls=0;
        result=encoder(view,0,0x21c);a=values[3];count=edits;
        assert(increment_calls==1 && delta_calls==1);
        values[3]=pair*FM_RATIO_STEP;ref_fast=(int)(pair%19)<<8;ref_slow=(int)(pair/19)<<8;
        ref_edits=increment_calls=delta_calls=0;
        assert(reference_ratio_encoder(view,0)==result && !reference_depth);
        /* Same combination; one stored control edits once (plus one lock seed)
         * where the donor edited both halves (plus two seeds). */
        assert(a==combined((T)ref_fast,(T)ref_slow) && 2*count==ref_edits);
        assert(increment_calls==1 && delta_calls==1);
        comparisons++;
    }
    assert(comparisons==23826);
    puts("PASS: 23826 cases against original Synfact RATIO B C on one stored control; 361 combinations, both directions/carry/clamp, pressed encoder, trig/page lock seeding");
    event_pressed=0;held_trig=held_selection=held_page=0;edits=0;
}
#endif
#ifndef FM_REFERENCE
/* List item 29: FUNC + encoder snaps, through the native increment, gate
 * and edit route. Every start value of each control, both directions. */
static void verify_func_snaps(void)
{
    static const struct {T id,page,knob,policy,min,max;int n;T list[5];} c[]={
        {0x21d,0,4,0x1f1,0x2500,0x5900,5,{0x2700,0x3300,0x3f00,0x4b00,0x5700}},
        {0x220,0,7,0x13,0,0x7f00,3,{0,0x4000,0x7f00}},
        {0x230,1,0,0x1f1,0,0x7ffe,3,{0,0x4000,0x7ffe}},
        {0x232,1,1,0x1f1,0,0x7ffe,3,{0,0x4000,0x7ffe}},
        {0x233,1,2,0x1f1,0,0x7ffe,3,{0,0x4000,0x7ffe}},
        {0x234,1,3,0x1f1,0,0x7ffe,3,{0,0x4000,0x7ffe}}};
    T k,raw,cases=0;int d,j;
    *(T *)(view+0x88)=0x14;machine_type=65;held_trig=held_selection=held_page=0;
    for(k=0;k<6;k++){
        *(T *)(view+0x90)=c[k].page;expected_knob=c[k].knob;expected_policy=c[k].policy;expected_id=c[k].id;
        /* Without FUNC the native editor keeps the turn. */
        held_func=0;event_delta=256;edits=0;assert(!encoder(view,0,c[k].id) && !edits);
        /* On the other page the control is not there. */
        held_func=1;*(T *)(view+0x90)=c[k].page^1;assert(!encoder(view,0,c[k].id) && !edits);
        *(T *)(view+0x90)=c[k].page;
        for(raw=c[k].min;raw<=c[k].max;raw+=raw+64>c[k].max && raw<c[k].max?c[k].max-raw:64)
        for(d=-1;d<=1;d+=2){
            T want=raw;
            if(d>0){for(j=0;j<c[k].n;j++)if(c[k].list[j]>raw){want=c[k].list[j];break;}}
            else for(j=c[k].n-1;j>=0;j--)if(c[k].list[j]<raw){want=c[k].list[j];break;}
            values[c[k].id-0x219]=(unsigned short)raw;event_delta=d*256;edits=0;snap_arms=0;snap_active=0;snap_blocked=0;
            assert(encoder(view,0,c[k].id) && edits==1 && values[c[k].id-0x219]==want);
            assert(snap_active==1 && snap_arms==2);              /* entry arm, then re-arm */
            snap_arms=0;edits=0;snap_blocked=1;                  /* gate closed: no edit */
            values[c[k].id-0x219]=(unsigned short)raw;
            assert(encoder(view,0,c[k].id) && !edits && values[c[k].id-0x219]==raw && !snap_arms);
            snap_blocked=0;cases++;
        }
        /* No accepted increment: consumed, nothing edited. */
        event_delta=0;edits=0;assert(encoder(view,0,c[k].id) && !edits);
        /* A held trig with the page key: the lock is seeded first. */
        held_trig=held_page=1;values[c[k].id-0x219]=(unsigned short)c[k].min;event_delta=256;edits=0;
        assert(encoder(view,0,c[k].id) && edits==2 && !edit_deltas[0] && values[c[k].id-0x219]==c[k].list[c[k].list[0]==c[k].min?1:0]);
        held_trig=held_page=0;
    }
    held_func=0;expected_knob=3;expected_policy=0x13;expected_id=0x21c;*(T *)(view+0x90)=0;
    printf("PASS: %u FUNC snaps: HARM -24..+24 through 0, MIX -64/0/+63, RATIO OFFSETs -1/0/+0.999; gate, locks, pages\n",cases);
}
#endif
static void verify_controls(void)
{
    T i;unsigned short input[24];
    assert(fm_tone.type==65 && fm_tone.category==10 && fm_tone.params==28);
    for(i=0;i<28;i++)assert(fm_tone.parameter[i].initial>=fm_tone.parameter[i].min &&
                               fm_tone.parameter[i].initial<=fm_tone.parameter[i].max);
    for(i=0;i<28;i++)if(i!=3 && i!=23 && i!=24)assert(fm_tone.parameter[i].min==reference_min[i] &&
        fm_tone.parameter[i].max==reference_max[i] && fm_tone.parameter[i].initial==reference_initial[i]);
    /* RATIO B: the donor pair as one control; OFFSET C moved to index 23. */
    assert(!fm_tone.parameter[3].min && fm_tone.parameter[3].max==combined(reference_max[3],reference_max[23]) &&
           fm_tone.parameter[3].initial==combined(reference_initial[3],reference_initial[23]));
    assert(fm_tone.parameter[23].min==reference_min[24] && fm_tone.parameter[23].max==reference_max[24] &&
           fm_tone.parameter[23].initial==reference_initial[24]);
    assert(!fm_tone.parameter[24].min && !fm_tone.parameter[24].max && !fm_tone.parameter[24].initial);
    for(i=0;i<24;i++)input[i]=0xffffu-i;
    memset(fm_ext,0,sizeof(fm_ext));memset(FM_OFFSETS,0,sizeof(FM_OFFSETS));
    for(i=0;i<8;i++){
        assert(fm_receive_controls(i,input));assert(!fm_receive_controls(i,input));
        assert(!memcmp(fm_ext+16*i,input,32));
        for(T k=0;k<4;k++)assert(FM_OFFSETS[4*i+k]==(input[k?16+k:15]&0x7ffe));
    }
    assert(!fm_receive_controls(8,input) && !fm_receive_controls(0,0));
    /* AUDIO withholds a row until one was received for FM on that track:
     * every value, the ratio offsets included, then stays as it was. */
    {unsigned short ext[sizeof fm_ext/2],offsets[sizeof FM_OFFSETS/2];
     memcpy(ext,fm_ext,sizeof ext);memcpy(offsets,FM_OFFSETS,sizeof offsets);
     for(i=0;i<8;i++)assert(!fm_receive_controls(i,0));
     assert(!memcmp(ext,fm_ext,sizeof ext) && !memcmp(offsets,FM_OFFSETS,sizeof offsets));}
    input[7]^=1;assert(fm_receive_controls(3,input) && fm_ext[3*16+7]==input[7]);
    input[16]^=2;assert(!fm_receive_controls(3,input));               /* placeholder word ignored */
#ifndef FM_REFERENCE
    assert(fm_tone_ui.pages==4 && fm_tone_ui.layout[31]==0xffffffffu);
    for(i=0;i<32;i++)assert(layout[i]==0xffffffffu || (layout[i]<28 && layout[i]!=24));
    assert(layout[8]==23);
    assert(!strcmp(format(0,&fm_tone,0,0x700),"8"));
    assert(!strcmp(format(0,&fm_tone,19,0x400),"A+B2"));
    assert(!strcmp(format(0,&fm_tone,23,0x4000),"0.000"));
    assert(!strcmp(format(0,&fm_tone,23,0),"-1.000"));
    assert(!format(0,&fm_tone,24,0));
    assert(!strcmp(format(0,&fm_tone,4,0x3f00),"0.00"));
    assert(!strcmp(format(0,&fm_tone,3,FM_RATIO_STEP*60),"1.00/1.00"));
    assert(!strcmp(format(0,&fm_tone,3,FM_RATIO_STEP*43),"3.00/0.75"));
    assert(!strcmp(format(0,&fm_tone,3,0x7f00),"16.0/16.0"));          /* modulation beyond 360 clamps */
    {
        struct st_machine_ui_api ui={4,0,0,0,0,ST_UI_FORMAT_POPUP,edit_mock,clear_mock,bind_mock,codec_mock};
        struct st_smz_api api={2,&ui};
        assert(!strcmp(format(&ui,&fm_tone,3,FM_RATIO_STEP*43),"0.75 RB2=3.00"));
        assert(!strcmp(fm_tone.parameter[3].name,"RATIO RB1"));        /* popup: RATIO RB1=<B1> RB2=<B2> */
        assert(!initialize(&api));ui.version=3;assert(initialize(&api));ui.version=4;
        api.version=1;assert(initialize(&api));api.version=2;
        assert(fm_tone_module.count==1 && subscriptions[0].event==(ST_EV_ENC|ST_EV_VIEW));
        *(T *)(view+0x88)=0x14;*(T *)(view+0x90)=0;
        machine_type=66;assert(!encoder(view,0,0x21c));machine_type=65;
        *(T *)(view+0x88)=0;assert(!encoder(view,0,0x21c));*(T *)(view+0x88)=0x14;
        for(i=0;i<=360;i++)for(int direction=-1;direction<=1;direction+=2){
            values[3]=i*FM_RATIO_STEP;event_delta=direction*256;edits=0;
            assert(encoder(view,0,0x21c) && edits==1);
            int n=(int)i+direction;if(n<0)n=0;if(n>360)n=360;
            assert(values[3]==(T)n*FM_RATIO_STEP);
        }
        values[3]=0x300;edits=0;assert(encoder(view,0,0x21c) && values[3]==9*FM_RATIO_STEP); /* off-grid value */
        held_trig=held_selection=1;values[3]=18*FM_RATIO_STEP;edits=0;event_delta=256;
        assert(encoder(view,0,0x21c) && edits==2 && !edit_deltas[0] && values[3]==19*FM_RATIO_STEP);
        held_trig=held_selection=0;
        *(T *)(view+0x90)=1;assert(!encoder(view,0,0x21c));
        verify_ratio_reference();
        verify_func_snaps();
    }
#endif
}
#ifndef FM_REFERENCE
/* The UI's direct colour-1 writes against the native PIX double, clipping
 * included: every position from beyond the left/top edges to beyond the
 * right/bottom ones, single dots and columns of 1..32 rows. */
static void verify_direct_writes(void)
{
    static T expected[128*2];
    const T patterns[]={0x80000000u,0xffffffffu,0xa5a5a5a5u,0xf0000001u,0x12345678u};
    int x,y;T k,n,j,bits,cases=0;
    for(x=-3;x<=130;x++)for(y=-40;y<=70;y+=3)for(k=0;k<5;k++)for(n=1;n<=32;n+=k+6){
        bits=patterns[k];if(n<32)bits&=~(0xffffffffu>>n);
        memset(plane,0,PLANE_BYTES);
        for(j=0;j<n;j++)if(bits&(0x80000000u>>j))pixel(&frame,x,y+(int)j,1);
        memcpy(expected,plane,PLANE_BYTES);
        memset(plane,0,PLANE_BYTES);column(&frame,x,y,bits,n);
        assert(!memcmp(expected,plane,PLANE_BYTES));guards_clear();
        memset(plane,0,PLANE_BYTES);for(j=0;j<n;j++)if(bits&(0x80000000u>>j))dot(&frame,x,y+(int)j);
        assert(!memcmp(expected,plane,PLANE_BYTES));guards_clear();cases++;
    }
    printf("PASS: %u direct dot/column writes equal native PIX, edges and clipping included\n",cases);
}
#endif
int main(int argc,char **argv)
{
    T page,algo,variant,i,gets[4]={0},pixels[4]={0};FILE *f;assert(argc==2);verify_controls();
#ifndef FM_REFERENCE
    verify_direct_writes();
#endif
    f=fopen(argv[1],"wb");assert(f);
    *(T *)(view+0x88)=0x14;
    for(page=0;page<4;page++)for(algo=0;algo<8;algo++)for(variant=0;variant<8;variant++){
        for(i=0;i<28;i++){
            values[i]=variant%4==0?reference_initial[i]:variant%4==1?reference_min[i]:variant%4==2?reference_max[i]:(reference_min[i]+reference_max[i])/2;
            locked_values[i]=variant>=4;
        }
#ifndef FM_REFERENCE
        /* The same logical state in the stored layout: one RATIO B, OFFSET C at 23. */
        values[3]=(unsigned short)combined(values[3],values[23]);values[23]=values[24];values[24]=0;
#endif
        values[0]=algo*256;*(T *)(view+0x90)=page;
        /* Sentinel native track rail/header must survive the custom UI. The
         * rest starts as a pattern, not blank: a renderer that skips writes
         * outside the area it cleared itself cannot match the donor. */
        for(i=0;i<sizeof(screen);i++)screen[i]=i%128<19 || i/128>=54 || (i%128+i/128)%3==0;
        memset(plane,0,PLANE_BYTES);
        for(i=0;i<sizeof(screen);i++)if(screen[i])pixel(&frame,i%128,i/128,1);
        get_calls=pix_calls=0;
#ifdef FM_REFERENCE
        fm_ui_draw(view,&frame);
#else
        /* The renderer uses the core-provided page, not its own view lookup. */
        *(T *)(view+0x90)=(page+1)&3u;
        fm_ui_draw(view,&frame,page);
#endif
        guards_clear();
        for(i=0;i<sizeof(screen);i++)screen[i]=plane[2*(i%128)+i/128/32]>>(31-i/128%32)&1u;
        gets[page]+=get_calls;pixels[page]+=pix_calls;
        for(i=0;i<sizeof(screen);i++)if(i%128<19 || i/128>=54)assert(screen[i]);
        assert(fwrite(screen,1,sizeof(screen),f)==sizeof(screen));
    }
    assert(!fclose(f));
    /* Machine-readable per-page totals over 64 frames (test_st_fm.py). */
    for(page=0;page<4;page++)printf("CALLS %u GET %u PIX %u\n",page,gets[page],pixels[page]);
#ifdef FM_REFERENCE
    puts("PASS: 256 original Synfact drawing frames with shared native-renderer doubles");
#else
    puts("PASS: 256 UI frames, 28 controls, eight tracks, one stored RATIO B (carry/clamp, popup), OFFSET C at 23");
#endif
    return 0;
}
