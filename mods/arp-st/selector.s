| SPDX-License-Identifier: GPL-2.0-or-later
| Authored integer selector; same state transitions as selector.c.
| A0=private selector, A1=settings, D4=direction, D5=logical bitmap word.
| D7=0 UP/DOWN; 1 first CYCL selection; 2 second CYCL selection.
.text
.globl arp_next
arp_next:
 movea.l 4(%sp),%a0
 move.l 12(%sp),%d0
 cmp.l 148(%a0),%d0
 bne.w arp_next_stale
 tst.l 144(%a0)
 beq.w arp_next_stale
 movea.l 8(%sp),%a1
 mvz.b 4(%a1),%d0
 cmpi.l #16,%d0
 bcc.w arp_next_stale
 move.l 152(%a0),%d1
 addq.l #1,%d1
 cmp.l %d0,%d1
 ble.s arp_step_ready
 clr.l %d1
arp_step_ready:
 move.l %d1,152(%a0)
 mvz.w 6(%a1),%d0
 btst %d1,%d0
 beq.w arp_next_rest
 mvz.b (%a1),%d0
 beq.w arp_next_rest
 lea -24(%sp),%sp
 movem.l %d2-%d7,(%sp)
 cmpi.l #1,%d0
 beq.w arp_true
 move.l 156(%a0),%d5
 cmpi.l #3,%d0
 bhi.w arp_cycle
 | Physical word = logical word XOR (0 for UP, 3 for DOWN).
 moveq #0,%d7
 moveq #0,%d4
 cmpi.l #2,%d0
 beq.s arp_ud_start
 moveq #3,%d4
arp_ud_start:
 move.l %d5,%d6
 eor.l %d4,%d6
 move.l (%a0,%d6.l*4),%d3
 and.l 176(%a0),%d3
 bne.s arp_ud_choose
 moveq #-1,%d0
 move.l %d0,176(%a0)
 | At most four bitmap words: fixed stages remove loop bookkeeping in the
 | sparse one-note case without another eligibility test or cached state.
 .rept 4
 addq.l #1,%d5
 moveq #3,%d0
 and.l %d0,%d5
 move.l %d5,156(%a0)
 bne.s 1f
 bsr.w arp_octave
1:
 move.l %d5,%d6
 eor.l %d4,%d6
 move.l (%a0,%d6.l*4),%d3
 bne.w arp_ud_choose
 .endr
 bra.w arp_saved_rest
arp_ud_choose:
 tst.l %d4
 beq.w arp_choose_low
 bra.w arp_choose_high

arp_cycle:
 move.l 160(%a0),%d4
 moveq #1,%d7
arp_cycle_again:
 move.l %d5,%d6
 tst.l %d4
 bne.s arp_cycle_initial
 moveq #3,%d6
 sub.l %d5,%d6
arp_cycle_initial:
 move.l (%a0,%d6.l*4),%d3
 and.l 176(%a0),%d3
 bne.s arp_cycle_choose
 moveq #-1,%d0
 move.l %d0,176(%a0)
 moveq #8,%d2
arp_cycle_scan:
 addq.l #1,%d5
 moveq #3,%d0
 and.l %d0,%d5
 move.l %d5,156(%a0)
 bne.s arp_cycle_word
 tst.l %d4
 beq.s arp_cycle_up
 clr.l %d4
 clr.l 160(%a0)
 bra.s arp_cycle_word
arp_cycle_up:
 bsr.w arp_octave
 moveq #1,%d4
 move.l %d4,160(%a0)
arp_cycle_word:
 move.l %d5,%d6
 tst.l %d4
 bne.s arp_cycle_mask
 moveq #3,%d6
 sub.l %d5,%d6
arp_cycle_mask:
 move.l (%a0,%d6.l*4),%d3
 bne.s arp_cycle_choose
 subq.l #1,%d2
 bne.s arp_cycle_scan
 moveq #-1,%d0
 bra.w arp_selector_selected
arp_cycle_choose:
 tst.l %d4
 beq.s arp_choose_high
arp_choose_low:
 move.l %d3,%d1
 bitrev %d1
 ff1 %d1
 move.l %d1,%d3
 addq.l #1,%d3
 moveq #-1,%d0
 | ColdFire register shifts by 32 produce zero, as the C helpers require.
 lsl.l %d3,%d0
 move.l %d0,176(%a0)
 bra.s arp_note_word
arp_choose_high:
 ff1 %d3
 moveq #31,%d1
 sub.l %d3,%d1
 addq.l #1,%d3
 moveq #-1,%d0
 lsr.l %d3,%d0
 move.l %d0,176(%a0)
arp_note_word:
 move.l %d6,%d0
 lsl.l #5,%d0
 add.l %d1,%d0
 tst.l %d7
 beq.s arp_selector_selected
 cmp.l 164(%a0),%d0
 bne.s arp_selector_selected
 cmpi.l #2,%d7
 beq.s arp_selector_selected
 moveq #2,%d7
 bra.w arp_cycle_again

arp_true:
 move.l 168(%a0),%d5
 tst.l %d5
 bmi.s arp_true_advance
 move.l %d5,%d0
 addq.l #1,%d0
 cmp.l 144(%a0),%d0
 bcs.s arp_true_advance
 bsr.w arp_octave
arp_true_advance:
 addq.l #1,%d5
 cmp.l 144(%a0),%d5
 bcs.s arp_true_ready
 clr.l %d5
arp_true_ready:
 move.l %d5,168(%a0)
 mvz.b 16(%a0,%d5.l),%d0
arp_selector_selected:
 move.l %d0,164(%a0)
 move.l 172(%a0),%d1
 moveq #12,%d2
 muls.l %d2,%d1
 add.l %d1,%d0
 move.l 152(%a0),%d1
 mvs.b 8(%a1,%d1.l),%d1
 add.l %d1,%d0
arp_next_restore:
 movem.l (%sp),%d2-%d7
 lea 24(%sp),%sp
 rts
arp_saved_rest:
 moveq #-1,%d0
 bra.s arp_next_restore
arp_next_stale:
 moveq #-2,%d0
 rts
arp_next_rest:
 moveq #-1,%d0
 rts

| Updates octave only; preserves D2-D7 and both pointers.
arp_octave:
 mvz.b 2(%a1),%d0
 move.l 172(%a0),%d1
 cmpi.l #3,%d0
 bhi.s arp_octave_down
 addq.l #1,%d1
 cmp.l %d0,%d1
 ble.s arp_octave_store
 clr.l %d1
 bra.s arp_octave_store
arp_octave_down:
 subq.l #1,%d1
 neg.l %d0
 addq.l #3,%d0
 cmp.l %d0,%d1
 bge.s arp_octave_store
 clr.l %d1
arp_octave_store:
 move.l %d1,172(%a0)
 rts
.globl arp_next_end
arp_next_end:
.size arp_next,arp_next_end-arp_next
