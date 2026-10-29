| SPDX-License-Identifier: GPL-2.0-or-later
| Optional machine-file infrastructure. Application hooks only, OS 1.41.
        .section .run,"ax"
        .globl core_machine_boot,core_machine_flush,core_machine_page_count
        .globl core_machine_page_map,core_machine_sound_type,core_machine_copy_page
        .globl core_machine_menu_alloc,core_machine_menu_capacity,core_machine_menu_bound
core_machine_boot:
        jsr core_machine_load
        jmp 0x400bd6d6              | original project/state load, before samples
core_machine_flush:
        move.l 4(%sp),%d0
        move.l %d0,%d1
        andi.l #15,%d1
        sub.l %d1,%d0
        movea.l %d0,%a0
        move.l 8(%sp),%d0
        add.l %d1,%d0
1:      cpushl %dc,(%a0)
        lea 16(%a0),%a0
        subi.l #16,%d0
        bgt.s 1b
        move.l #0xa40ce100,%d0       | native MAIN CACR + instruction invalidate
        movec %d0,%cacr
        rts
| Read-only pointers into the retained, validated heap allocation. The native
| page sender remains responsible for the handshake and final type-3 packet.
core_machine_page_count:
        move.l core_machine_pages,%d0
        beq.s 1f
        move.l 0x4d84f2c1,%d2
        addi.l #2047,%d2
        lsr.l #8,%d2
        lsr.l #3,%d2
        add.l %d0,%d2
        lsl.l #8,%d2
        lsl.l #3,%d2
        rts
1:      move.l 0x4d84f2c1,%d2
        rts
core_machine_page_map:
        move.l core_machine_pages,%d0
        beq.s 2f
        move.l 0x41bbe424,%d0
        move.l 0x4d84f2c1,%d1
        addi.l #2047,%d1
        lsr.l #8,%d1
        lsr.l #3,%d1
        cmp.l %d1,%d0
        bcs.s 1f
        sub.l %d1,%d0
        move.l %d0,%d1
        addi.l #0x4000,%d1
        move.w %d1,0x1000000c
        lsl.l #8,%d0
        lsl.l #3,%d0
        add.l core_machine_audio,%d0
        subi.l #0x4d84f2c5,%d0
        rts
1:      moveq #11,%d1
2:      move.l 0x41bbe424,%d0
        move.w %d0,0x1000000c
        lsl.l %d1,%d0
        rts
core_machine_copy_page:
        movea.l 4(%sp),%a0
        move.l 8(%sp),-(%sp)
        jsr core_stock_page_copy
        addq.l #4,%sp
        rts
| Inline Sound validation: the file types are digital. Preserve incoming D0
| and the stock flags on the fallback; no C call with a modified native frame.
core_machine_sound_type:
        lea -12(%sp),%sp
        movem.l %d0-%d1/%a0,(%sp)
        andi.l #255,%d0
        move.l %d0,-(%sp)
        jsr core_machine_present
        addq.l #4,%sp
        tst.l %d0
        beq.s 1f
        movem.l (%sp),%d0-%d1/%a0
        lea 12(%sp),%sp
        moveq #-1,%d1
        move.b #3,%d2
        rts
1:      movem.l (%sp),%d0-%d1/%a0
        lea 12(%sp),%sp
        move.l %d0,%d1
        addq.l #1,%d1
        mvz.b %d1,%d1
        cmp.l %d1,%d2
        shi %d1
        move.b #3,%d2
        rts
| The native allocation site leaves its argument for the caller to remove.
| Retain that exact stack shape, with the original 180 B when no SMZ is active.
core_machine_menu_alloc:
        move.l (%sp),-(%sp)
        move.l #180,%d0
        move.l %d0,4(%sp)
        tst.l core_machine_active
        beq.s 1f
        move.l #320,%d0
        move.l %d0,4(%sp)
1:      move.l 4(%sp),-(%sp)
        jsr 0x40103788
        addq.l #4,%sp
        rts
core_machine_menu_capacity:
        tst.l core_machine_active
        beq.s 1f
        addi.l #320,%d2
        rts
1:      addi.l #180,%d2
        rts
core_machine_menu_bound:
        addq.l #1,%d2
        moveq #45,%d0
        tst.l core_machine_active
        beq.s 1f
        moveq #80,%d0
1:      move.l %d2,core_machine_menu_limit
        cmp.l %d2,%d0
        rts
        .globl core_machine_cursor_bound,core_machine_highlight_bound
        .globl core_machine_relative_bound4,core_machine_relative_bound2
core_machine_cursor_bound:
        moveq #45,%d1
        tst.l core_machine_active
        beq.s 1f
        moveq #79,%d1
1:      move.l %a2,-(%sp)
        move.l %d2,-(%sp)
        jmp 0x4002f2b8
core_machine_highlight_bound:
        moveq #45,%d2
        tst.l core_machine_active
        beq.s 1f
        moveq #79,%d2
1:      move.l %d0,%d1
        addq.l #1,%d1
        rts
core_machine_relative_bound4:
        tst.l core_machine_active
        beq.s 1f
        cmpi.l #0x3f8,%d4
        rts
1:      cmpi.l #0x1f8,%d4
        rts
core_machine_relative_bound2:
        tst.l core_machine_active
        beq.s 1f
        cmpi.l #0x3f8,%d2
        rts
1:      cmpi.l #0x1f8,%d2
        rts
        .globl core_machine_frame_tail
        .globl core_machine_menu_finish
core_machine_menu_finish:
        lea -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l %a2,-(%sp)
        jsr core_machine_menu_observe
        addq.l #4,%sp
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        move.l %a2,%d0
        movem.l 8(%sp),%d2-%d5/%a2-%a4
        jmp 0x400265be
core_machine_frame_tail:
        tst.l core_machine_extended
        beq.s 1f
        lea -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l %d3,-(%sp)            | this block's per-track trig mask (native, preserved)
        jsr core_machine_snapshot
        addq.l #4,%sp
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        bra.s 2f
1:      clr.b 0x800021a9
2:
        lea 0x402d7010,%a0           | exact displaced native clock-tail pointer
        rts
