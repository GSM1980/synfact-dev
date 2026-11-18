| SPDX-License-Identifier: GPL-2.0-or-later
| OS 1.42's selected-track model overrides the base long-label lookup at
| 40013730 (vtable entry 401c3e28). UI task, C ABI (model, engine ID).
| Reuse query 20 for machines and module providers, including the CHO send.
| An unhandled query replays the complete native prologue and preserves the
| selected-model handling of parameters 92/102 and every other stock label.
        .section .run,"ax"
        .globl core_machine_global_parameter_long_label
core_machine_global_parameter_long_label:
        tst.l core_machine_active
        beq.s 2f
        move.l 8(%sp),%d0
        cmpi.l #0x1f9,%d0
        bcc.s 1f
        cmpi.l #72,%d0
        bne.s 2f
1:      lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 20
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 3f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
3:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
2:      .byte 0x4f,0xef,0xff,0xf4,0x48,0xd7,0x04,0x0c
        jmp 0x40013738
