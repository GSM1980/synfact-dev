| SPDX-License-Identifier: GPL-2.0-or-later
        .section .run,"ax"
        .globl core_machine_dispatch_delta
core_machine_dispatch_delta:
        link.w %a6,#-72
        movem.l %d2-%d7/%a2-%a5,(%sp)
        move.l 8(%a6),%a2
        move.l 12(%a6),%d2
        move.l 16(%a6),%d5
        move.l 20(%a6),%d3
        move.l %a2,%d6
        addi.l #148,%d6
        lea 0x4022d59c,%a4
        jmp 0x4003b9c6

        .globl core_machine_sample_dial,core_stock_sample_dial
core_machine_sample_dial:
        .rept 8
        move.l 32(%sp),-(%sp)
        .endr
        jsr core_machine_dial
        lea 32(%sp),%sp
        tst.l %d0
        beq.s 1f
        rts
1:
core_stock_sample_dial:
        .byte 0x4f,0xef,0xff,0xec,0x48,0xd7,0x00,0x7c
        jmp 0x40011e7c
        .globl core_machine_direct_tail
core_machine_direct_tail:
        lea -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l %d3,-(%sp)
        move.l %d2,-(%sp)
        move.l %d4,-(%sp)
        jsr core_machine_direct_format
        lea 12(%sp),%sp
        tst.l %d0
        beq.s 1f
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        move.l %d3,%d0
        movem.l (%sp),%d2-%d4/%a2-%a3
        lea 20(%sp),%sp
        rts
1:      movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        movem.l (%sp),%d2-%d4/%a2-%a3
        lea 20(%sp),%sp
        jmp (%a1)
