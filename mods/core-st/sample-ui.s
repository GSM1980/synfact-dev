| SPDX-License-Identifier: GPL-2.0-or-later
| Generic MAIN mix event, before the native output gain/mix stages.
| Task-side sample preview supplies PCM; subscribers do no IO or allocation.
        .section .run,"ax"
        .globl core_main_mix
core_main_mix:
        lea -20(%sp),%sp
        movem.l %d0-%d1/%a0-%a2,(%sp)
        .globl core_main_mix_table
core_main_mix_table:
        lea ev_main_mix,%a2
1:      move.l (%a2)+,%d0
        beq.s 2f
        movea.l %d0,%a0
        jsr (%a0)
        bra.s 1b
2:      movem.l (%sp),%d0-%d1/%a0-%a2
        lea 20(%sp),%sp
        pea -296(%a6)
        pea 0x8000e0f0
        jmp 0x4008fb0e
