| SPDX-License-Identifier: GPL-2.0-or-later
| Idle accounting from Synfact pool/system_info_main.s (digihealth, irpina).
| OS 1.41 only. The scheduler has saved all registers before this site.
| D2-D4 are scratch here; A0 is the incoming TCB and must survive.
| core_clock_snapshot(out[3]) -> {timer, idle, MAIN audio IRQ inside idle}.
| UI/task context; a short SR critical section makes the snapshot consistent.
        .section .run,"ax"
        .globl core_task_switch, core_clock_snapshot
core_task_switch:
        move.l 0xfc07000c,%d2
        move.l 0x454e39a8,%d3
        tst.l core_clock+8
        beq.s 3f
        move.l 8(%sp),%d4
        cmpi.l #0x41bae25c,%d3
        bne.s 1f
        cmpi.l #0x40082816,%d4
        beq.s 2f
        bra.s 3f
1:      cmpi.l #0x41baa208,%d3
        bne.s 3f
        cmpi.l #0x4008229c,%d4
        bne.s 3f
2:      move.l %d2,%d4
        move.l core_clock,%d3
        sub.l %d3,%d4
        add.l %d4,core_clock+4
3:      clr.l core_clock+8
        move.l %a0,%d3
        cmpi.l #0x41bae25c,%d3
        beq.s 4f
        cmpi.l #0x41baa208,%d3
        bne.s 5f
4:      move.l %d2,core_clock
        moveq #1,%d0
        move.l %d0,core_clock+8
5:      move.l #0xffffdfff,%d0
        rts

core_clock_snapshot:
        move.l 4(%sp),%a0
        moveq #0,%d1
        move.w %sr,%d1
        move.w #0x2700,%sr
        move.l 0xfc07000c,%d0
        move.l %d0,(%a0)
        tst.l core_clock+8
        beq.s 1f
        sub.l core_clock,%d0
        bra.s 2f
1:      moveq #0,%d0
2:      add.l core_clock+4,%d0
        move.l %d0,4(%a0)
        move.l core_clock+16,%d0
        move.l %d0,8(%a0)
        move.w %d1,%sr
        rts

| ev_render_in/out: void f(void), MAIN audio interrupt, level 5.
| Both preserve the stock register frame; handlers must be bounded, no heap/IO.
        .globl core_render_in,core_render_out
core_render_in:
        lea -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l 0xfc07000c,%d0
        move.l %d0,core_clock+12
        move.l core_clock+8,%d0
        move.l %d0,core_clock+20
        move.l %a2,-(%sp)
        .globl core_render_in_table
core_render_in_table:
        lea ev_render_in,%a2
1:      move.l (%a2)+,%d0
        beq.s 2f
        movea.l %d0,%a0
        jsr (%a0)
        bra.s 1b
2:      movea.l (%sp)+,%a2
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        link %a6,#-164
        movem.l %d0-%a5,(%sp)
        jmp 0x400a385e
core_render_out:
        .globl core_render_out_table
core_render_out_table:
        lea ev_render_out,%a2
1:      move.l (%a2)+,%d0
        beq.s 2f
        movea.l %d0,%a0
        jsr (%a0)
        bra.s 1b
2:      tst.l core_clock+20
        beq.s 3f
        move.l 0xfc07000c,%d0
        sub.l core_clock+12,%d0
        add.l %d0,core_clock+16
3:      movem.l -164(%a6),%d0-%a5
        jmp 0x400a54a6

        .section .bss
        .balign 4
core_clock: .skip 24
