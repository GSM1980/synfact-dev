/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SYNFACT_COMP_H
#define SYNFACT_COMP_H
#include <syntakt/modules.h>
#include <syntakt/host.h>
#include <syntakt/native.h>
typedef unsigned int u32;typedef int s32;typedef unsigned short u16;typedef unsigned char u8;
#define DATA __attribute__((section(".data")))
#define COMP_ID 0x40au
#define COMP_UI 0x4f1u
#define COMP_PAGE 39u
#define COMP_COUNT 6u
#define COMP_KIT_SIZE 0x1618u
#define COMP_KIT_BASE 0x41ad6134u
#define COMP_RECORD 0x15d0u
#define COMP_RECORD_BYTES 16u
/* Published under IRQ exclusion; audio only reads this prepared 40-byte row. */
struct comp_config {s32 threshold,slope,makeup,attack32,release32,cross;u32 wet,dry,idle_peak,idle_gain;};
struct comp_runtime {struct comp_config config;u32 kit;s32 hold,attack;u32 meter;};
extern struct comp_runtime comp_ddr_runtime;
extern struct comp_runtime *comp_hot;
extern const struct comp_config *comp_current_config(u32);
static inline u32 comp_meter_read(void){return *(volatile u32 *)&comp_hot->meter;}
extern const u16 comp_initial[6],comp_maximum[6];
extern void comp_process(struct st_fx_cv *),comp_audio(struct st_fx_cv *);
extern u32 comp_lock(void);extern void comp_unlock(u32);
extern s32 comp_param_get(u32),comp_param_set(u32,s32);
extern void comp_meta(const u32 *),comp_params_init(void);
extern const u16 *comp_current_values(void);
extern int comp_ui_init(const struct st_smz_api *);
extern int comp_query(u32,u32 *,void *,u32 *),comp_native(u32,const u32 *,u32 *);
extern void comp_draw(void *,void *,u32),comp_tick(void *);
extern u32 comp_place(void);
extern void comp_check_state(void);
#endif
