| SPDX-License-Identifier: GPL-2.0-or-later
| OS 1.41: shared audio-track event acceptance, not a private ARP detour.
| Site 400a423e: after mute/stale checks, A2 event, A4 resolved Sound,
| A3 native queue bucket, D2 track, A6 the MAIN IRQ frame.
| Decline replays exactly MOVE.L 36(A2),D0; MOVEQ #1,D1. Consume reaches
| 400a471e, where the stock releases this same event once and advances next.
| Never call the native free routine from a subscriber. No additional pool.
        .section .run,"ax"
        .globl core_native_note_accept
core_native_note_accept:
        tst.b core_native_interest+20
        beq.s note_stock
        | D0 is overwritten by stock replay (or native cleanup); only D1/A0/A1
        | are live across this point. Preserve X as well as the IRQ mask.
        lea -32(%sp),%sp
        movem.l %d1/%a0-%a1,(%sp)
        move.w %sr,%d1
        move.w %d1,12(%sp)
        move.l %a2,16(%sp)
        move.l %a4,20(%sp)
        move.l 0x800046ec,%d0
        tst.l (%a3)
        bne.s note_immediate
        move.l 4(%a3),%d0
note_immediate:
        move.l %d0,24(%sp)
        move.l -64(%a6),28(%sp)
        pea 16(%sp)
        pea 20
        jsr core_native
        addq.l #8,%sp
        move.w 12(%sp),%d1
        move.w %d1,%ccr
        movem.l (%sp),%d1/%a0-%a1
        lea 32(%sp),%sp
        tst.l %d0
        bne.s note_consumed
note_stock:
        move.l 36(%a2),%d0
        moveq #1,%d1
        jmp 0x400a4244
note_consumed:
        jmp 0x400a471e
