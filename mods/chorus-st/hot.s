| SPDX-License-Identifier: GPL-2.0-or-later
| The chorus's per-block path, MAIN audio IRQ (1.2: hand-written; 1.1 was
| hot.c). One span: the state block (state.inc), then this file's code up to
| chorus_hot_c_end. chorus_place copies the code to MAIN SRAM right after the
| SRAM state block (<syntakt/sram.h>), so the same PC-relative LEA reaches the
| SRAM state from the copy and the DDR state from the original. The span
| keeps no other PC-relative reference outside itself (the emulator test
| checks); kernels.s's code is reached through the state's kernel table, the
| rare paths through absolute calls. The arithmetic is 1.1's, bit for bit.
|
| Entries (dsp.c): chorus_hot_sends (ev_track_send, no EMAC there), then
| chorus_hot_before_delay, chorus_hot_before_reverb, chorus_hot_master_return.
        .include "state.inc"
        .include "coeffs.inc"
        .text
        .balign 4
        .globl chorus_hot_c,chorus_hot_c_end
chorus_hot_c:

| ev_track_send, once a block: each track's target gain, V56's scaling: its
| dormant chorus send (mixer record +106, clamped to 0x7f00), squared, times
| its level x pan (left>>16 + right>>16); 0 when muted. A change marks the
| records for a rebuild (S_DIRTY = 2). Integer multiplies only: the mixer's
| live products are in the EMAC here.
        .globl chorus_hot_sends
chorus_hot_sends:
        lea -20(%sp),%sp
        movem.l %d2-%d5/%a2,(%sp)
        movea.l 24(%sp),%a1             | rows {left, right, mix, 0}, then the mutes
        movea.l chorus_ch,%a0   | target[0]
        move.l 192(%a1),%d5
        moveq #20,%d0
        lsl.l %d0,%d5                   | track t's mute at bit 20+t
        move.l #0x100000,%d3            | track 0 at bit 20; bit 31 is track 11
        moveq #0,%d4                    | new mask at bits 20..31, changes counted at 0..19
.Lsend:
        movea.l 8(%a1),%a2
        move.w 106(%a2),%d0
        beq.s .Lzero
        move.l %d5,%d1
        and.l %d3,%d1
        bne.s .Lzero
        cmpi.w #0x7f00,%d0
        bls.s 1f
        move.w #0x7f00,%d0
1:      mulu.w %d0,%d0
        add.l %d0,%d0
        clr.w %d0
        swap %d0                        | gain(raw) >> 16
        move.l (%a1),%d1
        swap %d1
        ext.l %d1
        move.l 4(%a1),%d2
        swap %d2
        ext.l %d2
        add.l %d2,%d1
        muls.l %d0,%d1
        beq.s .Lzero
        or.l %d3,%d4
        cmp.l (%a0),%d1
        beq.s .Lnext
        move.l %d1,(%a0)
        addq.l #1,%d4
        bra.s .Lnext
.Lzero:
        tst.l (%a0)
        beq.s .Lnext
        clr.l (%a0)
        addq.l #1,%d4
.Lnext:
        addq.l #4,%a0
        lea 16(%a1),%a1
        add.l %d3,%d3
        bcc.s .Lsend
        move.l %d4,%d0
        andi.l #0xfffff,%d0
        beq.s 2f
        movea.l chorus_ch,%a0
        moveq #20,%d0
        lsr.l %d0,%d4
        move.l %d4,S_TMASK(%a0)
        moveq #2,%d0
        move.l %d0,S_DIRTY(%a0)
2:      movem.l (%sp),%d2-%d5/%a2
        lea 20(%sp),%sp
        rts

| ev_delay_in: the mono send mix, the chorus, the DEL return. Asleep, a
| raised send is not activity: the chorus wakes when a track with a send has
| sound. The mix's MACSR (0xa0: saturating fractional) stays for the chain.
        .globl chorus_hot_before_delay
chorus_hot_before_delay:
        movea.l chorus_ch,%a0
        move.l S_TMASK(%a0),%d0
        or.l S_CMASK(%a0),%d0
        bne.s 1f
        tst.l S_IDLE(%a0)
        beq.s 1f
        rts                             | asleep with no send: nothing
