/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SP_SLICE_ALLOCATE_H
#define SP_SLICE_ALLOCATE_H
#include <syntakt/machines.h>
void sp_allocate_reset(void);
int sp_allocate_pre_key(const struct st_machine_ui_api *,void *,void *,st_u32);
int sp_allocate_encoder(void *);
int sp_allocate_idle(void *);
int sp_allocate_active(void *);
int sp_allocate_draw(void *,void *);
#endif
