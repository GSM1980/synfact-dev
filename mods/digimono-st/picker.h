/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef DM_PICKER_H
#define DM_PICKER_H
#include <syntakt/modules.h>
int dm_picker_idle(void *view);
void dm_picker_draw(void *view,void *bitmap);
int dm_destination_init(const struct st_smz_api *api);
/* SY MnM's on_load: registers its picker with the bank's one SUBTYPE SELECTOR
 * module (st_subtype_module, <syntakt/subtype-picker.h>) */
struct core_machine_def;
int dm_picker_on_load(const struct core_machine_def *m);
#endif
