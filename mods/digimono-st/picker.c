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
/* The heavy TYPEs on one track at a time (types.h, DM_ONE_TRACK): the list
 * strikes one through while another track's SY MnM plays it. A track's machine type and
 * its TYPE as the core's sender sends a configuration control (the Sound
 * mirror's slot 0x36), as MD KIT reads them. */
#ifndef DM_TRACK_MACHINE
#define DM_TRACK_MACHINE(t) (*(const volatile unsigned char *)(0x80002f50u+(t)))
#define DM_TRACK_TYPE(t) (((const volatile unsigned short *)(0x800021ccu+(t)*0x8eu))[0x36]>>8)
#endif
static int dm_fits(unsigned track,unsigned type)
{
    unsigned t;
    if(!DM_ONE_TRACK(type) || track>=8)return 1;
    for(t=0;t<8;t++)
        if(t!=track && DM_TRACK_MACHINE(t)==DM_TYPE && DM_TRACK_TYPE(t)==type)return 0;
    return 1;
}
#define STP_ENABLED(track,index) dm_fits(track,index)
#include <syntakt/subtype-picker.h>
int dm_picker_idle(void *view){return stp_idle(view);}
void dm_picker_draw(void *view,void *bitmap){stp_draw(view,bitmap);}
