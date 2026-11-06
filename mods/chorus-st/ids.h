/* SPDX-License-Identifier: GPL-2.0-or-later
 * The chorus's parameter IDs, in the core's module range after LFO3's
 * (<syntakt/machines.h>, ST_MODULE_ID/ST_MODULE_UI + 10..16). V56 used the
 * Digitone's 0x23d..0x243 and 0x324..0x32a, which core 0.10+ gives to machine
 * type 66. Settings are kept by FX record word, not by ID, so the IDs never
 * reach a project. sites.s repeats CHORUS_ID. */
#ifndef CHORUS_IDS_H
#define CHORUS_IDS_H
#define CHORUS_ID 0x403u    /* engine: DPTH SPD HPF WDTH DEL REV VOL */
#define CHORUS_UI 0x4eau    /* UI, same order */
#define CHORUS_SEND 72u     /* the Sound's dormant Digitone chorus send (AMP) */
#endif
