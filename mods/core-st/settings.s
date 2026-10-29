| SPDX-License-Identifier: GPL-2.0-or-later
| A manager for borrowed inline 4-byte captures; no owned heap object.
| Define it in this translation unit before the shared SETTINGS implementation
| so FN_MGR stays relocatable (a command-line alias to an undefined symbol
| would be evaluated as absolute zero by GNU as).
        .section .run,"ax"
        .globl core_fn_manager
core_fn_manager:
        move.l 4(%sp),%a0
        move.l 12(%sp),%d0
        beq.s 1f
        cmpi.l #3,%d0
        beq.s 3f
        move.l 8(%sp),%a1
        cmpi.l #1,%d0
        beq.s 2f
        move.l (%a1),(%a0)
        bra.s 3f
1:      clr.l (%a0)
        bra.s 3f
2:      move.l %a1,(%a0)
3:      moveq #0,%d0
        rts
        .set FN_MGR,core_fn_manager
        .include "../core/settings.s"
