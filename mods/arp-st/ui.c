/* SPDX-License-Identifier: GPL-2.0-or-later
 * The two ARP TRIG pages (1.8), in the native page's layout (ui.h). The step
 * graph keeps the Digitone ARP's marks (filled steps on octaves, hollow ones
 * between, a dash for a muted step, the length marker, the adaptive scale),
 * smaller, in the bottom row. Resources and fonts are the adapter's. */
#include "ui.h"
static char *decimal(char *s,int n)
{
    char *p=s;
    if(n<0){*p++='-';n=-n;}
    if(n>=100)*p++='0'+n/100;
    if(n>=10)*p++='0'+n/10%10;
    *p++='0'+n%10;*p=0;return s;
}
static int absolute(int n){return n<0?-n:n;}
static unsigned int length(const char *s){unsigned int n=0;while(s[n])n++;return n;}
#define TEXT(f,x,y,a,s) c->text(c->context,f,x,y,a,s)
#define SHAPE(k,x,y,x2,y2,col) c->shape(c->context,k,x,y,x2,y2,col)
/* ARP's 3x5 font: rows top to bottom, three bits each (4 = left column). */
static const char small_chars[]=" #+-./0123456789ABCDEFGLNOPRTUWY";
static const unsigned short small_glyphs[]={
    0x0000, 0x5f7d, 0x05d0, 0x01c0, 0x0002, 0x12a4, 0x7b6f, 0x2c97,
    0x73e7, 0x73cf, 0x5bc9, 0x79cf, 0x79ef, 0x7249, 0x7bef, 0x7bcf,
    0x2bed, 0x6bae, 0x3923, 0x6b6e, 0x79a7, 0x79a4, 0x396b, 0x4927,
    0x5fed, 0x2b6a, 0x6ba4, 0x6bad, 0x7492, 0x5b6f, 0x5bfd, 0x5a92};
void arp_small_text(void *context,void (*fill)(void *,int,int,int,int,unsigned int),
                    int x,int y,unsigned int align,const char *s)
{
    unsigned int n=length(s),i,k,row,col,g;int at;
    if(align==2)x-=ARP_SMALL_WIDTH(n)/2;
    for(i=0;i<n;i++){
        for(k=0;small_chars[k] && small_chars[k]!=s[i];k++);
        g=small_chars[k]?small_glyphs[k]:0;
        at=x+4*(int)i;
        for(col=0;col<3;col++)for(row=0;row<5;row++)
            if(g>>(14-3*row-col)&1u)fill(context,at+(int)col,y+4-(int)row,at+(int)col,y+4-(int)row,1);
    }
}
/* One top-row control: label, box and value. A control whose values can be
 * three or four characters (MODE, SPEED, NOTE, NOT2..NOT4) always takes ARP's
 * 3x5 font; RANGE (at most two) the native value font. */
static void control(const struct arp_canvas *c,unsigned int i,const char *label,const char *value,unsigned int font)
{
    int x=ARP_COL(i);
    TEXT(ARP_FONT_LABEL,x+9,27,2,label);
    SHAPE(ARP_RECT,x,34,x+18,50,1);
    if(value)TEXT(font,x+9,font==ARP_FONT_SMALL?40:39,2,value);
}
/* A locked value is inverted, as the native pages show it (XOR fill). */
static void invert(const struct arp_canvas *c,unsigned int i)
{int x=ARP_COL(i);SHAPE(ARP_FILL,x,34,x+18,50,0xffffffffu);}
/* The step graph: steps at x 26..86, 4 px apart; marks in rows 0..3; notes
 * around row 15, up to 9 rows either way; OFS and LEN (F) at x 93..123. */
#define STEP_X(i) (26+4*(int)(i))
#define CENTRE 15
#define REACH 9
void arp_draw(const struct arp_screen *v,const struct arp_canvas *c)
{
    const struct arp_settings *p=v->settings;
    static const char *modes[]={"OFF","TRUE","UP","DOWN","CYCL"};
    char value[12];unsigned int i;int scale=1,height[16],pad,selected,x,off,extent;
    control(c,0,"MODE",modes[p->mode<=4?p->mode:0],ARP_FONT_SMALL);
    control(c,1,"SPEED",v->speed_text,ARP_FONT_SMALL);
    control(c,2,"RANGE",decimal(value,arp_range_label(p->range)),ARP_FONT_VALUE);
    control(c,3,v->length_text?v->length_text:"LEN",0,0);
    c->bitmap(c->context,v->length_bitmap,ARP_COL(3)+9,42,1u);
    for(i=0;i<4;i++)if(v->locked&(1u<<i))invert(c,i);
    SHAPE(ARP_VLINE,22,0,22,25,1);
    SHAPE(ARP_VLINE,STEP_X(p->length+1)-1,5,STEP_X(p->length+1)-1,25,1);
    for(i=0;i<16;i++)if(p->enabled&(1u<<i)){
        extent=(absolute(p->offset[i])+REACH-1)/REACH;
        if(extent>scale)scale=extent;
    }
    for(i=0;i<16;i++)height[i]=p->offset[i]/scale+CENTRE;
    for(i=0;i<16;i++){
        selected=i==v->selected;pad=1+selected;x=STEP_X(i);off=p->offset[i];
        if(i<=p->length){
            if(p->enabled&(1u<<i)){
                if(absolute(height[i]-CENTRE)<=REACH)
                    SHAPE(off%12?ARP_RECT:ARP_FILL,x-pad,height[i]-pad,x+pad,height[i]+pad,1);
            }else SHAPE(ARP_HLINE,x-1,CENTRE,x+1,CENTRE,1);
            SHAPE(i%4?ARP_RECT:ARP_FILL,x-1,3-selected,x+1,1-selected,1);
        }else SHAPE(ARP_PIXEL,x,2-selected,x,2-selected,1);
    }
    SHAPE(ARP_VLINE,92,0,92,25,1);
    TEXT(ARP_FONT_LABEL,108,20,2,"OFS (E)");TEXT(ARP_FONT_LABEL,108,14,2,decimal(value,p->offset[v->selected&15]));
    TEXT(ARP_FONT_LABEL,108,7,2,"LEN (F)");TEXT(ARP_FONT_LABEL,108,1,2,decimal(value,p->length+1));
}

