/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <syntakt/machines.h>
#define P(a,b,lo,hi,init) {a,b,lo,hi,init,0x13}
/* 0x13 has no fixed FUNC quantizer. TUNE's native fine encoder policy is
 * selected separately by the UI; input.c owns its extended octave list. */
#define COMMON(tune_alias) {"TUNE","TUNE",0x0400,0x5800,0x4000,tune_alias}, \
 P("PLAY","PLAY MODE",0,0x0300,0x0300),P("BR","BIT REDUCTION",0,0x7f00,0), \
 {"SAMP","SAMP",0,0x7f00,0,0x13}
#define OVER {"OVER","OVERDRIVE",0,0x7f00,0,0x1f7}
static const struct core_machine_param one[]={COMMON(0x13),P("STRT","START",0,0x7800,0),
 P("LEN","LENGTH",0,0x7800,0x7800),P("LOOP","LOOP POSITION",0,0x7800,0),OVER};
static const struct core_machine_param werp[]={COMMON(0x13),P("SEG","SEGMENT SIZE",0,0x0200,0x0100),
 P("MODE","SEGMENT PLAY MODE",0,0x0300,0x0300),P("BARS","BARS",0,0x0300,0x0200),OVER};
static const struct core_machine_param repitch[]={COMMON(0x1f1),P("STRT","START",0,0x7800,0),
 P("LEN","LENGTH",0,0x7800,0x7800),P("BARS","BARS",0,0x0300,0x0200),OVER};
static const struct core_machine_param slice[]={COMMON(0x13),P("SLCE","SLICE SELECT",0,0x4000,0),
 P("LEN","ADDITIONAL SLICES",0,0x3f00,0),P("GRID","SLICE GRID",0,0x0400,0),OVER};
#define D(type,name,shortname,controls,rank) \
 {type,8,name,shortname,"SYN",controls,ST_MACHINE_NO_INIT,44,11,rank}
const struct core_machine_def sp_one_shot=D(66,"SP ONE SHOT","ONE",one,10);
const struct core_machine_def sp_werp=D(67,"SP WERP","WERP",werp,11);
const struct core_machine_def sp_repitch=D(68,"SP REPITCH","RPCH",repitch,12);
const struct core_machine_def sp_slice=D(69,"SP SLICE","SLIC",slice,13);
