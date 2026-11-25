/* SPDX-License-Identifier: GPL-2.0-or-later
 * The optional third LFO. Shared object/lock storage is a separate internal
 * service; loading ARP or Playhead does not enable this feature. */
#include <syntakt/modules.h>
extern int st_state_enable_lfo(const struct st_smz_api *);
const struct st_smz_module lfo3_module={"LFO3",st_state_enable_lfo,(const struct st_smz_subscription *)0xffffffffu,0};
