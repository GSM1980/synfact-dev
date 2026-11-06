| SPDX-License-Identifier: GPL-2.0-or-later
| Mixer FX-send events, MAIN audio interrupt (1500 blocks a second).
| Subscribers do no IO, allocation or waiting; buses are in <syntakt/mix.h>.
| With no subscriber each site costs a LEA, a TST and a branch, then stock.
        .section .run,"ax"

| ev_track_send: void f(const struct st_track_sends *t), once per block, at
| 4008f3a0 of track 11, when every track's level x pan gains (native inverted
| Q31) and its delay send were computed (<syntakt/mix.h>). For each track 0..11
| the stub stores left/right and its 142-byte mixer record in the core's table
| (s16 at +106: the dormant Digitone chorus send, +118 DEL, +120 REV, +122 PAN,
| +124 LEV), then the native send-mute bits once. The EMAC accumulators hold
| the mixer's live products here: subscribers must not use EMAC instructions
| or change MACSR. D0-D3 are the native ones on return; the stub replays the
| two displaced MSACs first. With no subscriber: a LEA, a TST and a branch.
        .globl core_track_send,core_track_send_table,core_track_sends
core_track_send:
        .word 0xae00,0x0900,0xae81,0x0900    | msac.l %d0,%d7,%acc0 / %d1,%d7,%acc1
core_track_send_table:
        lea ev_track_send,%a0
        tst.l (%a0)
        bne.s 1f
        jmp 0x4008f3a8
1:      lea core_track_sends,%a1                 | A1 is rewritten at 4008f3a8
        lea (%a1,%d2.l*4),%a1                   | D2 = 4 x track: 16-byte rows
        move.l %d0,(%a1)+
        move.l %d1,(%a1)+
        move.l %a3,(%a1)
        cmpi.l #11,%d3
        bne.s 4f
        lea -16(%sp),%sp
        movem.l %d0-%d1/%a1-%a2,(%sp)
        move.l -508(%a6),%d0                    | the mixer's send mute bits
        move.l %d0,core_track_sends+192
        movea.l %a0,%a2
2:      move.l (%a2)+,%d0
        beq.s 3f
        movea.l %d0,%a0
        pea core_track_sends
        jsr (%a0)
        addq.l #4,%sp
        bra.s 2b
3:      movem.l (%sp),%d0-%d1/%a1-%a2
        lea 16(%sp),%sp
4:      jmp 0x4008f3a8

| struct st_track_sends: 12 rows {left, right, mix record, 0}, then the mutes
        .section .bss
        .balign 4
core_track_sends:
        .space 196
        .section .run,"ax"

| ev_delay_in: void f(void), at 4008f828, when the FX send bus ST_FX_SEND_BUS
| holds the block's complete delay input, before the native delay renders it.
        .globl core_delay_in,core_delay_in_table
core_delay_in:
core_delay_in_table:
        lea ev_delay_in,%a0
        tst.l (%a0)
        bne.s 1f
        jmp 0x40096890
1:      move.l %a2,-(%sp)
        movea.l %a0,%a2
2:      move.l (%a2)+,%d0
        beq.s 3f
        movea.l %d0,%a0
        jsr (%a0)
        bra.s 2b
3:      movea.l (%sp)+,%a2
        jmp 0x40096890

| ev_reverb_in: void f(void), at 4008f952, when ST_FX_SEND_BUS holds the
| block's complete reverb input, before the native reverb renders it.
        .globl core_reverb_in,core_reverb_in_table
core_reverb_in:
core_reverb_in_table:
        lea ev_reverb_in,%a0
        tst.l (%a0)
        bne.s 1f
        jmp 0x40096e3c
1:      move.l %a2,-(%sp)
        movea.l %a0,%a2
2:      move.l (%a2)+,%d0
        beq.s 3f
        movea.l %d0,%a0
        jsr (%a0)
        bra.s 2b
3:      movea.l (%sp)+,%a2
        jmp 0x40096e3c
