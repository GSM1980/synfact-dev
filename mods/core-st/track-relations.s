| SPDX-License-Identifier: GPL-2.0-or-later
        .section .run,"ax"
        .globl core_relation_mask,core_relation_event,core_fx_route_track

| A2 parent event, A0 active Kit, D2 source track, D4 destination mask.
| Stock retains the returned physical mask for the matching native Note Off.
core_relation_mask:
        move.w (%a0,%d0.l*2),%d4
        tst.b core_native_interest+22
        beq.s .Lrelation_mask_stock
        move.l core_relation_scope,%d0
        swap %d0
        btst %d2,%d0
        bne.s .Lrelation_mask_owner
        mvz.w %d4,%d0
        and.l core_relation_scope,%d0
        bne.s .Lrelation_mask_dispatch
        bra.s .Lrelation_mask_stock
.Lrelation_mask_owner:
        | An empty master row needs neither normalization nor allocation.
        | With another subscriber the scope is open and D0 is negative.
        move.l core_relation_offsets,%d0
        bmi.s .Lrelation_mask_dispatch
        move.l %d1,-(%sp)
        move.l %d2,%d1
        add.l %d1,%d1
        lsr.l %d1,%d0
        andi.l #3,%d0
        neg.l %d0
        add.l %d2,%d0
        addi.l #0xaf2,%d0
        tst.w (%a0,%d0.l*2)
        movem.l (%sp),%d1
        lea 4(%sp),%sp
        bne.s .Lrelation_mask_dispatch
        clr.w %d4
        bra.s .Lrelation_mask_stock
.Lrelation_mask_dispatch:
        lea -24(%sp),%sp
        movem.l %d1/%a0-%a1,(%sp)
        move.l %a2,12(%sp)
        move.l %a0,16(%sp)
        mvz.w %d4,%d0
        move.l %d0,20(%sp)
        pea 12(%sp)
        pea 22
        jsr core_native
        addq.l #8,%sp
        move.w %d0,%d4
        movem.l (%sp),%d1/%a0-%a1
        lea 24(%sp),%sp
.Lrelation_mask_stock:
        tst.l %d7
        jmp 0x400a42f6

| A2 native clone. Restore the original saved SR for the stock list splice.
core_relation_event:
        move.w %sr,%d0
        move.w #0x2700,%sr
        tst.b core_native_interest+23
        beq.s .Lrelation_event_stock
        move.l core_relation_scope,%d1
        | The native clone's track is 0..12. Use D0 only after saving SR.
        move.l %d0,-(%sp)
        move.l 8(%a2),%d0
        btst %d0,%d1
        beq.s .Lrelation_event_restore
        move.l %a2,-(%sp)
        pea (%sp)
        pea 23
        jsr core_native
        lea 12(%sp),%sp
.Lrelation_event_restore:
        move.l (%sp)+,%d0
.Lrelation_event_stock:
        jmp 0x400a2912

| One stock getter feeds both the routing display and the track LEDs.
core_fx_route_track:
        move.l 24(%sp),-(%sp)
        jsr core_track_owner
        addq.l #4,%sp
        move.l %d0,%d4
        lea 0x4016e9c8,%a2
        jmp 0x4004bf98

        .globl core_fx_route_edit_track,core_relation_ui_mask
core_fx_route_edit_track:
        addi.l #-24,%d2
        move.l %d2,-(%sp)
        jsr core_track_owner
        addq.l #4,%sp
        move.l %d0,%d2
        jmp 0x4004c616

| Native trigger-only reader. Its choke counterpart reads another table.
core_relation_ui_mask:
        mvz.w (%a0,%d1.l*2),%d0
        move.l %d0,-(%sp)
        move.l 20(%sp),-(%sp)
        move.l %a0,-(%sp)
        jsr core_relation_ui_read
        lea 12(%sp),%sp
        move.l 20(%sp),%d1
        jmp 0x4001012a