/* POLY's port of the native MIDI TRIG keyboard (fifteen 5-pixel white keys,
 * rows 7..23, black keys in rows 14..23, window on the chord), where POLY
 * draws it: x 48..122. */
static unsigned int pitch(int note){while(note>=12)note-=12;return (unsigned int)note;}
static int black(int note){return (0x54au>>pitch(note))&1u;}
static int marked(const int *notes,int note){return notes[0]==note || notes[1]==note || notes[2]==note || notes[3]==note;}
static void keyboard(const struct arp_canvas *c,const int *notes)
{
    /* White key of each pitch (a black one: the white below) and semitone of
     * each white key, as the native tables 401c979c and 401c97cc. */
    static const int white_of[12]={0,0,1,1,2,3,3,4,4,5,5,6},semitone[7]={0,2,4,5,7,9,11};
    int low=notes[0],high=notes[0],base,start,key,octave,note,x,i;
    for(i=1;i<4;i++){if(notes[i]<low)low=notes[i];if(notes[i]>high)high=notes[i];}
    base=low+high;if(base<0)base++;
    base=(base>>1)-12;
    if(base>notes[0])base=notes[0];
    if(base<notes[0]-23)base=notes[0]-23;
    if(base<0)base=0;
    else {if(black(base))base--;if(base>102)base=103;}
    start=white_of[pitch(base)];
    for(key=start,octave=0,x=51;x!=126;x+=5){
        note=base+semitone[key]-semitone[start]+octave;
        SHAPE(marked(notes,note)?ARP_PATTERN:ARP_FILL,x-3,7,x,23,1);
        if(++key==7){key=0;octave+=12;}
    }
    for(key=start,octave=0,x=53;x!=123;x+=5){
        note=base+semitone[key]-semitone[start]+octave+1;
        if(black(note)){
            if(marked(notes,note)){
                SHAPE(ARP_FILL,x-3,13,x+1,24,0);
                SHAPE(ARP_PATTERN,x-2,14,x,23,1);
            }else SHAPE(ARP_FILL,x-2,14,x,23,0);
        }
        if(++key==7){key=0;octave+=12;}
    }
}
/* MIDI 60 is C4. */
static char *note_name(char *s,unsigned int note)
{
    static const char names[]="C C#D D#E F F#G G#A A#B ";
    char *p=s;unsigned int k=note%12u;
    *p++=names[2*k];if(names[2*k+1]!=' ')*p++=names[2*k+1];
    decimal(p,(int)(note/12u)-1);return s;
}
void arp_draw_chord(const struct arp_chord_screen *v,const struct arp_canvas *c)
{
    static const char *labels[4]={"NOTE","NOT2","NOT3","NOT4"};
    char value[12];int notes[4];unsigned int i,raw;
    notes[0]=(int)(v->value[0]&127u);
    for(i=0;i<4;i++){
        raw=v->value[i];notes[i]=notes[0];
        if(!i)note_name(value,raw&127u);
        else if(!raw || raw==64u || raw>127u){value[0]='O';value[1]='F';value[2]='F';value[3]=0;}
        else {
            notes[i]=notes[0]+(int)raw-64;
            if(raw>64u){value[0]='+';decimal(value+1,(int)raw-64);}else decimal(value,(int)raw-64);
        }
        control(c,i,labels[i],value,ARP_FONT_SMALL);
        if(v->locked&(1u<<i))invert(c,i);
    }
    TEXT(ARP_FONT_LABEL,ARP_COL(0)+9,0,2,"SCALE");
    SHAPE(ARP_TOGGLE,ARP_COL(0),7,ARP_COL(0),7,v->scale?1u:0u);
    keyboard(c,notes);
}
#undef TEXT
#undef SHAPE
