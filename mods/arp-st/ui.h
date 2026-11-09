/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYNFACT_ST_ARP_UI_H
#define SYNFACT_ST_ARP_UI_H
#include "arp.h"
/* 1.8: the two ARP pages are TRIG pages, drawn inside the native TRIG view
 * as POLY's page is. The native view keeps its track/LEV rail (x < 19), the
 * page indicator (x 125..127) and the header (rows 54..63); a page draws in
 * x 20..123, rows 0..53 (y grows up). Top row: four controls at the native
 * positions (encoders A..D), label at y 27, box x..x+18 by 34..50. Bottom
 * row (rows 0..26): the arp steps or SCALE and the keyboard. */
#define ARP_COL(i) (24+26*(int)(i))
#define ARP_AREA_X0 20
#define ARP_AREA_X1 123
#define ARP_AREA_Y1 53
enum {ARP_FILL,ARP_RECT,ARP_VLINE,ARP_HLINE,ARP_PIXEL,ARP_PATTERN,
      ARP_TOGGLE /* the native AMP.T/FLT.T toggle at x,y; colour = on */};
/* Labels and short values use the native page fonts (443da230, 443da210).
 * Values of three or four characters (TRUE, DOWN, CYCL, 1/12, C#-1, -63)
 * use ARP's own 3x5 font, four pixels a character, so they fit the 17-pixel
 * inside of a box: the native fonts are about five pixels a character. */
enum {ARP_FONT_LABEL,ARP_FONT_VALUE,ARP_FONT_SMALL};
/* ARP_FONT_SMALL through the adapter's fill (x0,y0,x1,y1, colour 1); y is the
 * bottom row (y grows up), align 2 centres on x. Unknown characters are blank. */
void arp_small_text(void *context,void (*fill)(void *,int,int,int,int,unsigned int),
                    int x,int y,unsigned int align,const char *s);
#define ARP_SMALL_WIDTH(n) ((n)?4*(int)(n)-1:0)
struct arp_canvas {
    void *context;
    void (*text)(void *,unsigned int font,int x,int y,unsigned int align,const char *);
    void (*shape)(void *,unsigned int kind,int x,int y,int x2,int y2,unsigned int color);
    void (*bitmap)(void *,unsigned int resource,int x,int y,unsigned int mode);
};
struct arp_screen {
    const struct arp_settings *settings;
    unsigned int track,selected;
    unsigned int locked; /* MODE, SPEED, RANGE, LEN: bits 0..3, drawn inverted */
    const char *speed_text;
    const char *length_text;    /* while LEN turns: its value, in its label's place; else 0 */
    unsigned int length_bitmap; /* LEN's native knob image, also while it turns */
};
/* Page 1: MODE, SPEED, RANGE, LEN; the sixteen steps, OFS (E), LEN (F). */
void arp_draw(const struct arp_screen *,const struct arp_canvas *);
/* Page 2: NOTE and NOT2..NOT4 as raw lane values (chord.h) on A..D; SCALE
 * (E) and POLY's keyboard over the chord. */
struct arp_chord_screen {
    unsigned int track;
    unsigned int value[4];
    unsigned int locked; /* NOTE, NOT2..NOT4: bits 0..3, drawn inverted */
    unsigned int scale;
};
void arp_draw_chord(const struct arp_chord_screen *,const struct arp_canvas *);
#endif
