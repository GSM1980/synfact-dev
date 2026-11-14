| Same scalar projection as the donor, compact copies and cached track bounds.
| No Sound/state writes; leave aux padding 101..103 and 123 untouched.
.text
.globl poly_fast_publish
.globl poly_fast_publish_span
poly_fast_publish:
 move.l #0x010c,%d0
 bra.s publish_enter
poly_fast_publish_span:
 move.l 12(%sp),%d0
publish_enter:
 lea -44(%sp),%sp
 movem.l %d2-%d7/%a2-%a6,(%sp)
 movea.l 48(%sp),%a2
 movea.l 52(%sp),%a5
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
 lea (%a2,%d0.l*2),%a0
 move.w (%a0),(%a2,%d2.l*2)
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
 | Six scratch registers carry 24 aligned bytes per burst. The track bounds
 | and cached master remain in D2-D4, so no per-slave spill is needed.
 .rept 5
 movem.l (%a3),%d0-%d1/%d5-%d7/%a0
 movem.l %d0-%d1/%d5-%d7/%a0,(%a1)
 lea 24(%a3),%a3
 lea 24(%a1),%a1
 .endr
 .rept 5
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
 .rept 4
 movem.l (%a4),%d0-%d1/%d5-%d7/%a0
 movem.l %d0-%d1/%d5-%d7/%a0,(%a6)
 lea 24(%a4),%a4
 lea 24(%a6),%a6
 .endr
 move.l (%a4)+,(%a6)+
 move.b (%a4),(%a6)
 movem.l 4(%a4),%d0-%d1/%d5-%d6
 movem.l %d0-%d1/%d5-%d6,4(%a6)
 move.w 20(%a4),20(%a6)
 move.b 22(%a4),22(%a6)
 | Reuse the master pointers for the next member; destination advances.
 lea -142(%a3),%a3
 lea -100(%a4),%a4
 lea 24(%a6),%a6
 bra.s publish_advance
publish_next:
 lea 142(%a1),%a1
 lea 124(%a6),%a6
publish_advance:
 addq.l #1,%d2
 bra.w publish_track
publish_done:
 movem.l (%sp),%d2-%d7/%a2-%a6
 lea 44(%sp),%sp
 rts

| A single shared slave is always adjacent to its master (opposite parity).
| No per-block owner loop or alignment test is needed for this topology.
.globl poly_fast_publish_one
poly_fast_publish_one:
 lea -32(%sp),%sp
 movem.l %d2/%d5-%d7/%a2-%a5,(%sp)
 movea.l 36(%sp),%a2
 movea.l 40(%sp),%a3
 move.l 44(%sp),%d2
 lsr.l #8,%d2
 moveq #0,%d1
 move.b (%a3,%d2.l),%d1
 lea (%a2,%d1.l*2),%a0
 move.w (%a0),(%a2,%d2.l*2)
 move.l %d2,%d0
 mulu.w #142,%d0
 lea 28(%a2,%d0.l),%a1
 mulu.w #124,%d2
 lea (%a2,%d2.l),%a5
 lea 1876(%a5),%a5
 move.l %d1,%d0
 mulu.w #142,%d0
 lea 28(%a2,%d0.l),%a0
 mulu.w #124,%d1
 lea (%a2,%d1.l),%a4
 lea 1876(%a4),%a4
 .rept 71
 move.w (%a0)+,(%a1)+
 .endr
 .rept 4
 movem.l (%a4),%d0-%d1/%d5-%d7/%a0
 movem.l %d0-%d1/%d5-%d7/%a0,(%a5)
 lea 24(%a4),%a4
 lea 24(%a5),%a5
 .endr
 move.l (%a4)+,(%a5)+
 move.b (%a4),(%a5)
 movem.l 4(%a4),%d0-%d1/%d5-%d6
 movem.l %d0-%d1/%d5-%d6,4(%a5)
 move.w 20(%a4),20(%a5)
 move.b 22(%a4),22(%a5)
 movem.l (%sp),%d2/%d5-%d7/%a2-%a5
 lea 32(%sp),%sp
 rts
