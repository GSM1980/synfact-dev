/* SPDX-License-Identifier: GPL-2.0-or-later
 * SYN + YES and page randomize for bank controls (list item 22). The SYN
 * view's randomize (40044972) calls 4001c5da: 400146bc lists the machine's
 * parameter IDs (stock scans only 0..504, so a bank machine got none) and the
 * Sound model draws each with 40014954, which skips an ID whose cached entry
 * (4007b9b2) has no randomizer. Parameter pages (400385dc) reach 40014954
 * through their layout. A bank or module control's cached entry is its
 * alias's, and None (0x13) has none, so 40014954 gets the native generic one.
 * The draw is stored by the model's setter: module IDs on the Kit's model
 * reach their provider as query 24 (native.s), as Sound stores already do.
 */
#include "machines.h"
typedef unsigned int u32;
extern u32 core_machine_engine(const struct core_machine_def *);
extern int core_parameter_provider(u32,u32 *,void *,u32 *);
/* The pool sample slot alias (core_pool_format) is never drawn. */
#define SAMPLE_SLOT_ALIAS 0x1f2u
#define CORE_QUERY_RANDOMIZE 31u
/* An 84-byte cached parameter entry. 40014954 reads only the std::function
 * at +52: functor +52 (the clone allocates its own), manager +60 (400744f0)
 * and invoker +64 (40077ee8: uniform in the control's range, whole steps). */
static const u32 entry[21]={[15]=0x400744f0u,[16]=0x40077ee8u};
static int drawn(const struct core_machine_def *m,u32 index)
{return m->parameter[index].alias!=SAMPLE_SLOT_ALIAS;}
/* 4007b9b2 called from 40014954: the entry, or 0 for the stock path. A bank
 * machine's controls are drawn; any other ID when a provider answers query
 * 31 (host API 14): a module's own controls, or stock IDs on its page. */
const void *core_machine_randomize_entry(u32 id)
{
    u32 index,args[2],value;const struct core_machine_def *m=core_machine_parameter(id,&index);
    if(m)return drawn(m,index)?entry:0;
    args[0]=id;args[1]=0;
    return core_parameter_provider(CORE_QUERY_RANDOMIZE,args,0,&value)?entry:0;
}
/* The SYN view whose randomize (40044972) is running, for the list below. */
void *core_machine_randomize_view;
/* 400146bc for a bank machine type: the native vector (begin, end, capacity)
 * of the engine IDs on the SYN view's current page (all of the machine's
 * outside a SYN randomize), with the firmware's own reserve and push_back. */
u32 *core_machine_randomize_list(u32 *vector,u32 type)
{
    const struct core_machine_def *m=core_machine_find(type);u32 i,id,index;
    u32 *layout=core_machine_randomize_view?core_machine_view_layout(core_machine_randomize_view):0;
    core_machine_randomize_view=0;
    vector[0]=vector[1]=vector[2]=0;
    ((void (*)(u32 *,u32))0x40173a0au)(vector,8);
#pragma GCC unroll 1
    for(i=0;m && i<(layout?8u:m->params);i++){
        id=layout?layout[2+i]:core_machine_engine(m)+i;
        if(core_machine_parameter(id,&index)==m && drawn(m,index))
            ((void (*)(u32 *,u32 *))0x40173b40u)(vector,&id);
    }
    return vector;
}
