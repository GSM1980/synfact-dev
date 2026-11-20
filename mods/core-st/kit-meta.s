| SPDX-License-Identifier: GPL-2.0-or-later
| OS 1.42 task-side lifecycle. Original results and callee-saved ABI retained.
        .section .run,"ax"
        .globl core_meta_copy,core_meta_copy_stock,core_meta_kit_new
core_meta_copy:
        move.l 12(%sp),%d0
        cmpi.l #0x8e,%d0
        bcs.s core_meta_copy_stock
        move.w %sr,%d0
        andi.l #0x700,%d0
        bne.s core_meta_copy_stock
        jmp core_meta_copy_task
core_meta_copy_stock:
        movea.l 4(%sp),%a1
        movea.l 8(%sp),%a0
        jmp 0x40116144
core_meta_kit_new:
        move.w %sr,%d0
        andi.l #0x700,%d0
        bne.s .Lkit_new_stock
        move.l %d2,-(%sp)
        move.l 8(%sp),-(%sp)
        jsr .Lkit_new_stock
        addq.l #4,%sp
        move.l %d0,%d2
        pea 1
        pea 0x1618
        clr.l -(%sp)
        move.l 20(%sp),-(%sp)
        pea 2
        jsr core_kit_notify
        lea 20(%sp),%sp
        move.l %d2,%d0
        move.l (%sp)+,%d2
        rts
.Lkit_new_stock:
        lea -36(%sp),%sp
        clr.l %d0
        movem.l %d2-%d5/%a2-%a6,(%sp)
        jmp 0x400b338e

        .macro notify kind,phase,args
        pea \phase
        .if \phase
        move.l %d2,-(%sp)
        .else
        clr.l -(%sp)
        .endif
        move.l 20(%sp),-(%sp)
        move.l 20(%sp),-(%sp)
        pea \kind
        jsr core_kit_notify
        lea 20(%sp),%sp
        .endm
        .globl core_meta_kit_encode,core_meta_kit_decode
core_meta_kit_encode:
        move.l %d2,-(%sp)
        notify 5,0,0
        move.l 16(%sp),-(%sp)
        move.l 16(%sp),-(%sp)
        move.l 16(%sp),-(%sp)
        jsr .Lkit_encode_stock
        lea 12(%sp),%sp
        move.l %d0,%d2
        notify 5,1,0
        move.l %d2,%d0
        move.l (%sp)+,%d2
        rts
.Lkit_encode_stock:
        lea -28(%sp),%sp
        movem.l %d2-%d4/%a2-%a5,(%sp)
        jmp 0x400a78fc
core_meta_kit_decode:
        move.l %d2,-(%sp)
        notify 6,0,0
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        jsr .Lkit_decode_stock
        addq.l #8,%sp
        move.l %d0,%d2
        notify 6,1,0
        move.l %d2,%d0
        move.l (%sp)+,%d2
        rts
.Lkit_decode_stock:
        move.l 4(%sp),%d0
        movea.l 8(%sp),%a0
        jmp 0x400a78a8
