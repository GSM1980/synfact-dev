| Scalar working projection only; original Sound/sequencer objects untouched.
.text
.globl poly_fast_publish
poly_fast_publish:
 lea -28(%sp),%sp
 movem.l %d2-%d3/%a2-%a6,(%sp)
 movea.l 32(%sp),%a2
 movea.l 36(%sp),%a5
 lea 170(%a2),%a1
 lea 2000(%a2),%a6
 moveq #1,%d2
 moveq #-1,%d3
publish_track:
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
 | Word accesses also cover the alternating two-byte track alignment.
 .set pub_i,0
 .rept 71
 move.w pub_i(%a3),pub_i(%a1)
 .set pub_i,pub_i+2
 .endr
 .set pub_i,0
 .rept 25
 move.l pub_i(%a4),pub_i(%a6)
 .set pub_i,pub_i+4
 .endr
 move.b 100(%a4),100(%a6)
 .set pub_i,104
 .rept 4
 move.l pub_i(%a4),pub_i(%a6)
 .set pub_i,pub_i+4
 .endr
 move.b 120(%a4),120(%a6)
 move.b 121(%a4),121(%a6)
 move.b 122(%a4),122(%a6)
publish_next:
 lea 142(%a1),%a1
 lea 124(%a6),%a6
 addq.l #1,%d2
 cmpi.l #8,%d2
 bne.w publish_track
 movem.l (%sp),%d2-%d3/%a2-%a6
 lea 28(%sp),%sp
 rts
