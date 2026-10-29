| SPDX-License-Identifier: GPL-2.0-or-later
| Existing DRAW collection at each complete native view's layer. This replaces
| emission inside the base ParameterView and private owned-view draw bridges.
        .section .run,"ax"
        .globl core_view_draw
core_view_draw:
        lea -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        clr.l -(%sp)
        move.l %d3,-(%sp)
        move.l %a4,-(%sp)
        pea 1
        jsr core_event_view
        lea 16(%sp),%sp
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        move.l %a4,-(%sp)
        jsr (%a5)
        movea.l (%a3),%a3
        jmp 0x40100836
