/* SPDX-License-Identifier: GPL-2.0-or-later
 * md-load.h - the knob values a voice loads with when it is chosen from the list
 * (MD KIT 0.3), in TYPE's order (md-voices.h), raw 0..127 as the Syntakt shows them.
 *
 * Read by hand from the user's photographs of a Machinedrum SPS-1UW+ (a new kit,
 * each machine just selected), 2026-11-24. The Machinedrum shows the PTCH of PI-BD,
 * PI-SD, PI-MT, PI-ML and PI-RS centred (-64..63) and so does MD KIT 0.3
 * (md_centred, main.c); stored, as here and as mdkit reads it, it is 64 more: shown
 * 16, 0, -42, 0 and 32 are 80, 64, 22, 64 and 96 (mdkit's own defaults agree: PI-SD
 * 64, PI-MT 20, PI-RS 90). PI-RC, PI-CC and PI-HH show 64, which a centred value
 * cannot reach: theirs is plain.
 *
 * TRX-XC and TRX-B2 had no photograph: the user gave their values (XC is mdkit's but
 * RDEC 96). TRX-MA and TRX-CL keep mdkit's defaults (md_voice_defaults) until their
 * values are known. A control past a voice's last one is 0.
 */
#ifndef MD_LOAD_H
#define MD_LOAD_H
static const unsigned char md_voice_load[30][8]={
    {64,64,0,0,0,0,0,0},             /* TRX-BD  PTCH DEC RAMP RDEC STRT NOIS HARM CLIP */
    {64,96,0,64,127,100,64,0},       /* TRX-SD  PTCH DEC BUMP BENV SNAP TONE TUNE CLIP */
    {64,40,40,102,64,0,0,0},         /* TRX-XT  PTCH DEC RAMP RDEC DAMP DIST DTYP */
    {64,64,127,64,64,32,32,64},      /* TRX-CP  CLPY TONE HARD RICH RATE ROOM RSIZ RTUN */
    {64,32,0,0,0,0,0,0},             /* TRX-RS  PTCH DEC DIST */
    {64,64,64,0,64,64,0,0},          /* TRX-CB  PTCH DEC ENH DAMP TONE BUMP */
    {32,32,64,64,64,0,0,0},          /* TRX-CH  GAP DEC HPF LPF MTAL */
    {64,64,64,64,64,0,0,0},          /* TRX-OH  GAP DEC HPF LPF MTAL */
    {64,64,64,64,64,64,0,0},         /* TRX-CY  RICH DEC TOP TTUN SIZE PEAK */
    {32,32,0,0,64,64,64,64},         /* TRX-MA  no photograph: mdkit's */
    {64,32,64,0,64,0,0,0},           /* TRX-CL  no photograph: mdkit's */
    {64,32,32,96,0,0,0,0},           /* TRX-XC  PTCH DEC RAMP RDEC DAMP DIST DTYP (the user: mdkit's, RDEC 96) */
    {0,64,96,64,0,0,0,0},            /* TRX-B2  PTCH DEC RAMP HOLD TICK NOIS DIRT DIST (the user) */
    {36,76,64,55,64,64,32,127},      /* EFM-BD  PTCH DEC RAMP RDEC MOD MFRQ MDEC MFB */
    {64,55,64,64,64,32,44,16},       /* EFM-SD  PTCH DEC NOIS NDEC MOD MFRQ MDEC HPF */
    {64,64,38,87,48,84,55,127},      /* EFM-XT  PTCH DEC RAMP RDEC MOD MFRQ MDEC CLIC */
    {83,41,64,36,32,64,127,32},      /* EFM-CP  PTCH DEC CLPS CDEC MOD MFRQ MDEC HPF */
    {64,32,64,32,64,64,44,64},       /* EFM-RS  PTCH DEC MOD HPF SNAR SPTC SDEC SMOD */
    {64,64,64,96,64,64,64,0},        /* EFM-CB  PTCH DEC SNAP FB MOD MFRQ MDEC */
    {96,32,0,0,64,64,64,127},        /* EFM-HH  PTCH DEC TREM TFRQ MOD MFRQ MDEC FB */
    {64,72,96,127,64,32,64,0},       /* EFM-CY  PTCH DEC FB HPF MOD MFRQ MDEC */
    {80,26,127,96,29,122,0,0},       /* PI-BD   PTCH DEC HARD HAMR TENS DAMP (PTCH shown 16) */
    {64,64,64,16,0,127,96,0},        /* PI-SD   PTCH DEC HARD RING TENS RVOL RDEC (PTCH shown 0) */
    {22,24,127,127,127,96,96,64},    /* PI-MT   PTCH DEC HARD HAMR TUNE DAMP SIZE POS (PTCH shown -42) */
    {64,54,127,0,0,0,0,0},           /* PI-ML   PTCH DEC HARD TENS (PTCH shown 0) */
    {99,0,72,96,64,0,0,0},           /* PI-MA   GRNS DEC GLEN SIZE HARD */
    {96,48,64,64,64,64,0,0},         /* PI-RS   PTCH DEC HARD RING RVOL RDEC (PTCH shown 32) */
    {64,70,96,32,127,127,64,127},    /* PI-RC   PTCH DEC HARD RING AG AU BR GRAB */
    {64,70,96,32,127,127,64,127},    /* PI-CC   PTCH DEC HARD RING AG AU BR GRAB */
    {64,70,96,32,127,127,64,127},    /* PI-HH   PTCH DEC CLSN RING AG AU BR CLOS */
};
_Static_assert(sizeof md_voice_load / sizeof md_voice_load[0] == MD_VOICES, "one row a voice");
#endif
