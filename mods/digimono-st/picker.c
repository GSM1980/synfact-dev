/* SPDX-License-Identifier: GPL-3.0-only
 * MAIN only: shared subtype selector adapted from the MD worktree prototype.
 * TYPE keeps its existing Sound slot and IDs; no DSP changes or core hooks.
 */
#include "types.h"
#include "picker.h"
#define STP_MACHINE DM_TYPE
#define STP_CONTROL_ID (DM_ENGINE+DM_TYPE_CONTROL)
#define STP_CONTROL_INDEX DM_TYPE_CONTROL
#define STP_COUNT DM_TYPES
#define STP_NAMES dm_names
#define STP_DEFAULT DM_DEFAULT_TYPE
#define STP_ON_LOAD dm_picker_on_load
#define STP_CONFIG_INIT(api) dm_destination_init(api)
/* One track at a time plays a heavy TYPE (types.h, DM_ONE_TRACK): the list
 * strikes them all through while another track's SY MnM plays one. A track's machine type and
 * its stored TYPE (slot 0x36), from the playing Kit (syntakt/kit.h), as MD KIT reads them.
 * Until 0.8.2 they were the SRAM copies AUDIO was last sent, stale until each track
 * played after a pattern change or a cleared track. */
#ifndef DM_TRACK_MACHINE
#include <syntakt/kit.h>
#define DM_TRACK_MACHINE(t) st_kit_track_machine(t)
#define DM_TRACK_TYPE(t) (st_kit_track_value((t),0x36u)>>8)
#endif
static int dm_fits(unsigned track,unsigned type)
{
    unsigned t;
    if(!DM_ONE_TRACK(type) || track>=8)return 1;
    for(t=0;t<8;t++)
        if(t!=track && DM_TRACK_MACHINE(t)==DM_TYPE && DM_ONE_TRACK(DM_TRACK_TYPE(t)))return 0;
    return 1;
}
#define STP_ENABLED(track,index) dm_fits(track,index)
/* right of the names: a drawing of the Monomachine of our own (mnm-icon.h,
 * tools/mnm_icon.py), drawn after the list; "DPRO WAVE" ends at x 68 */
static void dm_list_art(void *bitmap);
#define STP_LIST_ART(bitmap) dm_list_art(bitmap)
#define STP_LIST_RIGHT 69                     /* the cursor's bar stops left of it */
#include <syntakt/subtype-picker.h>
#include "mnm-icon.h"
#define DM_ICON_X 72                          /* x 72..123, left of the scroll bar */
#define DM_ICON_TOP 42                        /* the list spans y 2..50: centred */
/* its runs of lit pixels a row at a time; LCD y up, its top row at DM_ICON_TOP */
static void dm_list_art(void *bitmap)
{
    unsigned r;
    for (r = 0; r < DM_ICON_H; ++r) {
        int x = 0;
        while (x < DM_ICON_W) {
            int start;
            while (x < DM_ICON_W && !((dm_icon[r][x >> 5] >> (31 - (x & 31))) & 1u)) ++x;
            if (x >= DM_ICON_W) break;
            start = x;
            while (x < DM_ICON_W && ((dm_icon[r][x >> 5] >> (31 - (x & 31))) & 1u)) ++x;
            STP_FILL(bitmap, DM_ICON_X + start, DM_ICON_TOP - (int)r, DM_ICON_X + x - 1, DM_ICON_TOP - (int)r, 1);
        }
    }
}
int dm_picker_idle(void *view){return stp_idle(view);}
void dm_picker_draw(void *view,void *bitmap){stp_draw(view,bitmap);}
