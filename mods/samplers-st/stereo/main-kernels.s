| SPDX-License-Identifier: GPL-2.0-or-later
| MAIN copies and fractional EMAC mixing, one to four active tracks per pass.
| The caller preserves physical EMAC state. All sample sums and their order
| match the four-lane mixer; zero padding never changes the accumulator.
| References leaving a movable span are absolute bank relocations.
.text
.balignw 4,0x4e71
.globl st_stereo_copy_begin,st_stereo_copy32,st_stereo_copy64,st_stereo_copy_end
st_stereo_copy_begin:
st_stereo_copy64:
 lea -8(%sp),%sp
 movem.l %d2-%d3,(%sp)
 movea.l 12(%sp),%a0
 movea.l 16(%sp),%a1
 movem.l (%a0),%d0-%d3
 movem.l %d0-%d3,(%a1)
 movem.l 16(%a0),%d0-%d3
 movem.l %d0-%d3,16(%a1)
 lea 32(%a0),%a0
 lea 32(%a1),%a1
 bra.s stereo_copy_tail
st_stereo_copy32:
 lea -8(%sp),%sp
 movem.l %d2-%d3,(%sp)
 movea.l 12(%sp),%a0
 movea.l 16(%sp),%a1
stereo_copy_tail:
 movem.l (%a0),%d0-%d3
 movem.l %d0-%d3,(%a1)
 movem.l 16(%a0),%d0-%d3
 movem.l %d0-%d3,16(%a1)
 movem.l (%sp),%d2-%d3
 lea 8(%sp),%sp
 rts

| Expand exactly the selected tracks: signed mantissa << per-track shift.
| Eight samples per iteration, four independent data registers and one
| MOVEM store per quartet. No EMAC use, no read beyond a selected track.
.globl st_stereo_widen
st_stereo_widen:
 lea -28(%sp),%sp
 movem.l %d2-%d7/%a2,(%sp)
 movea.l 32(%sp),%a2
 movea.l 36(%sp),%a1
 lea 20(%a2),%a0
 move.l 4(%a2),%d4
 moveq #0,%d6
 tst.l %d4
 beq.w stereo_widen_done
stereo_widen_track:
 lsr.l #1,%d4
 bcc.s stereo_widen_skip
 moveq #0,%d5
 move.b 12(%a2,%d6.l),%d5
 moveq #4,%d7
stereo_widen_eight:
 .rept 2
  mvs.w (%a0)+,%d0
  mvs.w (%a0)+,%d1
  mvs.w (%a0)+,%d2
  mvs.w (%a0)+,%d3
  lsl.l %d5,%d0
  lsl.l %d5,%d1
  lsl.l %d5,%d2
  lsl.l %d5,%d3
  movem.l %d0-%d3,(%a1)
  lea 16(%a1),%a1
 .endr
 subq.l #1,%d7
 bne.s stereo_widen_eight
 bra.s stereo_widen_next
stereo_widen_skip:
 lea 64(%a0),%a0
 lea 128(%a1),%a1
stereo_widen_next:
 addq.l #1,%d6
 tst.l %d4
 bne.s stereo_widen_track
stereo_widen_done:
 movem.l (%sp),%d2-%d7/%a2
 lea 28(%sp),%sp
 rts
.balignw 4,0x4e71
st_stereo_copy_end:

.balignw 4,0x4e71
.globl st_stereo_mix_begin,st_stereo_mix4,st_stereo_mix_end
| (right bus, four padded D pointers, gains, steps, active count 1..4).
| Keep full 32-bit gains and the exact doubled MSAC per track. Saturate once
| at the end of each pass, just as in the original four-lane implementation.
| The second MSAC can fetch the NEXT track's sample in parallel; no read
| goes past a sample vector. Stable gains omit all 32 per-frame step updates.
.macro stereo_mix_sample count, ramp
 move.l (%a0),%d0
 move.l %d0,%acc0
 move.l (%a1)+,%d0
 msac.l %d2,%d0,%acc0
 .if \count > 1
  msac.l %d2,%d0,(%a2)+,%d0,%acc0
  msac.l %d3,%d0,%acc0
  .if \count > 2
   msac.l %d3,%d0,(%a3)+,%d0,%acc0
   msac.l %d4,%d0,%acc0
   .if \count > 3
    msac.l %d4,%d0,(%a4)+,%d0,%acc0
    msac.l %d5,%d0,%acc0
    msac.l %d5,%d0,%acc0
   .else
    msac.l %d4,%d0,%acc0
   .endif
  .else
   msac.l %d3,%d0,%acc0
  .endif
 .else
  msac.l %d2,%d0,%acc0
 .endif
 .if \ramp
  add.l (%a5),%d2
  .if \count > 1
   add.l 4(%a5),%d3
  .endif
  .if \count > 2
   add.l 8(%a5),%d4
  .endif
  .if \count > 3
   add.l 12(%a5),%d5
  .endif
 .endif
 movclr.l %acc0,%d0
 move.l %d0,(%a0)
 addq.l #8,%a0
