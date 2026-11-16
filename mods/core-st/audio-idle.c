/* SPDX-License-Identifier: GPL-2.0-or-later
 * MAIN-only preview mode. The native IRQ, DMA and CPU2 mailboxes still run.
 * One boot-bound owner supplies bounded, IRQ-safe eligibility and preview.
 */
#include <syntakt/host.h>
typedef unsigned int u32;
#ifndef CORE_IDLE_SEQ_STATE
#define CORE_IDLE_SEQ_STATE (*(const volatile u32 *)0x43bd73bcu)
#endif
volatile u32 core_audio_idle_active;
const struct st_audio_idle_owner *volatile core_audio_idle_owner;

void core_audio_idle_reset(void)
{
    /* active is IRQ-owned and recomputed before any block uses it. */
    core_audio_idle_owner=0;
}
/* Once at render-in, before observers. The native control frame and DMA
 * continue even while active. External START fails open to normal. */
#ifndef __m68k__
u32 core_audio_idle_begin(void)
{
    const struct st_audio_idle_owner *owner=core_audio_idle_owner;u32 active=0;
    if(owner && CORE_IDLE_SEQ_STATE==2u)active=(u32)owner->ready();
    return core_audio_idle_active=active;
}
#endif
