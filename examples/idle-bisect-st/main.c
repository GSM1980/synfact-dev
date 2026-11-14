/* SPDX-License-Identifier: GPL-2.0-or-later
 * Diagnostic only. A source-built bank can route disabled subscriptions to
 * this C-ABI no-op without moving any executable byte or relocation entry.
 * No native site, SRAM, device IO or periodic activity is added here.
 */
#include <syntakt/modules.h>
#include <syntakt/host.h>

int st_idle_diag_noop(void){return 0;}

int st_idle_diag_initialize(const struct st_smz_api *api)
{
    const struct st_machine_host_api *host;
    if(!api || !api->ui || !(host=api->ui->host) || host->version<5 ||
       !host->native_interest)return 1;
    /* Disabled native subscribers must decline without enabling every
     * operation through the core's default fail-open interest mask. All
     * disabled entries share this one slot; active LFO3/ARP/POLY need three.
     * The reference also reserves the slot, keeping initial registration
     * order identical. It has no effect unless a list contains this no-op. */
    host->native_interest((void (*)(void))st_idle_diag_noop,0);
    return 0;
}

const struct st_smz_module st_idle_diag_module={
    "IDLE DIAGNOSTIC",st_idle_diag_initialize,
    (const struct st_smz_subscription *)0xffffffffu,0};
