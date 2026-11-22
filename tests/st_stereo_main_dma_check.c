/* SPDX-License-Identifier: GPL-2.0-or-later
 * Target build of the STEREO module's MAIN side (mods/samplers-st/stereo/
 * main.c) for tests/test_st_stereo_main_dma_emu.py: the eDMA read of the
 * window block at ev_render_in and its pairing at the first mix event, run in
 * Unicorn with the eDMA emulated. No EMAC runs here (st_stereo_track_d decodes
 * D without it). Never in a bank.
 */
#include "../mods/samplers-st/stereo/main.c"
/* ev_render_in; returns have | tagged << 1 | pending << 2 */
u32 dma_receive(void){receive();return have|tagged<<1|pending<<2;}
/* the first mix event's view of track t: D into dma_out, 1 when stereo */
s32 dma_out[32];
u32 dma_track(u32 t){return (u32)st_stereo_track_d(t,dma_out);}
u32 dma_init(void){initialize(0);return boot_sequence;}
/* where slot 0 is (the eDMA's destination) and which slot was used */
u32 dma_slot0(void){return (u32)&slot0;}
u32 dma_used(void){return use==&slot0?1:use==&slot1?2:0;}
