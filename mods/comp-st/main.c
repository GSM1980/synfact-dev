/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "comp.h"
struct comp_runtime *comp_hot DATA=&comp_ddr_runtime;
_Static_assert(sizeof(struct comp_runtime)==68,"compact hot state layout");
_Static_assert(__builtin_offsetof(struct comp_runtime,config.hpa)==40 && __builtin_offsetof(struct comp_runtime,kit)==44 &&
               __builtin_offsetof(struct comp_runtime,meter)==56 && __builtin_offsetof(struct comp_runtime,lp)==60,"dsp.s offsets");
/* The fixed detector source is the FX bus ADC pair. No extra PCM loop:
 * parallel mix is y=(1-m)*x+m*g*x=((1-m)+m*g)*x, via the same two VCAs. */
/* The single assembly callback owns the C ABI and cached Kit selection. */
static int initialize(const struct st_smz_api *api)
{
    if(!api || !api->ui || !api->ui->host || api->ui->host->version<16)return 1;
    comp_params_init();comp_place();return comp_ui_init(api);
}
static const struct st_smz_subscription subscriptions[]={
    {ST_EV_FX_CV,(void (*)(void))comp_audio},
    {ST_EV_NATIVE,(void (*)(void))comp_native},
    {ST_EV_DRAW|ST_EV_VIEW,(void (*)(void))comp_draw},
    {ST_EV_TICK,(void (*)(void))comp_tick}};
const struct st_smz_module comp_module={"COMP",initialize,subscriptions,4};
