/* The two ARP TRIG pages (1.8, list item 37) on the host: every primitive
 * stays inside the page area the native TRIG view leaves (x 20..123, rows
 * 0..53: the rail, the page indicator and the header stay native), the four
 * top-row controls sit at the native positions, locks invert their box, LEN
 * shows its icon or its text, and SCALE and the keyboard share the bottom row.
 * Fonts are approximated at 4 px per character for the bounds. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../mods/arp-st/ui.h"
static unsigned int toggles,bitmaps,inverted,labels_at[4];
static int toggle_on;
/* The step marks pass their rows high to low, as the Digitone page did (and
 * the unit draws them): order each pair before checking. */
static void bounds(int x,int y,int x2,int y2)
{
    int t;
    if(x>x2){t=x;x=x2;x2=t;}
    if(y>y2){t=y;y=y2;y2=t;}
    assert(x>=ARP_AREA_X0 && x2<=ARP_AREA_X1 && y>=0 && y2<=ARP_AREA_Y1);
}
/* ARP's 3x5 font: every pixel inside the box interior (1 px margin) of the
 * control being drawn, and every character drawn (none blank). */
static int box_x0,box_x1;static unsigned int small_pixels,small_texts;
static void small_fill(void *c,int x,int y,int x2,int y2,unsigned int col)
{
    (void)c;assert(x==x2 && y==y2 && col==1);
    assert(x>=box_x0+2 && x<=box_x1-2 && y>=36 && y<=48);small_pixels++;
}
static void small(int x,int y,const char *s)
{
    unsigned int i,before;char one[2]={0,0};
    box_x0=x-9;box_x1=x+9;assert(y==40 && strlen(s)>=1 && strlen(s)<=4);
    arp_small_text(0,small_fill,x,y,2,s);
    for(i=0;s[i];i++){
        one[0]=s[i];before=small_pixels;arp_small_text(0,small_fill,x,y,2,one);
        assert(small_pixels>before);           /* a glyph for every character */
    }
    small_texts++;
}
static char len_label[16];
static void text(void *c,unsigned int font,int x,int y,unsigned int align,const char *s)
{
    int w=4*(int)strlen(s),x0=align==2?x-w/2:x;unsigned int i;(void)c;
    assert(font==ARP_FONT_LABEL || font==ARP_FONT_VALUE || font==ARP_FONT_SMALL);
    if(font==ARP_FONT_SMALL){assert(align==2);small(x,y,s);return;}
    assert(font==ARP_FONT_VALUE?strlen(s)<=2 && x==ARP_COL(2)+9:1);   /* only RANGE */
    bounds(x0,y,x0+w-1,y+(font==ARP_FONT_VALUE?8:4));
    if(y==27)for(i=0;i<4;i++)if(x==ARP_COL(i)+9)labels_at[i]++;
    if(y==27 && x==ARP_COL(3)+9)strcpy(len_label,s);
}
static void shape(void *c,unsigned int kind,int x,int y,int x2,int y2,unsigned int col)
{
    (void)c;
    if(kind==ARP_TOGGLE){bounds(x,y,x+18,y+16);toggles++;toggle_on=(int)col;return;}
    bounds(x,y,x2,y2);
    if(kind==ARP_FILL && col==0xffffffffu){assert(y==34 && y2==50 && x2==x+18);inverted++;}
}
static void bitmap(void *c,unsigned int r,int x,int y,unsigned int mode)
{(void)c;assert(r==0x1234 && mode==1u && x==ARP_COL(3)+9 && y==42);bitmaps++;}
static const struct arp_canvas canvas={0,text,shape,bitmap};
static void reset(void){toggles=bitmaps=inverted=0;memset(labels_at,0,sizeof labels_at);toggle_on=-1;}
int main(void)
{
    static const char *lengths[]={"1/64","1.5","128"};
    static const char *speeds[]={"1/1","1/2","1/3","1/4","1/5","1/6","1/8","1/10","1/12",
        "1/16","1/20","1/24","1/32","1/40","1/48","1/64","1/80","1/96"};
    struct arp_settings p;struct arp_screen s;struct arp_chord_screen cs;
    unsigned int pages=0,mode,range,len,sel,locks,show,k,off;int v;
    memset(&p,0,sizeof p);
    for(mode=0;mode<=4;mode++)for(range=0;range<=ARP_RANGE_MAX;range++)for(len=0;len<16;len+=5)
    for(sel=0;sel<18;sel+=1)for(locks=0;locks<16;locks+=5)for(show=0;show<2;show++)for(off=0;off<4;off++){
        p.mode=(unsigned char)mode;p.range=(unsigned char)range;p.length=(unsigned char)len;
        p.enabled=(unsigned short)(off==3?0x5555:0xffff);
        for(k=0;k<16;k++)p.offset[k]=(signed char)(off==0?0:off==1?(int)k*3-24:off==2?(k&1?24:-24):(int)k-8);
        s.settings=&p;s.track=0;s.selected=sel&15;s.locked=locks;
        s.speed_text=speeds[sel];s.length_bitmap=0x1234;
        s.length_text=show?lengths[sel%3]:0;
        reset();arp_draw(&s,&canvas);
        for(k=0;k<4;k++)assert(labels_at[k]==1);
        /* LEN is always the native knob; while it turns its value takes the
         * label's place, then "LEN" again (1.8 device feedback). */
        assert(bitmaps==1 && inverted==(unsigned int)__builtin_popcount(locks) && !toggles);
        assert(!strcmp(len_label,show?lengths[sel%3]:"LEN"));
        pages++;
    }
    for(k=0;k<4;k++)for(v=0;v<128;v++)for(locks=0;locks<16;locks+=5)for(show=0;show<2;show++){
        cs.track=k;cs.value[0]=(unsigned int)v;cs.value[1]=(unsigned int)(v+k)&127u;
        cs.value[2]=k==1?0:k==2?255:64+k*7;cs.value[3]=(unsigned int)(127-v);
        cs.locked=locks;cs.scale=show;
        reset();arp_draw_chord(&cs,&canvas);
        for(off=0;off<4;off++)assert(labels_at[off]==1);
        assert(toggles==1 && toggle_on==(int)show && inverted==(unsigned int)__builtin_popcount(locks) && !bitmaps);
        pages++;
    }
    assert(small_texts);
    printf("PASS: %u ARP page frames inside x 20..123, rows 0..53; native control positions, locks inverted, LEN knob and its value as label while turned, SCALE toggle and keyboard; %u 3x5 values inside their boxes (1 px margin)\n",pages,small_texts);
    return 0;
}
