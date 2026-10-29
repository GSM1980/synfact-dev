/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef CORE_ST_MACHINES_H
#define CORE_ST_MACHINES_H
#include "smz.h"
#include <syntakt/machines.h>
static __inline__ int core_machine_parameter_view(const void *view)
{
    st_u32 p=*(const st_u32 *)view;
    return p==0x401c7838u || p==0x401c8838u || p==0x401ca0dcu ||
        p==0x401c8d94u || p==0x4023ff74u || p==0x401c97fcu;
}
extern st_u32 fw_parameter_selecting(void *);
/* One SYN ownership gate for drawing, navigation and periodic UI work.
 * MachineParameterPageView draws the native selector instead of its controls
 * while this model flag is set. Highlighted machines must not take that layer. */
static __inline__ int core_machine_syn_view(const void *raw)
{
    const core_u8 *v=raw;const st_u32 *pages;
    if(!v || !core_machine_parameter_view(v) || *(const st_u32 *)(v+0x88)!=0x14)return 0;
    pages=*(const st_u32 *const *)(v+0x7c);
    return pages && pages[0]==6 && !(core_u8)fw_parameter_selecting(*(void *const *)(v+0x6c));
}
/* Disk descriptors have this layout after relocation, on the 32-bit target.
 * A machine's on_load may initialize ONLY its private file-owned memory;
 * return zero on success. No core subscriptions or firmware writes. */
enum core_machine_query {
    ST_NAME,ST_SHORT,ST_GROUP,ST_CATEGORY,ST_AVAILABLE,ST_DIGITAL,
    ST_UI_ENGINE,ST_LOCAL,ST_OWNER,ST_ENGINE_UI,ST_SLOT_ENGINE,ST_PAGE_LOCAL,
    ST_PAGE_COPY,ST_RANGE,ST_VALID_OWNER,ST_LAYOUT,ST_CACHED,ST_FORMAT,
    ST_LABEL,ST_SYNTH,ST_LONG_LABEL,ST_FLAGS,ST_RANK,ST_ENCODER_CACHED
};
extern volatile st_u32 core_machine_active;
extern const struct core_machine_def *core_machine_definitions;
extern st_u32 core_machine_count;
/* Last effective extended controls per track (machine-params.c), in the
 * bind allocation: 8 rows of type|count<<16 and ten packed word pairs. */
#define CORE_MACHINE_SHADOW_ROW 11u
extern st_u32 *core_machine_shadow;
extern const struct st_machine_ui *core_machine_ui_definitions;
const struct st_machine_ui *core_machine_ui(const struct core_machine_def *);
const struct core_machine_def *core_machine_parameter(st_u32,st_u32 *);
const char *core_machine_format(st_u32,st_u32);
st_u32 core_machine_policy(st_u32);
void core_machine_draw(void *,void *);
const struct core_machine_def *core_machine_find(st_u32);
st_u32 core_machine_engine(const struct core_machine_def *);
st_u32 core_machine_slot(st_u32);
st_u32 *core_machine_layout(const struct core_machine_def *,st_u32);
st_u32 *core_machine_view_layout(void *);
int core_machine_sync(void *);
st_u32 core_machine_nav(void *,st_u32);
st_u32 core_machine_action_begin(void *);
void core_machine_action_end(st_u32);
st_u32 *core_machine_action_layout(st_u32);
int core_machine_present(st_u32);
void core_machine_defaults(st_u32,void *,st_u32);
int core_machine_query(st_u32,st_u32 *,void *,st_u32 *);
int core_machine_bind(const struct core_machine_def *,st_u32);
/* Status: 0 active, 1 absent, 2 invalid, 3 IO, 4 allocation, 5 init failed.
 * Stored ordinary mods are independent of this optional machine bank. */
extern volatile st_u32 core_machine_status;
#endif