.endm
.macro stereo_mix_loop count, ramp
stereo_mix_\count\()_\ramp:
 stereo_mix_sample \count,\ramp
 stereo_mix_sample \count,\ramp
 subq.l #1,%d1
 bne.w stereo_mix_\count\()_\ramp
 bra.w stereo_mix_done
.endm
st_stereo_mix_begin:
st_stereo_mix4:
 lea -44(%sp),%sp
 movem.l %d2-%d7/%a2-%a6,(%sp)
 movea.l 48(%sp),%a0
 movea.l 52(%sp),%a1
 movea.l 56(%sp),%a6
 movea.l 60(%sp),%a5
 movem.l (%a1),%a1-%a4
 movem.l (%a6),%d2-%d5
 moveq #16,%d1
 move.l 64(%sp),%d6
 move.l (%a5),%d0
 or.l 4(%a5),%d0
 or.l 8(%a5),%d0
 or.l 12(%a5),%d0
 bne.w stereo_mix_ramp_dispatch
 cmpi.l #1,%d6
 beq.w stereo_mix_1_0
 cmpi.l #2,%d6
 beq.w stereo_mix_2_0
 cmpi.l #3,%d6
 beq.w stereo_mix_3_0
 bra.w stereo_mix_4_0
stereo_mix_ramp_dispatch:
 cmpi.l #1,%d6
 beq.w stereo_mix_1_1
 cmpi.l #2,%d6
 beq.w stereo_mix_2_1
 cmpi.l #3,%d6
 beq.w stereo_mix_3_1
 bra.w stereo_mix_4_1
 stereo_mix_loop 1,0
 stereo_mix_loop 2,0
 stereo_mix_loop 3,0
 stereo_mix_loop 4,0
 stereo_mix_loop 1,1
 stereo_mix_loop 2,1
 stereo_mix_loop 3,1
 stereo_mix_loop 4,1
stereo_mix_done:
 movem.l (%sp),%d2-%d7/%a2-%a6
 lea 44(%sp),%sp
 rts
st_stereo_mix_end:

| Commit all four gain rows only after their native kernels and stereo
| corrections have run. The empty MAIN callback uses this small movable
| span; the active worker uses an inlined copy inside its saved frame.
.globl st_stereo_remember_begin,st_stereo_remember,st_stereo_remember_end
st_stereo_remember_begin:
st_stereo_remember:
 lea -24(%sp),%sp
 movem.l %d2-%d7,(%sp)
 movea.l st_stereo_state,%a0
 movea.l #0x80006308,%a1
 movem.l (%a1),%d0-%d7
 movem.l %d0-%d7,1092(%a0)
 movea.l #0x800062c0,%a1
 movem.l (%a1),%d0-%d7
 movem.l %d0-%d7,1124(%a0)
 movea.l #0x8000e160,%a1
 movem.l (%a1),%d0-%d7
 movem.l %d0-%d7,1156(%a0)
 movea.l #0x8000e110,%a1
 movem.l (%a1),%d0-%d7
 movem.l %d0-%d7,1188(%a0)
 moveq #1,%d0
 moveq #1,%d1
 movem.l %d0-%d1,1220(%a0)
 movem.l %d0-%d1,1228(%a0)
 movem.l (%sp),%d2-%d7
 lea 24(%sp),%sp
 rts
st_stereo_remember_end:

| Cold task-context helpers; excluded from both movable spans.
.globl st_stereo_lock,st_stereo_unlock,st_stereo_sync
st_stereo_lock:
 moveq #0,%d0
 move.w %sr,%d0
 move.w #0x2700,%sr
 rts
st_stereo_unlock:
 move.l 4(%sp),%d0
 move.w %d0,%sr
 rts
st_stereo_sync:
 nop
 move.l #0xa40ce100,%d0
 movec %d0,%cacr
 rts
