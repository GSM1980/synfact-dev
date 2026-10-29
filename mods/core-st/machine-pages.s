| SPDX-License-Identifier: GPL-2.0-or-later
| Generic four-page SYN view and native actions. UI task only.
        .section .run,"ax"
        .globl core_machine_slot_to_storage,core_machine_id_to_slot
core_machine_slot_to_storage:
        move.l 8(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_machine_storage_id
        addq.l #8,%sp
        cmpi.l #-1,%d0
        beq.s 1f
        rts
1:      move.l %d2,-(%sp)
        moveq #12,%d2
        move.l 8(%sp),%d1
        jmp 0x400a6cfc
core_machine_id_to_slot:
        move.l 8(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_machine_storage_slot
        addq.l #8,%sp
        cmpi.l #-1,%d0
        beq.s 1f
        rts
1:      move.l %d2,-(%sp)
        moveq #12,%d2
        move.l 8(%sp),%d1
        jmp 0x400a6cc2
        .globl core_machine_lane_bound,core_machine_lane_compress,core_machine_lane_update
core_machine_lane_bound:
        moveq #0x46,%d5
        tst.l core_machine_active
        beq.s 1f
        moveq #0x57,%d5
1:      mvz.b %d0,%d1
        cmp.l %d1,%d5
        bcs.s 2f
        jmp 0x400a7fce
2:      jmp 0x400a8030
core_machine_lane_compress:
        tst.l core_machine_active
        beq.s 1f
        movea.l core_machine_lanes,%a5
        rts
1:      lea 0x402340a4,%a5
        rts
core_machine_lane_update:
        tst.l core_machine_active
        beq.s 1f
        movea.l core_machine_lanes,%a2
        rts
1:      lea 0x402340a4,%a2
        rts
        .globl core_machine_page_nav_37d9e,core_stock_page_nav_37d9e
core_machine_page_nav_37d9e:
        move.l 8(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_machine_nav
        addq.l #8,%sp
        cmpi.l #-1,%d0
        beq.s 1f
        rts
1:
core_stock_page_nav_37d9e:
        .byte 0x2f,0x02,0x70,0x08,0x20,0x6f,0x00,0x08
        jmp 0x40037da6
        | One resolved encoder phase after the actual virtual resolver. It
        | covers ParameterPageView, TRIG and SourcePageView uniformly.
        .globl core_machine_editor_resolve
core_machine_editor_resolve:
        movea.l 0xbc(%a0),%a0
        move.l 8(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr (%a0)
        addq.l #8,%sp
        move.l %d0,-(%sp)
        move.l %d0,-(%sp)
        move.l %d4,-(%sp)
        move.l %a2,-(%sp)
        jsr core_machine_editor_event
        lea 12(%sp),%sp
        tst.l %d0
        bne.s core_machine_encoder_taken
        move.l (%sp)+,%d0
        rts
core_machine_encoder_taken:
        movem.l -72(%a6),%d2-%d7/%a2-%a5
        unlk %a6
        moveq #1,%d0
        rts
        .globl core_machine_page_op_385dc,core_stock_page_op_385dc
core_machine_page_op_385dc:
        pea 0
        move.l 8(%sp),-(%sp)
        pea 14
        jsr core_native_view
        lea 12(%sp),%sp
        tst.l %d0
        beq.s 8f
        moveq #1,%d0
        rts
8:
        move.l %a2,-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_machine_action_begin
        addq.l #4,%sp
        movea.l %d0,%a2
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        jsr core_stock_page_op_385dc
        addq.l #8,%sp
        move.l %d0,-(%sp)
        move.l %a2,-(%sp)
        jsr core_machine_action_end
        addq.l #4,%sp
        move.l (%sp)+,%d0
        movea.l (%sp)+,%a2
        rts
core_stock_page_op_385dc:
        .byte 0x2f,0x0a,0x70,0xff,0x2f,0x02,0x24,0x6f,0x00,0x0c
        jmp 0x400385e6
        .globl core_machine_page_op_38642,core_stock_page_op_38642
core_machine_page_op_38642:
        pea 0
        move.l 8(%sp),-(%sp)
        pea 14
        jsr core_native_view
        lea 12(%sp),%sp
        tst.l %d0
        beq.s 8f
        moveq #1,%d0
        rts
8:
        move.l %a2,-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_machine_action_begin
        addq.l #4,%sp
        movea.l %d0,%a2
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        jsr core_stock_page_op_38642
        addq.l #8,%sp
        move.l %d0,-(%sp)
        move.l %a2,-(%sp)
        jsr core_machine_action_end
        addq.l #4,%sp
        move.l (%sp)+,%d0
        movea.l (%sp)+,%a2
        rts
core_stock_page_op_38642:
        .byte 0x4f,0xef,0xff,0xf4,0x70,0xff,0x48,0xd7,0x04,0x0c
        jmp 0x4003864c
        .globl core_machine_page_op_3a244,core_stock_page_op_3a244
core_machine_page_op_3a244:
        move.l 4(%sp),-(%sp)
        jsr core_machine_sync
        addq.l #4,%sp
        move.l 8(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_stock_page_op_3a244
        addq.l #8,%sp
        rts
core_stock_page_op_3a244:
        .byte 0x4f,0xef,0xff,0xb0,0x48,0xd7,0x7c,0xfc
        jmp 0x4003a24c
        .globl core_machine_page_op_3b1f0,core_stock_page_op_3b1f0
core_machine_page_op_3b1f0:
        pea 0
        move.l 8(%sp),-(%sp)
        pea 14
        jsr core_native_view
        lea 12(%sp),%sp
        tst.l %d0
        beq.s 8f
        moveq #1,%d0
        rts
8:
        move.l %a2,-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_machine_action_begin
        addq.l #4,%sp
        movea.l %d0,%a2
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        jsr core_stock_page_op_3b1f0
        addq.l #8,%sp
        move.l %d0,-(%sp)
        move.l %a2,-(%sp)
        jsr core_machine_action_end
        addq.l #4,%sp
        move.l (%sp)+,%d0
        movea.l (%sp)+,%a2
        rts
core_stock_page_op_3b1f0:
        .byte 0x4e,0x56,0xff,0xf0,0x70,0xff,0x2f,0x0a
        jmp 0x4003b1f8
        .globl core_machine_page_op_3b29e,core_stock_page_op_3b29e
core_machine_page_op_3b29e:
        pea 0
        move.l 8(%sp),-(%sp)
        pea 14
        jsr core_native_view
        lea 12(%sp),%sp
        tst.l %d0
        beq.s 8f
        moveq #1,%d0
        rts
8:
        move.l %a2,-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_machine_action_begin
        addq.l #4,%sp
        movea.l %d0,%a2
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        jsr core_stock_page_op_3b29e
        addq.l #8,%sp
        move.l %d0,-(%sp)
        move.l %a2,-(%sp)
        jsr core_machine_action_end
        addq.l #4,%sp
        move.l (%sp)+,%d0
        movea.l (%sp)+,%a2
        rts
core_stock_page_op_3b29e:
        .byte 0x4e,0x56,0xff,0xf0,0x70,0xff,0x2f,0x0a
        jmp 0x4003b2a6
        .globl core_machine_page_op_3b3b8,core_stock_page_op_3b3b8
core_machine_page_op_3b3b8:
        pea 0
        move.l 8(%sp),-(%sp)
        pea 14
        jsr core_native_view
        lea 12(%sp),%sp
        tst.l %d0
        beq.s 8f
        moveq #1,%d0
        rts
8:
        move.l %a2,-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_machine_action_begin
        addq.l #4,%sp
        movea.l %d0,%a2
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        jsr core_stock_page_op_3b3b8
        addq.l #8,%sp
        move.l %d0,-(%sp)
        move.l %a2,-(%sp)
        jsr core_machine_action_end
        addq.l #4,%sp
        move.l (%sp)+,%d0
        movea.l (%sp)+,%a2
        rts
core_stock_page_op_3b3b8:
        .byte 0x4e,0x56,0xff,0xf0,0x70,0xff,0x2f,0x0a
        jmp 0x4003b3c0
        .globl core_machine_page_op_3bc74,core_stock_page_op_3bc74
core_machine_page_op_3bc74:
        pea 1
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        pea 2
        jsr core_event_view
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 7f
        moveq #1,%d0
        rts
7:
        move.l 4(%sp),-(%sp)
        jsr core_machine_sync
        addq.l #4,%sp
        move.l 8(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_stock_page_op_3bc74
        addq.l #8,%sp
        move.l %d0,-(%sp)
        clr.l -(%sp)
        move.l 16(%sp),-(%sp)
        move.l 16(%sp),-(%sp)
        pea 2
        jsr core_event_view
        lea 16(%sp),%sp
        move.l (%sp)+,%d0
        rts
core_stock_page_op_3bc74:
        .byte 0x4e,0x56,0xff,0xa8,0x48,0xd7,0x3c,0xfc
        jmp 0x4003bc7c
        .globl core_machine_page_layout,core_stock_page_layout
core_machine_page_layout:
        move.l 4(%sp),-(%sp)
        jsr core_machine_action_layout
        addq.l #4,%sp
        tst.l %d0
        beq.s 1f
        rts
1:
core_stock_page_layout:
        .byte 0x72,0x24,0x20,0x2f,0,4,0xb2,0x80
        jmp 0x4007b9d8
        .globl core_machine_sound_save,core_stock_sound_save
core_machine_sound_save:
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        jsr core_stock_sound_save
        lea 12(%sp),%sp
        move.l %d0,-(%sp)
        tst.b %d0
        beq.s 1f
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        jsr core_machine_sound_saved
        addq.l #8,%sp
1:      move.l (%sp)+,%d0
        rts
core_stock_sound_save:
        .byte 0x4f,0xef,0xff,0xe0,0x48,0xd7,0x7c,0x1c
        jmp 0x400a736e
        .globl core_machine_sound_load,core_stock_sound_load
core_machine_sound_load:
        move.l 8(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_stock_sound_load
        lea 8(%sp),%sp
        move.l %d0,-(%sp)
        tst.b %d0
        beq.s 1f
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        jsr core_machine_sound_loaded
        addq.l #8,%sp
1:      move.l (%sp)+,%d0
        rts
core_stock_sound_load:
        .byte 0x4f,0xef,0xff,0xe0,0x48,0xd7,0x7c,0x1c
        jmp 0x400a6f86
