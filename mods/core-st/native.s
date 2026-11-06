| SPDX-License-Identifier: GPL-2.0-or-later
| Fixed OS 1.41 bridges. No module-supplied patches or destinations.
| Each adapter tests its own operation byte of core_native_interest (modules.c).
        .section .run,"ax"
        .macro ENTRY name,op,bytes
        .globl core_native_\name,core_stock_\name
core_native_\name:
        tst.b core_native_interest+\op
        beq.w core_stock_\name
        lea 4(%sp),%a0
        move.l %a0,-(%sp)
        pea \op
        jsr core_native
        addq.l #8,%sp
        rts
core_stock_\name:
        .endm
        ENTRY trig,0,8
        lea -56(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        jmp 0x4008b88a
        ENTRY enqueue,1,6
        jmp 0x4011a648
        ENTRY header_load,5,8
        lea -12(%sp),%sp
        movem.l %d2/%a2-%a3,(%sp)
        jmp 0x400a79d4
        ENTRY header_reset,6,6
        movea.l 4(%sp),%a0
        tst.l %a0
        jmp 0x400b36d4

        .macro OBSERVE name,op,arg
        .globl core_native_\name
core_native_\name:
        tst.b core_native_interest+\op
        beq.s 99f
        lea -20(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l \arg,16(%sp)
        pea 16(%sp)
        pea \op
        jsr core_native
        addq.l #8,%sp
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 20(%sp),%sp
99:
        .endm
        OBSERVE publish,3,%a2
        lea 0x80004688,%a4
        clr.l %d5
        jmp 0x400a4ae6

        .globl core_native_immediate
core_native_immediate:
        tst.b core_native_interest+2
        beq.s 8f
        lea -16(%sp),%sp
        movem.l %d1/%a0-%a1,(%sp)
        move.l %d2,12(%sp)
        pea 12(%sp)
        pea 2
        jsr core_native
        addq.l #8,%sp
        movem.l (%sp),%d1/%a0-%a1
        lea 16(%sp),%sp
        tst.l %d0
        bne.s 1f
8:
        movea.l 0x444de434,%a2
        tst.l %a2
        jmp 0x4011a73c
1:      jmp 0x4011a780

        .globl core_native_type,core_native_src_type
core_native_type:
        tst.b core_native_interest+4
        beq.s 7f
        | Bytes 18/19: subscribed digital tracks / every cursor (modules.c).
        | d0 is free: both the stock replay and the active path overwrite it.
        tst.b core_native_interest+19
        bne.s 8f
        move.l %a1,%d0
        subi.l #0x80002f50,%d0
        cmpi.l #8,%d0
        bcc.s 7f
        btst %d0,core_native_interest+18
        bne.s 8f
7:
        .byte 0x71,0x19,0xe1,0x88,0x31,0x40,0x00,0x3e
        jmp 0x400a4bd6
8:
        lea -16(%sp),%sp
        movem.l %d1/%a0-%a1,(%sp)
        move.l %a1,12(%sp)
        pea 12(%sp)
        pea 4
        jsr core_native
        addq.l #8,%sp
        movem.l (%sp),%d1/%a0-%a1
        lea 16(%sp),%sp
        lea 1(%a1),%a1
        lsl.l #8,%d0
        move.w %d0,62(%a0)
        jmp 0x400a4bd6
core_native_src_type:
        tst.b core_native_interest+4
        beq.s 7f
        | Bytes 18/19: subscribed digital tracks / every cursor (modules.c).
        | d0 is free: both the stock replay and the active path overwrite it.
        tst.b core_native_interest+19
        bne.s 8f
        move.l %a3,%d0
        subi.l #0x80002f50-0xda0,%d0
        cmpi.l #8,%d0
        bcc.s 7f
        btst %d0,core_native_interest+18
        bne.s 8f
7:
        .byte 0x71,0x2b,0x0d,0xa0,0x72,0x25,0xb2,0x80
        jmp 0x400a4c9e
8:
        lea -16(%sp),%sp
        movem.l %d1/%a0-%a1,(%sp)
        lea 0xda0(%a3),%a0
        move.l %a0,12(%sp)
        pea 12(%sp)
        pea 4
        jsr core_native
        addq.l #8,%sp
        movem.l (%sp),%d1/%a0-%a1
        lea 16(%sp),%sp
        moveq #0x25,%d1
        cmp.l %d0,%d1
        jmp 0x400a4c9e

        .globl core_native_sound_set,core_native_machine_select
core_native_sound_set:
        tst.b core_native_interest+7
        beq.s 8f
        pea 0
        move.l 20(%sp),-(%sp)
        pea 7
        jsr core_native_view
        lea 12(%sp),%sp
        tst.l %d0
        beq.s 2f
8:
| Host API 7: stores of the modules' engine IDs (1.2: ST_MODULE_ID..+127;
| LFO3's) go to the parameter providers as query 24 with the original
| arguments; nonzero = stored.
        move.l 8(%sp),%d0
        subi.l #0x3f9,%d0
        cmpi.l #128,%d0
        bcc.s 9f
        clr.l -(%sp)
        clr.l -(%sp)
        pea 12(%sp)
        pea 24
        jsr core_parameter_provider
        lea 16(%sp),%sp
        tst.l %d0
        bne.s 2f
9:
        link %a6,#-40
        movem.l %d2-%d7/%a2,(%sp)
        jmp 0x4001330c
2:      rts
core_native_machine_select:
        tst.b core_native_interest+8
        beq.s 8f
        pea 0
        move.l 16(%sp),-(%sp)
        pea 8
        jsr core_native_view
        lea 12(%sp),%sp
        tst.l %d0
        beq.s 2b
8:
        lea -40(%sp),%sp
        movem.l %d2-%d6/%a2-%a6,(%sp)
        jmp 0x4002559c

        .globl core_native_modulation,core_native_lfo
core_native_modulation:
        tst.b core_native_interest+16
        beq.s 3f
        clr.l -(%sp)
        pea 16
        jsr core_native
        addq.l #8,%sp
        tst.l %d0
        beq.s 3f
        movea.l %d0,%a0
        jmp (%a0)
3:      lea -60(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        jmp 0x40097b04
| LFO subscribers receive the native call's {working, tempo, trigs} (0.12.14).
core_native_lfo:
        tst.b core_native_interest+17
        beq.s 4f
        pea 4(%sp)
        pea 17
        jsr core_native
        addq.l #8,%sp
        tst.l %d0
        beq.s 4f
        movea.l %d0,%a0
        jmp (%a0)
4:      lea -64(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        jmp 0x4011926e
