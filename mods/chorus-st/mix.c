/* SPDX-License-Identifier: GPL-2.0-or-later
 * The chorus's subscriptions to core 1.2's events: each calls its hot.c
 * entry, in MAIN SRAM once chorus_place copied it there (dsp.c). */
#include "chorus.h"
void chorus_sends(const struct st_track_sends *t) {chorus_e.sends(t);}
void chorus_before_delay(void) {chorus_e.before_delay();}
void chorus_before_reverb(void) {chorus_e.before_reverb();}
void chorus_master_return(void) {chorus_e.master_return();}