1:      lea -44(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        movea.l %a0,%a6
        move.l %d0,%d7                  | mask
        tst.l S_IDLE(%a6)
        beq.s .Lawake
        jsr chorus_cold_wake            | d0 = 1 when a send track has sound
        tst.l %d0
        bne.s .Lawake
        jsr chorus_cold_settle          | gains settle without waking
        bra .Lout
.Lawake:
        move.l #0xa0,%macsr
        moveq #0,%d0
        move.l %d0,%acc0
        move.l %d0,%acc1
        move.l %d0,%acc2
        move.l %d0,%acc3
        tst.l S_DIRTY(%a6)
        beq.s 2f
        movea.l S_TAB+K_BUILD(%a6),%a0
        jsr (%a0)
2:      tst.l S_NGRP(%a6)
        beq.s 4f
        movea.l S_TAB+K_MIXS(%a6),%a0
        tst.l S_RAMP(%a6)
        beq.s 3f
        movea.l S_TAB+K_MIXR(%a6),%a0
3:      jsr (%a0)
        bra.s 5f
4:      jsr chorus_cold_silence         | no send: a silent block (a tail)
5:      lea chorus_in+96,%a0
        moveq #31,%d1
51:     tst.l (%a0)+
        bne.s .Llive
        subq.l #1,%d1
        bpl.s 51b
        moveq #0,%d6
        tst.l S_IDLE(%a6)
        bne .Lout
        bra.s .Lsettings
.Llive: moveq #1,%d6
| The Kit's settings, decoded again (cold) only when the record or one of
| its eight words changed.
.Lsettings:
        movea.l 0x800030bc,%a0
        move.l %a0,%d0
        beq.s .Ldecode
        cmpa.l S_FX(%a6),%a0
        bne.s .Ldecode
        lea S_KEY(%a6),%a1
        move.w (%a0),%d0
        cmp.w (%a1)+,%d0
        bne.s .Ldecode
        move.w 0x34(%a0),%d0
        cmp.w (%a1)+,%d0
        bne.s .Ldecode
        move.w 0x48(%a0),%d0
        cmp.w (%a1)+,%d0
        bne.s .Ldecode
        move.w 0x4e(%a0),%d0
        cmp.w (%a1)+,%d0
        bne.s .Ldecode
        move.w 0x52(%a0),%d0
        cmp.w (%a1)+,%d0
        bne.s .Ldecode
        move.w 0x54(%a0),%d0
        cmp.w (%a1)+,%d0
        bne.s .Ldecode
        move.w 0x6e(%a0),%d0
        cmp.w (%a1)+,%d0
        bne.s .Ldecode
        move.w 0x8c(%a0),%d0
        cmp.w (%a1)+,%d0
        beq.s .Lrender
.Ldecode:
        jsr chorus_cold_decode          | (a0 = record or 0)
.Lrender:
        clr.l S_IDLE(%a6)
        tst.l S_INIT(%a6)
        bne.s 1f
        jsr chorus_cold_reset
1:      move.l S_POST(%a6),%d0
        subq.l #1,%d0
        bne.s 2f
        bsr up_history                  | the last ev_main_mix did not come
2:      clr.l S_POST(%a6)
        move.l %d6,-(%sp)               | live, across the kernels
        movea.l S_TAB+K_DEC(%a6),%a0    | 48 -> 24 kHz into the ring at write_pos
        jsr (%a0)
| The one-pole HPF in place on the ring's new 16 samples:
| y = d2 (x - x') + c y', d2 = c/2 + 1/2.
        move.l S_WPOS(%a6),%d0
        movea.l #chorus_ring,%a0
        lea (0,%a0,%d0.l*4),%a0
        move.l S_HPC(%a6),%d1
        move.l %d1,%d2
        asr.l #1,%d2
        addi.l #0x40000000,%d2
        move.l S_HP(%a6),%d3
        move.l S_HP+4(%a6),%d5
        moveq #8,%d4
.Lhp:   msac.l %d2,%d3,(%a0),%d3,%acc0
        mac.l %d2,%d3,%acc0
        mac.l %d1,%d5,%acc0
        movclr.l %acc0,%d5
        move.l %d5,(%a0)+
        msac.l %d2,%d3,(%a0),%d3,%acc0
        mac.l %d2,%d3,%acc0
        mac.l %d1,%d5,%acc0
        movclr.l %acc0,%d5
        move.l %d5,(%a0)+
        subq.l #1,%d4
        bne.s .Lhp
        move.l %d3,S_HP(%a6)
        move.l %d5,S_HP+4(%a6)
        moveq #48,%d1                   | the ring's mirror (cold), w < 48
        cmp.l %d1,%d0
        bcc.s 1f
        lea -64(%a0),%a0
        jsr chorus_cold_mirror
| The decimator's history: its last 24 inputs.
1:      lea chorus_in,%a0
        movem.l 128(%a0),%d0-%d7
        movem.l %d0-%d7,(%a0)
        movem.l 160(%a0),%d0-%d7
        movem.l %d0-%d7,32(%a0)
        movem.l 192(%a0),%d0-%d7
        movem.l %d0-%d7,64(%a0)
        movea.l S_TAB+K_MOD(%a6),%a0
        jsr (%a0)
        movea.l S_TAB+K_DELAY(%a6),%a0
        jsr (%a0)
        move.l S_WPOS(%a6),%d0
        moveq #16,%d1
        add.l %d1,%d0
        andi.l #255,%d0
        move.l %d0,S_WPOS(%a6)
| The wet: with sound in, DEL and REV off and VOL steady, ev_main_mix makes
| it fused with VOL's return (S_POST 1), or nothing needs it (2). Else now.
        move.l (%sp)+,%d6
        beq.s 2f
        move.l S_RETG(%a6),%d0
        or.l S_DES(%a6),%d0
        or.l S_RETG+4(%a6),%d0
        or.l S_DES+4(%a6),%d0
        bne.s 2f
        move.l S_RETG+8(%a6),%d0
        cmp.l S_DES+8(%a6),%d0
        bne.s 2f
        moveq #1,%d1
        tst.l %d0
        bne.s 1f
        moveq #2,%d1
        bsr up_history
1:      move.l %d1,S_POST(%a6)
        clr.l S_QUIET(%a6)              | sound in; DEL is off
        bra.s .Lout
2:      suba.l %a1,%a1
        movea.l S_TAB+K_UP(%a6),%a0
        jsr (%a0)
        bsr up_history
| Sleep after 64 blocks of silence in and below -132 dBFS out (the filters'
| limit cycle is about 388 Q31 units; any input keeps it awake).
        tst.l %d6
        bne.s 2f
        jsr chorus_cold_quiet           | d0 = 1: asleep now
        tst.l %d0
        bne.s .Lout
        bra.s 3f
2:      clr.l S_QUIET(%a6)
3:      movea.l #0x8000dbd0,%a1         | ST_FX_SEND_BUS: DEL
        moveq #0,%d7
        moveq #0,%d6
        bsr.s add_return
.Lout:  movem.l (%sp),%d2-%d7/%a2-%a6
        lea 44(%sp),%sp
        rts

| A return: bus += g x wet (VOL: bus -= g x wet, the FX bus is inverted), g
| stepping from its last value to the setting's over the block. A1 = bus,
| D7 = 4 x which, D6 = subtract, A6 = state; D0-D5/A0-A4 scratch.
add_return:
        tst.l S_IDLE(%a6)
        bne.s 9f
        lea (S_RETG,%a6,%d7.l),%a2
        move.l (%a2),%d0
        move.l 12(%a2),%d1
        move.l %d0,%d2
        or.l %d1,%d2
        beq.s 9f
        move.l %d1,(%a2)
        move.l %d1,%d2
        sub.l %d0,%d2
        asr.l #5,%d2
        bne.s 5f
        cmp.l %d0,%d1
        bne.s 5f
        tst.l %d6
        beq.s 1f
        neg.l %d0
1:      movea.l S_TAB+K_RET(%a6),%a0
        jmp (%a0)
5:      jmp chorus_cold_ramp_return
9:      rts

        .globl chorus_hot_before_reverb,chorus_hot_master_return
chorus_hot_before_reverb:
        movea.l chorus_ch,%a0
        tst.l S_IDLE(%a0)
        bne.s 9f
        lea -44(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        movea.l %a0,%a6
        movea.l #0x8000dbd0,%a1         | ST_FX_SEND_BUS: REV
        moveq #4,%d7
        moveq #0,%d6
        bra.s 1f
chorus_hot_master_return:
        movea.l chorus_ch,%a0
        tst.l S_IDLE(%a0)
        bne.s 9f
        lea -44(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        movea.l %a0,%a6
        movea.l #0x8000dad0,%a1         | ST_FX_TRACK_BUS: VOL
        moveq #1,%d0
        cmp.l S_POST(%a6),%d0
        bne.s 2f
        clr.l S_POST(%a6)               | the wet, fused with VOL's return
        move.l S_RETG+8(%a6),%d0
        neg.l %d0
        movea.l S_TAB+K_UP(%a6),%a0
        jsr (%a0)
        bsr.s up_history
        bra.s 3f
2:      moveq #8,%d7
        moveq #1,%d6
1:      bsr.w add_return
3:      movem.l (%sp),%d2-%d7/%a2-%a6
        lea 44(%sp),%sp
9:      rts

| The reconstructor's history: its last 8 frames. May change D0-D7/A0.
up_history:
        lea chorus_up,%a0
        movem.l 128(%a0),%d0-%d7
        movem.l %d0-%d7,(%a0)
        movem.l 160(%a0),%d0-%d7
        movem.l %d0-%d7,32(%a0)
        rts

| Kernel: the return at a stable gain. A1 = bus, D0 = signed gain. The EMAC
| starts at the integer bus value: one truncated product added, one
| saturation (1.1: the product, then a saturating add; the same result).
        .globl chorus_k_ret
chorus_k_ret:
        movea.l %d0,%a3
        lea chorus_wet,%a0
        lea 256(%a1),%a4
.Lrs:   movem.l (%a1),%d0-%d3
        move.l %d0,%acc0
        move.l %d1,%acc1
        move.l %d2,%acc2
        move.l %d3,%acc3
        move.l (%a0)+,%d4
        mac.l %a3,%d4,(%a0)+,%d4,%acc0
        mac.l %a3,%d4,(%a0)+,%d4,%acc1
        mac.l %a3,%d4,(%a0)+,%d4,%acc2
        mac.l %a3,%d4,%acc3
        movclr.l %acc0,%d0
        movclr.l %acc1,%d1
        movclr.l %acc2,%d2
        movclr.l %acc3,%d3
        movem.l %d0-%d3,(%a1)
        lea 16(%a1),%a1
        cmpa.l %a4,%a1
        bne.s .Lrs
        rts

| Kernel: the delay modulation, once a block (1.1's arithmetic): the depth and
| width settle, a triangle per side (R offset by the width), the c^4 pole, the
| Digitone's shaping curve; each tap's block runs from its last node to the
| new one.
        .equ C4,1588359364
        .equ KFRAC,1256185306
        .globl chorus_k_mod
chorus_k_mod:
        moveq #0,%d0
        move.w S_VAL+6(%a6),%d0
        swap %d0                        | width
        moveq #0,%d1
        move.w S_VAL(%a6),%d1
        swap %d1                        | depth
        move.l #0x05000000,%d2
        move.l #0x7b000000,%d3
        move.l S_DRAW(%a6),%d4
        move.l S_WIDTH(%a6),%d5
        mac.l %d2,%d1,%acc0
        mac.l %d3,%d4,%acc1
        mac.l %d2,%d0,%acc2
        mac.l %d3,%d5,%acc3
        move.l %d1,S_DRAW(%a6)
        moveq #0,%d6
        move.w S_VAL+2(%a6),%d6
        swap %d6                        | speed
        move.l #252329,%d7
        movclr.l %acc0,%d4
        movclr.l %acc1,%d5
        add.l %d5,%d4                   | next
        mac.l %d7,%d6,%acc0
        movclr.l %acc2,%d5
        movclr.l %acc3,%d0
        add.l %d0,%d5                   | width
        move.l %d5,S_WIDTH(%a6)
        move.l #75544827,%d7
        mac.l %d7,%d4,%acc1
        movclr.l %acc0,%d6
        addi.l #25054,%d6
        lsl.l #5,%d6                    | increment
        movclr.l %acc1,%d4              | amplitude
        move.l %d4,S_AMP(%a6)
        move.l S_PHASE(%a6),%d0
        add.l %d6,%d0
        move.l %d0,S_PHASE(%a6)         | side 0's phase; side 1 adds the width
        lea S_PTRI(%a6),%a1
        lea S_POS(%a6),%a5
        movea.l #chorus24_lut,%a2
        movea.l #KFRAC,%a3
        movea.l #C4,%a4
        moveq #1,%d7
        swap %d7                        | side counter in the high word
.Lside: move.l %d0,%d1
        add.l %d1,%d1
        bcc.s 1f
        not.l %d1
1:      lsr.l #1,%d1                    | triangle
        move.l (%a1),%d2
        mac.l %d4,%d2,%acc0
        mac.l %d4,%d1,%acc1
        move.l %d1,(%a1)
        move.l 8(%a1),%d3
        mac.l %a4,%d3,%acc3
        movclr.l %acc0,%d2
        movclr.l %acc1,%d6
        add.l %d6,%d2
        sats.l %d2                      | x
        mac.l %a3,%d2,%acc2
        move.l %d2,%d6
        add.l %d6,%d6
        sats.l %d6
        add.l %d2,%d6
        sats.l %d6
        movclr.l %acc2,%d3
        add.l %d3,%d6
        sats.l %d6                      | k
        movclr.l %acc3,%d3
        add.l %d3,%d6
        sats.l %d6                      | y
        move.l %d6,8(%a1)
        move.l %d6,%d3
        moveq #26,%d2
        lsr.l %d2,%d3
        moveq #30,%d2
        cmp.l %d2,%d3
        bls.s 2f
        move.l %d2,%d3
2:      move.l (0,%a2,%d3.l*4),%d2
        move.l (4,%a2,%d3.l*4),%d3
        sub.l %d2,%d3
        andi.l #0x03ffffff,%d6
        lsl.l #5,%d6
        mac.l %d6,%d3,%acc0
        movclr.l %acc0,%d3
        add.l %d2,%d3
        asr.l #1,%d3
        subi.l #0x00c00000,%d3          | the new node
        move.l 16(%a1),(%a5)+
        move.l %d3,(%a5)+
        move.l %d3,16(%a1)
        addq.l #4,%a1
        add.l %d5,%d0
        sub.l #0x10000,%d7
        bpl.w .Lside
        rts
| 48 -> 24 kHz into the ring at write_pos: sixteen taps (design.json) in two
| passes of eight, coefficients in registers, two outputs per pass sharing
| their loads. Pass one leaves saturated partial sums in S_WET, pass two adds
| the rest to them and doubles, saturating (1.1's grouping and rounding).
        .globl chorus_k_dec
chorus_k_dec:
        move.l %a6,-(%sp)
        DEC_COEFFS_1
        lea chorus_in+96,%a0
        lea chorus_wet,%a1
        lea 128(%a0),%a2
.Ld1:   move.l 4(%a0),%d0
        mac.l %d1,%d0,(%a0),%d0,%acc1
        mac.l %d2,%d0,-4(%a0),%d0,%acc1
        mac.l %d1,%d0,%acc0
        mac.l %d3,%d0,-8(%a0),%d0,%acc1
        mac.l %d2,%d0,%acc0
        mac.l %d4,%d0,-12(%a0),%d0,%acc1
        mac.l %d3,%d0,%acc0
        mac.l %d5,%d0,-16(%a0),%d0,%acc1
        mac.l %d4,%d0,%acc0
        mac.l %d6,%d0,-20(%a0),%d0,%acc1
        mac.l %d5,%d0,%acc0
        mac.l %d7,%d0,-24(%a0),%d0,%acc1
        mac.l %d6,%d0,%acc0
        mac.l %a3,%d0,-28(%a0),%d0,%acc1
        mac.l %d7,%d0,-32(%a0),%d0,%acc0
        mac.l %a3,%d0,%acc0
        movclr.l %acc0,%d0
        move.l %d0,(%a1)+
        movclr.l %acc1,%d0
        move.l %d0,(%a1)+
        lea 16(%a0),%a0
        cmpa.l %a2,%a0
        bne.s .Ld1
        DEC_COEFFS_2
        lea chorus_in+96,%a0
        lea chorus_wet,%a1
        move.l S_WPOS(%a6),%d0
        movea.l #chorus_ring,%a2
        lea (0,%a2,%d0.l*4),%a2
        lea 128(%a0),%a4
.Ld2:   move.l (%a1)+,%d0
        move.l %d0,%acc0
        move.l (%a1)+,%d0
        move.l %d0,%acc1
        move.l -28(%a0),%d0
        mac.l %d1,%d0,-32(%a0),%d0,%acc1
        mac.l %d2,%d0,-36(%a0),%d0,%acc1
        mac.l %d1,%d0,%acc0
        mac.l %d3,%d0,-40(%a0),%d0,%acc1
        mac.l %d2,%d0,%acc0
        mac.l %d4,%d0,-44(%a0),%d0,%acc1
        mac.l %d3,%d0,-48(%a0),%d0,%acc0
        mac.l %d4,%d0,%acc0
        mac.l %d5,%d0,-52(%a0),%d0,%acc1
        mac.l %d6,%d0,-56(%a0),%d0,%acc1
        mac.l %d5,%d0,%acc0
        mac.l %d7,%d0,-60(%a0),%d0,%acc1
        mac.l %d6,%d0,-64(%a0),%d0,%acc0
        mac.l %d7,%d0,-68(%a0),%d0,%acc0
        mac.l %a3,%d0,-76(%a0),%d0,%acc1
        mac.l %a3,%d0,%acc0
        movclr.l %acc0,%d0
        add.l %d0,%d0
        sats.l %d0
        move.l %d0,(%a2)+
        movclr.l %acc1,%d0
        add.l %d0,%d0
        sats.l %d0
        move.l %d0,(%a2)+
        lea 16(%a0),%a0
        cmpa.l %a4,%a0
        bne.s .Ld2
        movea.l (%sp)+,%a6
        rts

chorus_hot_c_end:
