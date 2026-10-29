| SPDX-License-Identifier: GPL-2.0-or-later
| Publish fully built event lists once, before project/sample startup. The
| addresses are source labels inside the core's own 64 KiB RAM image. Mask IRQs
| during the operand stores/cache flush so no interrupt sees a partial binding.
        .section .run,"ax"
        .globl core_event_publish
core_event_publish:
        movea.l 4(%sp),%a0
        move.w %sr,%d1
        move.w #0x2700,%sr
1:      move.l (%a0)+,%d0
        beq.s 2f
        movea.l %d0,%a1
        move.l (%a0)+,(%a1)
        andi.l #-16,%d0
        movea.l %d0,%a1
        cpushl %dc,(%a1)
        lea 16(%a1),%a1
        cpushl %dc,(%a1)
        bra.s 1b
2:      move.l #0xa40ce100,%d0
        movec %d0,%cacr
        move.w %d1,%sr
        rts
