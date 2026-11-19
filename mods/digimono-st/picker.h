/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef DM_PICKER_H
#define DM_PICKER_H
#include <syntakt/modules.h>
int dm_picker_idle(void *view);
void dm_picker_draw(void *view,void *bitmap);
int dm_destination_init(const struct st_smz_api *api);
extern const struct st_smz_module dm_destination_module;
#endif
