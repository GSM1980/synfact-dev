/* SPDX-License-Identifier: GPL-2.0-or-later */
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
void arp_draw(const struct arp_screen *v,const struct arp_canvas *c)
{
    const struct arp_settings *p=v->settings;
    static const char *modes[]={"OFF","TRUE","UP","DOWN","CYCL"};
    char value[24],title[32];unsigned int i,j;int scale=1,height[16],pad,selected,x,off,extent;
    const char *suffix=" ARPEGGIATOR";
#define TEXT(f,x,y,a,s) c->text(c->context,f,x,y,a,s)
#define SHAPE(k,x,y,x2,y2,col) c->shape(c->context,k,x,y,x2,y2,col)
    title[0]='T';decimal(title+1,(int)v->track+1);
    for(j=0;title[j];j++);
    for(i=0;suffix[i];i++)title[j++]=suffix[i];
    title[j]=0;
    TEXT(0,64,59,2,title); /* 64 - native font height (5 pixels) */
    TEXT(0,16,48,2,modes[p->mode<=4?p->mode:0]);TEXT(0,16,37,2,"MODE");
    TEXT(0,48,48,2,v->speed_text);TEXT(0,48,37,2,"SPEED");
    TEXT(0,80,48,2,decimal(value,p->range+1));TEXT(0,80,37,2,"RANGE");
    /* Mode 3: centred (as always) and negative while LEN is p-locked. */
    c->bitmap(c->context,v->length_bitmap,112,48,(v->locked&8u)?3u:1u);
    SHAPE(ARP_FILL,106,37,118,42,0);
    TEXT(0,112,37,2,v->show_length?v->length_text:"LEN");
    /* A p-locked value is inverted, as the native pages show it: the fill
     * XORs with a negative colour (400f7df2). */
    /* LEN (bit 3) is the length marker, inverted by the bitmap call above. */
    for(i=0;i<3;i++)if(v->locked&(1u<<i))SHAPE(ARP_FILL,16+32*(int)i-14,47,16+32*(int)i+13,54,0xffffffffu);
    c->bitmap(c->context,0xffffffffu,1,7,0); /* native ARP rail artwork */
    title[0]='A';title[1]='R';title[2]='P';title[3]=' ';title[4]='T';decimal(title+5,(int)v->track+1);
    TEXT(1,14,2,1,title);
    SHAPE(ARP_VLINE,29,1,29,32,1);
    SHAPE(ARP_VLINE,33+4*(p->length+1),7,33+4*(p->length+1),31,1);
    for(i=0;i<16;i++)if(p->enabled&(1u<<i)){
        extent=(absolute(p->offset[i])+10)/11;
        if(extent>scale)scale=extent;
    }
    for(i=0;i<16;i++)height[i]=p->offset[i]/scale+19;
    for(i=0;i<16;i++){
        selected=i==v->selected;pad=1+selected;x=34+4*i;off=p->offset[i];
        if(i<=p->length){
            if(p->enabled&(1u<<i)){
                if(absolute(height[i]-19)<=11)
                    SHAPE(off%12?ARP_RECT:ARP_FILL,x-pad,height[i]-pad,x+pad,height[i]+pad,1);
            }else SHAPE(ARP_HLINE,x-1,19,x+1,19,1);
        }
        if(i<=p->length)SHAPE(i%4?ARP_RECT:ARP_FILL,x-1,3-selected,x+1,1-selected,1);
        else SHAPE(ARP_PIXEL,x,2-selected,x,2-selected,1);
    }
    TEXT(1,114,9,1,"OFS (E)");TEXT(1,114,2,1,decimal(value,p->offset[v->selected&15]));
    TEXT(1,114,26,1,"LEN (F)");TEXT(1,114,19,1,decimal(value,p->length+1));
    SHAPE(ARP_VLINE,100,0,100,31,1);SHAPE(ARP_HLINE,100,15,127,15,0);
    SHAPE(ARP_RECT,0,0,127,32,1);
#undef TEXT
#undef SHAPE
}

/* The chord page. POLY's port of the native MIDI TRIG keyboard (fifteen
 * 5-pixel white keys, rows 7..23, black keys in rows 14..23, window on the
 * chord), centred: POLY draws it at x 48..121, here 27..100. */
static unsigned int pitch(int note){while(note>=12)note-=12;return (unsigned int)note;}
static int black(int note){return (0x54au>>pitch(note))&1u;}
static int marked(const int *notes,int note){return notes[0]==note || notes[1]==note || notes[2]==note || notes[3]==note;}
static void keyboard(const struct arp_canvas *c,const int *notes)
{
    /* White key of each pitch (a black one: the white below) and semitone of
     * each white key, as the native tables 401c979c and 401c97cc. */
    static const int white_of[12]={0,0,1,1,2,3,3,4,4,5,5,6},semitone[7]={0,2,4,5,7,9,11};
    int low=notes[0],high=notes[0],base,start,key,octave,note,x,i;
    const int dx=-21;
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
        c->shape(c->context,marked(notes,note)?ARP_PATTERN:ARP_FILL,x+dx-3,7,x+dx,23,1);
        if(++key==7){key=0;octave+=12;}
    }
    for(key=start,octave=0,x=53;x!=123;x+=5){
        note=base+semitone[key]-semitone[start]+octave+1;
        if(black(note)){
            if(marked(notes,note)){
                c->shape(c->context,ARP_FILL,x+dx-3,13,x+dx+1,24,0);
                c->shape(c->context,ARP_PATTERN,x+dx-2,14,x+dx,23,1);
            }else c->shape(c->context,ARP_FILL,x+dx-2,14,x+dx,23,0);
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
    char value[12],title[32];int notes[4];unsigned int i,j,raw;
    const char *suffix=" ARP CHORD";
#define TEXT(f,x,y,a,s) c->text(c->context,f,x,y,a,s)
    title[0]='T';decimal(title+1,(int)v->track+1);
    for(j=0;title[j];j++);
    for(i=0;suffix[i];i++)title[j++]=suffix[i];
    title[j]=0;
    TEXT(0,64,59,2,title);
    notes[0]=(int)(v->value[0]&127u);
    for(i=0;i<4;i++){
        raw=v->value[i];
        if(!i)note_name(value,raw&127u);
        else if(!raw || raw==64u || raw>127u){value[0]='O';value[1]='F';value[2]='F';value[3]=0;}
        else {
            notes[i]=notes[0]+(int)raw-64;
            if(raw>64u){value[0]='+';decimal(value+1,(int)raw-64);}else decimal(value,(int)raw-64);
        }
        if(i && (!raw || raw==64u || raw>127u))notes[i]=notes[0];
        TEXT(0,16+32*(int)i,48,2,value);TEXT(0,16+32*(int)i,37,2,labels[i]);
        /* Inverted while the held step has its own value, as the main page. */
        if(v->locked&(1u<<i))c->shape(c->context,ARP_FILL,16+32*(int)i-14,47,16+32*(int)i+13,54,0xffffffffu);
    }
    keyboard(c,notes);
    c->shape(c->context,ARP_RECT,0,0,127,32,1);
#undef TEXT
}
