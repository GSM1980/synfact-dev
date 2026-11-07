/* SPDX-License-Identifier: GPL-2.0-or-later
 * Machine background UI work subscribes to the existing compose tick.
 * Traverse the live visible-view list; retain no pointers and change no vptr.
 * The five native parameter-interface tables are OS 1.41 identities, not
 * machine identities. Unknown native views are never read as parameter views.
 */
#include "machines.h"
typedef st_u32 u32;typedef core_u8 u8;
extern u32 fw_view_machine(void *);extern void fw_invalidate(void *);
extern void core_mods_auto_confirm_tick(void);
extern const struct st_machine_ui_api core_machine_ui_api;
void core_machine_tick(void *raw)
{
    u8 *ctrl=raw,*end=ctrl+0x14,*node=*(u8 **)(ctrl+0x1c),*view;
    const struct core_machine_def *m;const struct st_machine_ui *u;u32 page;
    core_mods_auto_confirm_tick();
    if(!core_machine_active)return;
    /* Same visible range/order as native DRAWALL. UI-task callbacks cannot
     * push/pop views. Opaque overlays exclude the views underneath them. */
    while(node && node!=end){
        view=*(u8 **)(node+8);node=*(u8 **)node;
        if(!core_machine_syn_view(view) || !*(u32 *)(view+0x2c) || view[0x17] || view[0x30])continue;
        m=core_machine_find(fw_view_machine(view));u=core_machine_ui(m);
        page=*(u32 *)(view+0x90);
        if(u && page<u->pages && (u32)u->idle!=CORE_SMZ_NONE &&
           u->idle(&core_machine_ui_api,m,view,page))fw_invalidate(view);
    }
}
