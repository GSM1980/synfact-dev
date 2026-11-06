/* SPDX-License-Identifier: GPL-2.0-or-later
 * Chorus: Synfact's stereo chorus (V56) on the MAIN mixer's FX sends.
 * Each track sends through its CHO control; one chorus renders the sum at
 * 24 kHz before the delay and returns to the delay (DEL), the reverb (REV)
 * and the FX bus (VOL). Settings live in the Kit's FX record (params.c).
 * Core 1.2's mixer events carry the audio; an older core's bank parser
 * refuses a bank that subscribes to them.
 */
#include <syntakt/modules.h>
typedef st_u32 u32;
typedef unsigned char u8;
extern void chorus_dsp_reset(void);
extern u32 chorus_place(void);
extern void chorus_check_code(void);
struct st_track_sends;
extern void chorus_sends(const struct st_track_sends *);
extern void chorus_before_delay(void),chorus_before_reverb(void),chorus_master_return(void);
extern int chorus_ui_init(const struct st_smz_api *),chorus_native(u32,const u32 *,u32 *);

/* Task context, before any subscription runs: DSP state, then the controls
 * and the settings' codec (ui.c). A bank with the mixer events loads on core
 * 1.2 or later only, so host API 13. */
static int initialize(const struct st_smz_api *api)
{
    chorus_place();
    chorus_dsp_reset();
    return chorus_ui_init(api);
}
static const struct st_smz_subscription subscriptions[] __attribute__((aligned(4)))={
    {ST_EV_TRACK_SEND,(void (*)(void))chorus_sends},
    {ST_EV_DELAY_IN,chorus_before_delay},
    {ST_EV_REVERB_IN,chorus_before_reverb},
    {ST_EV_MAIN_MIX,chorus_master_return},
    {ST_EV_NATIVE,(void (*)(void))chorus_native},
    {ST_EV_TICK,chorus_check_code}};
const struct st_smz_module chorus_module={"CHORUS",initialize,subscriptions,6};
