/* SPDX-License-Identifier: GPL-2.0-or-later
 * A list of the project pool's 128 slots for a machine's slot control, drawn
 * over its SYN page while the control turns, p-locks included: the SP
 * machines' sample list (mods/samplers-st/browser.inc) made machine-neutral,
 * so that any machine, and the SP machines later, can share one copy.
 * YES or any other key closes it; NO reverts the value (and a lock made while
 * the list was open); 240 UI ticks without a turn close it. Slots read ST
 * (stereo) or WT (a canonical wavetable, UI API 9) after their name.
 * MAIN, UI task: no disk IO or allocation; names come from the pool's cache.
 */
#ifndef SYNTAKT_SLOT_LIST_H
#define SYNTAKT_SLOT_LIST_H
#include <syntakt/machines.h>
void slot_list_reset(void);
/* The control id (engine id) that turned, on this page of this machine type. */
void slot_list_begin(const struct st_machine_ui_api *,void *view,st_u32 type,st_u32 page,st_u32 id);
void slot_list_close(void *view);
int slot_list_active(void *view);
/* From the machine's UI idle and draw: nonzero when the list owns the page. */
int slot_list_idle(void *view,st_u32 type,st_u32 page);
int slot_list_draw(void *view,void *ctx,st_u32 type,st_u32 page);
/* From a module's KEY|VIEW|PRE and KEY|VIEW subscriptions. */
int slot_list_pre_key(void *view,void *event,st_u32 phase);
void slot_list_post_key(void *view,void *event,st_u32 phase);
#endif
