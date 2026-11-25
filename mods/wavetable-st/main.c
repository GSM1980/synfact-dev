/* SPDX-License-Identifier: GPL-2.0-or-later
 * SP WAVETBL (type 72): two wavetable oscillators with their own ANIM
 * modulators, after the Tonverk's Wavefinder (docs/SP-WAVETBL.md). MAIN
 * owns the descriptor and the SYN pages only; the sound is audio.c.
 * Page 1 is osc 1, page 2 osc 2, in the Tonverk's knob order. Control 16 is
 * hidden: the core sends AUDIO the tempo in it (core 2.8).
 */
#include <syntakt/machines.h>
typedef st_u32 u32;
#define P(label,name,hi,init) {label,name,0,hi,init,0x13}
#define OSC(n,lev) \
    P("LEV","LEVEL OSC " n,0x7f00,lev),P("POS","POSITION OSC " n,0x7f00,0), \
    P("SLOT","WAVETABLE OSC " n,0x7f00,0x4000),P("SPD","ANIM SPEED OSC " n,0x7f00,0x4000), \
    P("A.LEV","ANIM LEV OSC " n,0x7f00,0x4000),P("A.POS","ANIM POS OSC " n,0x7f00,0x4000), \
    P("ANIM","ANIM SH OSC " n,0x0b00,0)
/* The long names title the value pop-up as "name=value" on one row: short,
 * so that the value fits. ANIM starts on EXP D, as on the Tonverk. */
static const struct core_machine_param wt_controls[17]={
    {"TUNE","TUNE",0x1000,0x7000,0x4000,0x13},OSC("1",0x6400),
    P("DTUN","DETUNE OSC 2",0x7f00,0x4000),OSC("2",0),
    {"TMPO","TEMPO",0,0xffff,14400,ST_MACHINE_TEMPO_PARAMETER}
};
const struct core_machine_def wt_machine={72,17,"SP WAVETBL","WTBL","SYN",wt_controls,ST_MACHINE_NO_INIT,44,11,14};

enum {INTEGER,TUNE,DETUNE,DECIMAL,SAMPLE,BIPOLAR,SHAPE};
static const unsigned char wt_kinds[8]={TUNE,INTEGER,DECIMAL,SAMPLE,INTEGER,BIPOLAR,BIPOLAR,SHAPE};
static u32 kind(const struct core_machine_def *m,u32 index)
{
    if(m->type!=72 || index>=16)return INTEGER;
    if(index==8)return DETUNE;
    return wt_kinds[index&7u];
}
static const char *const wt_shapes[12]={"EXP D","RAMP D","TRI","RAMP U","EXP U","L RAMP D",
    "L TRI","L SQUARE","L RAMP U","RANDOM","S&H","WAVE"};
static char text[24];
static char *number(char *p,u32 n)
{if(n>=100)*p++='0'+n/100;if(n>=10)*p++='0'+n/10%10;*p++='0'+n%10;*p=0;return p;}
static const char *format(const struct st_machine_ui_api *api,const struct core_machine_def *m,u32 index,u32 raw)
{
    u32 k=kind(m,index),n=raw>>8,f;char *p=text;const char *s;int v;
    if(k==INTEGER)return 0;
    if(k==SAMPLE){
        if(api->version>=2 && api->format_context==ST_UI_FORMAT_CONTROL)return m->parameter[index].label;
        if(!api->sample_name(n<128?n:127,text)){s="NO SAMPLE";while(*s)*p++=*s++;*p=0;}
        return text;
    }
    if(k==SHAPE){s=wt_shapes[n<12?n:11];while(*s)*p++=*s++;*p=0;return text;}
    if(k==DECIMAL){p=number(p,n);f=((raw&255u)*100u)>>8;*p++='.';*p++='0'+f/10;*p++='0'+f%10;*p=0;return text;}
    v=(int)raw-0x4000;
    if(k==TUNE){                                     /* semitones, two decimals */
        *p++=v<0?'-':'+';raw=v<0?(u32)-v:(u32)v;p=number(p,raw>>8);
        f=((raw&255u)*100u)>>8;*p++='.';*p++='0'+f/10;*p++='0'+f%10;*p=0;return text;
    }
    v>>=8;                                           /* DETUNE and BIPOLAR: -64..+63 */
    if(v)*p++=v<0?'-':'+';
    number(p,(u32)(v<0?-v:v));return text;
}
/* No native dials: page.c draws every control. A dial the firmware paints
 * when a knob turns reaches x 125..127, past the page's own clearing (the
 * page indicator's column), and stayed there as a stray mark. */
static int dial(const struct st_machine_ui_api *api,const struct core_machine_def *m,u32 index,const struct st_machine_dial *d)
{(void)api;(void)d;return m->type==72 && index<16;}
/* TUNE and POS keep the native fine encoder (Q8 fractions); the rest is coarse. */
static u32 policy(u32 i){return i==0 || i==2 || i==10?0x1f1:0xffffffffu;}
static const u32 wt_layout[16] __attribute__((aligned(4)))={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
/* page.c: the pages drawn as the Tonverk's Wavefinder. */
void wt_draw(const struct st_machine_ui_api *,const struct core_machine_def *,void *,void *,u32);
int wt_idle(const struct st_machine_ui_api *,const struct core_machine_def *,void *,u32);
const struct st_machine_ui wt_ui={72,2,wt_layout,wt_draw,wt_idle,dial,format,policy};
