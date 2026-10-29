/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SP_SAMPLE_BROWSER_H
#define SP_SAMPLE_BROWSER_H
#include <syntakt/machines.h>
void sp_browser_reset(void);
void sp_browser_begin(const struct st_machine_ui_api *,void *,st_u32,st_u32);
void sp_browser_close(void *);
int sp_browser_idle(const struct st_machine_ui_api *,const struct core_machine_def *,void *,st_u32);
int sp_browser_active(void *);
int sp_browser_draw(const struct st_machine_ui_api *,const struct core_machine_def *,void *,void *,st_u32);
int sp_browser_pre_key(void *,void *,st_u32);
void sp_browser_post_key(void *,void *,st_u32);
#endif
