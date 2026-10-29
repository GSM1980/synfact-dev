| Same scalar projection as the donor, compact copies and cached track bounds.
| No Sound/state writes; leave aux padding 101..103 and 123 untouched.
.text
.globl poly_fast_publish
.globl poly_fast_publish_span
poly_fast_publish:
 move.l #0x0108,%d0
 bra.s publish_enter
poly_fast_publish_span:
 move.l 12(%sp),%d0
publish_enter:
 lea -32(%sp),%sp
 movem.l %d2-%d4/%a2-%a6,(%sp)
 movea.l 36(%sp),%a2
 movea.l 40(%sp),%a5
 move.l %d0,%d4
 andi.l #255,%d4
 lsr.l #8,%d0
 move.l %d0,%d2
 mulu.w #142,%d0
 lea 28(%a2,%d0.l),%a1
 move.l %d2,%d0
 mulu.w #124,%d0
 lea (%a2,%d0.l),%a6
 lea 1876(%a6),%a6
 moveq #-1,%d3
publish_track:
 cmp.l %d4,%d2
 bhs.w publish_done
 moveq #0,%d0
 move.b (%a5,%d2.l),%d0
 cmp.l %d2,%d0
 bhs.w publish_next
 move.l %d0,%d1
 lsl.l #1,%d1
 lea (%a2,%d1.l),%a0
 move.l %d2,%d1
 lsl.l #1,%d1
 move.w (%a0),(%a2,%d1.l)
 cmp.l %d3,%d0
 beq.s publish_sources
 move.l %d0,%d3
 move.l %d0,%d1
 mulu.w #142,%d1
 lea 28(%a2,%d1.l),%a3
 mulu.w #124,%d0
 lea (%a2,%d0.l),%a4
 lea 1876(%a4),%a4
publish_sources:
 | Long accesses only when BOTH ends are four-byte aligned. The 142-byte
 | stride alternates alignment. Halfwords handle every other pairing.
 move.l %a3,%d0
 move.l %a1,%d1
 or.l %d1,%d0
 andi.l #3,%d0
 bne.s publish_halves
 .rept 35
 move.l (%a3)+,(%a1)+
 .endr
 move.w (%a3)+,(%a1)+
 bra.w publish_aux
publish_halves:
 .rept 71
 move.w (%a3)+,(%a1)+
 .endr
publish_aux:
 | Aux rows are 124 bytes, aligned at both ends in the native projection.
 .rept 25
 move.l (%a4)+,(%a6)+
 .endr
 move.b (%a4),(%a6)
 lea 4(%a4),%a4
 lea 4(%a6),%a6
 .rept 4
 move.l (%a4)+,(%a6)+
 .endr
 move.w (%a4)+,(%a6)+
 move.b (%a4)+,(%a6)+
 | Reuse the master pointers for the next member; destination advances.
 lea -142(%a3),%a3
 lea -123(%a4),%a4
 addq.l #1,%a6
 bra.s publish_advance
publish_next:
 lea 142(%a1),%a1
 lea 124(%a6),%a6
publish_advance:
 addq.l #1,%d2
 bra.w publish_track
publish_done:
 movem.l (%sp),%d2-%d4/%a2-%a6
 lea 32(%sp),%sp
 rts
