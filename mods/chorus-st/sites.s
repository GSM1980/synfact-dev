| SPDX-License-Identifier: GPL-2.0-or-later
| The five entries only the chorus hooks (install.c writes a JMP at each).
| Each replays its displaced stock instructions exactly when the call is not
| the chorus's. Core candidates: a global (FX) parameter provider and a view
| draw override would let these move into the core.
        .text
        .set CHORUS_ID,0x403                    | ids.h
        .globl chorus_kit_entry,chorus_get_entry,chorus_set_entry
        .globl chorus_reverb_draw_entry,chorus_amp_entry

| 4000d870 (model, id): the editor's "has a value" check. The generic page
| label caller (returns to 4003a3c4) reads the same bit as "locked" and would
| invert the label; the settings have no locks, so it gets 0 (V55).
chorus_kit_entry:
        move.l 8(%sp),%d0
        subi.l #CHORUS_ID,%d0
        cmpi.l #7,%d0
        bcc.s 1f
        move.l (%sp),%d0
        cmpi.l #0x4003a3c4,%d0
        beq.s 2f
        moveq #1,%d0
        rts
2:      moveq #0,%d0
        rts
1:      .byte 0x2f,0x02,0x74,0x7c,0x22,0x2f,0x00,0x08
        jmp 0x4000d878

| 4000d94a (model, id) -> value; 4000da32 (model, id, value, ...) -> 0/-1.
chorus_get_entry:
        move.l 8(%sp),%d0
        subi.l #CHORUS_ID,%d0
        cmpi.l #7,%d0
        bcc.s 1f
        jmp chorus_param_get
1:      .byte 0x2f,0x02,0x22,0x2f,0x00,0x08,0x20,0x2f,0x00,0x0c
        jmp 0x4000d954
chorus_set_entry:
        move.l 8(%sp),%d0
        subi.l #CHORUS_ID,%d0
        cmpi.l #7,%d0
        bcc.s 1f
        jmp chorus_param_set
1:      .byte 0x2f,0x03,0x2f,0x02,0x22,0x2f,0x00,0x0c
        jmp 0x4000da3a

| 40042ce0 (view, frame): REVERB's draw; ui.c draws page 25 itself.
chorus_reverb_draw_entry:
        move.l 8(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr chorus_reverb_draw
        addq.l #8,%sp
        tst.l %d0
        beq.s 1f
        rts
1:      .byte 0x4f,0xef,0xff,0xa0,0x48,0xd7,0x7c,0xfc
        jmp 0x40042ce8

| 40041e7c, inside AMP's control drawing (A6 = the parameter): HOLD (74)
| keeps its branch, the CHO send (72) takes the send controls' (V55).
chorus_amp_entry:
        move.b #74,%d1
        cmp.l %a6,%d1
        beq.s 1f
        cmpa.l #72,%a6
        beq.s 2f
        jmp 0x40041ee2
1:      jmp 0x40041ec8
2:      jmp 0x40041ede

| Installation: IRQ exclusion, one code word written with its D-cache line
| pushed, then the I-cache and branch cache invalidated with the native MAIN
| CACR value (the same sequence as LFO3's).
        .globl chorus_lock,chorus_unlock,chorus_code16,chorus_sync
chorus_lock:
        moveq #0,%d0
        move.w %sr,%d0
        move.w #0x2700,%sr
        rts
chorus_unlock:
        move.l 4(%sp),%d0
        move.w %d0,%sr
        rts
chorus_code16:
        movea.l 4(%sp),%a0
        move.l 8(%sp),%d0
        move.w %d0,(%a0)
        move.l %a0,%d0
        andi.l #-16,%d0
        movea.l %d0,%a0
        cpushl %dc,(%a0)
        rts
chorus_sync:
        nop
        move.l #0xa40ce100,%d0
        movec %d0,%cacr
        rts
