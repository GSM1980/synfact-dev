/* SPDX-License-Identifier: GPL-2.0-or-later; Synfact FM TONE control contract.
 * RATIO B is ONE stored control (slot 15): the combined index 19*slow+fast
 * (0..360, physical B1 slow / B2 fast) times FM_RATIO_STEP, so one lock, the
 * native clear/copy and modulation over the whole range all apply. Its former
 * B2 slot 45 holds RATIO OFFSET C, now lockable and a modulation target;
 * slot 0 is the native None destination and only keeps a hidden placeholder.
 * No conversion of Sounds/patterns saved before this layout (user decision). */
#include <syntakt/machines.h>
#include "ratio.h"
#define P(s,l,hi,d) {s,l,0,hi,d,0x13}
static const struct core_machine_param controls[]={
 P("ALGO","ALGORITHM",0x700,0),P("RTOC","RATIO C",0x1200,0x300),
 P("RTOA","RATIO A",0x2300,0x300),
 P("RTOB","RATIO RB1",FM_RATIO_STEP*FM_RATIO_MAX,FM_RATIO_STEP*FM_RATIO_DEFAULT),
 {"HARM","HARMONICS",0x2500,0x5900,0x3f00,0x1f1},P("DTUN","DETUNE",0x7f00,0),
 P("FDBK","FEEDBACK",0x7f00,0),P("MIX","MIX",0x7f00,0),
 P("ATKA","A ENV ATTACK",0x7f00,0),P("DECA","A ENV DECAY",0x7f00,0x2000),
 P("ENDA","A ENV END",0x7f00,0x7f00),P("LEVA","A LEVEL",0x7f00,0),
 P("ATKB","B ENV ATTACK",0x7f00,0),P("DECB","B ENV DECAY",0x7f00,0x2000),
 P("ENDB","B ENV END",0x7f00,0x7f00),P("LEVB","B LEVEL",0x7f00,0),
 P("ADEL","A DELAY",0x7f00,0),P("ATRG","A TRIG",0x100,0x100),
 P("ARST","A ENV RESET",0x100,0x100),P("PHRT","PHASE RESET",0x400,0x100),
 P("BDEL","B DELAY",0x7f00,0),P("BTRG","B TRIG",0x100,0x100),
 P("BRST","B ENV RESET",0x100,0x100),P("C","RATIO OFFSET C",0x7ffe,0x4000),
 P("---","UNUSED",0,0),P("A","RATIO OFFSET A",0x7ffe,0x4000),
 P("B1","RATIO OFFSET B1",0x7ffe,0x4000),P("B2","RATIO OFFSET B2",0x7ffe,0x4000)
};
const struct core_machine_def fm_tone={65,28,"SY FM DTONE","DTON","SYN",controls,ST_MACHINE_NO_INIT,44,10,14};
