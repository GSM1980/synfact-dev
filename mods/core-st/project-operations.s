| SPDX-License-Identifier: GPL-2.0-or-later
| OS 1.42 project command workers. Redirect their callback literals, leaving
| every native body/prologue and its normal success/error returns intact.
| These are the actual workers after confirmation, not menu/dialog creation.
        .section .run,"ax"
        .macro PROJECT name,native
        .globl core_project_\name
core_project_\name:
        bsr.w core_project_call
        .long \native
        .endm
        PROJECT new,0x40059f9e
        PROJECT rename,0x4005a6f8
        PROJECT copy,0x4005a788
        PROJECT paste,0x4005a7aa
        PROJECT clear,0x4005a7d0
        PROJECT clear_many,0x4005a828
        PROJECT delete_many,0x4005a8a2
        PROJECT save,0x4005ba50
        PROJECT load,0x4005bafa

| All nine workers take one capture pointer and return a scalar in D0.
| BSR's return points to the native address literal. Keep the caller's A2
| and original return on its own task stack, including nested operations.
        .globl core_project_call,core_project_call_end
core_project_call:
        movea.l (%sp)+,%a0
        move.l %a2,-(%sp)
        movea.l (%a0),%a2
        moveq #1,%d0
        cmp.l 0x43bd83bc,%d0
        bne.s 1f
        jsr 0x4008c300
1:      addq.l #1,core_project_depth
        move.l 8(%sp),-(%sp)
        jsr (%a2)
        addq.l #4,%sp
        subq.l #1,core_project_depth
        movea.l (%sp)+,%a2
        rts
core_project_call_end:
