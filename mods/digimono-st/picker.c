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
#define STP_MODULE dm_destination_module
#define STP_MODULE_NAME "MONO TYPE SELECTOR"
#define STP_CONFIG_INIT(api) dm_destination_init(api)
#include <syntakt/subtype-picker.h>
int dm_picker_idle(void *view){return stp_idle(view);}
void dm_picker_draw(void *view,void *bitmap){stp_draw(view,bitmap);}
