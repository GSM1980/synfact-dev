| SPDX-License-Identifier: GPL-2.0-or-later
| Zero-ramp fast paths for the stock MAIN mix kernels (OS 1.42).
|
| The mixer calls three kernels once or more per audio block, each as
| f(out, in, gains, increments): for 32 frames it accumulates N tracks'
| samples (track-major, 128 bytes a track) times N gains with MSAC into
| ACC0, writes the frame to out (stride 8: one channel of a stereo pair),
| and adds each gain's per-frame increment, then stores the gains back.
| When every increment is zero (gains not ramping, the usual case) the
| additions and the store-back change nothing. These entries test that
| first; if so they run the same MSAC sequence, in the same order and with
| the same operands, without them, two frames per loop. Otherwise they
| replay the displaced stock instructions and continue in the stock kernel.
| Results, ACC0, MACSR and the caller-visible registers are those of the
| stock kernel (tools/stock-profile: XDIFF exact differential).
        .text
        .globl mixfast_k10,mixfast_k9,mixfast_k8

| 4008ef8c: ten tracks. Stock: A3 out, A4 in, A5 gains, A6 increments.
mixfast_k10:
        movea.l 16(%sp),%a0
        move.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        bne.w 9f
        lea -48(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        movem.l 52(%sp),%a3-%a5
        movem.l (%a5),%d0-%d5/%a0-%a2/%a5
        movea.w #16,%a6
        move.l (%a4)+,%d6
1:      msacl %d0,%d6,%a4@(124),%d6,%acc0
        msacl %d1,%d6,%a4@(252),%d6,%acc0
        msacl %d2,%d6,%a4@(380),%d6,%acc0
        msacl %d3,%d6,%a4@(508),%d6,%acc0
        msacl %d4,%d6,%a4@(636),%d6,%acc0
        msacl %d5,%d6,%a4@(764),%d6,%acc0
        msacl %a0,%d6,%a4@(892),%d6,%acc0
        msacl %a1,%d6,%a4@(1020),%d6,%acc0
        msacl %a2,%d6,%a4@(1148),%d6,%acc0
        msacl %a5,%d6,%a4@+,%d6,%acc0
        movclrl %acc0,%d7
        move.l %d7,(%a3)
        msacl %d0,%d6,%a4@(124),%d6,%acc0
        msacl %d1,%d6,%a4@(252),%d6,%acc0
        msacl %d2,%d6,%a4@(380),%d6,%acc0
        msacl %d3,%d6,%a4@(508),%d6,%acc0
        msacl %d4,%d6,%a4@(636),%d6,%acc0
        msacl %d5,%d6,%a4@(764),%d6,%acc0
        msacl %a0,%d6,%a4@(892),%d6,%acc0
        msacl %a1,%d6,%a4@(1020),%d6,%acc0
        msacl %a2,%d6,%a4@(1148),%d6,%acc0
        msacl %a5,%d6,%a4@+,%d6,%acc0
        movclrl %acc0,%d7
        move.l %d7,8(%a3)
        lea 16(%a3),%a3
        subq.l #1,%a6
        tst.l %a6
        bgt.w 1b
        movem.l (%sp),%d2-%d7/%a2-%a6
        lea 48(%sp),%sp
        rts
9:      lea -48(%sp),%sp
        moveq #32,%d0
        jmp 0x4008ef92

| 4008f024: nine tracks. Stock: A4 out, A5 in, A0 gains, A6 increments.
mixfast_k9:
        movea.l 16(%sp),%a0
        move.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        bne.w 9f
        lea -44(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        movea.l 48(%sp),%a4
        moveq #16,%d5
        movea.l 52(%sp),%a5
        movea.l 56(%sp),%a0
        movem.l (%a0),%d0-%d4/%a0-%a3
        move.l (%a5)+,%d6
1:      msacl %d0,%d6,%a5@(124),%d6,%acc0
        msacl %d1,%d6,%a5@(252),%d6,%acc0
        msacl %d2,%d6,%a5@(380),%d6,%acc0
        msacl %d3,%d6,%a5@(508),%d6,%acc0
        msacl %d4,%d6,%a5@(636),%d6,%acc0
        msacl %a0,%d6,%a5@(764),%d6,%acc0
        msacl %a1,%d6,%a5@(892),%d6,%acc0
        msacl %a2,%d6,%a5@(1020),%d6,%acc0
        msacl %a3,%d6,%a5@+,%d6,%acc0
        movclrl %acc0,%d7
        move.l %d7,(%a4)
        msacl %d0,%d6,%a5@(124),%d6,%acc0
        msacl %d1,%d6,%a5@(252),%d6,%acc0
        msacl %d2,%d6,%a5@(380),%d6,%acc0
        msacl %d3,%d6,%a5@(508),%d6,%acc0
        msacl %d4,%d6,%a5@(636),%d6,%acc0
        msacl %a0,%d6,%a5@(764),%d6,%acc0
        msacl %a1,%d6,%a5@(892),%d6,%acc0
        msacl %a2,%d6,%a5@(1020),%d6,%acc0
        msacl %a3,%d6,%a5@+,%d6,%acc0
        movclrl %acc0,%d7
        move.l %d7,8(%a4)
        lea 16(%a4),%a4
        subq.l #1,%d5
        bgt.s 1b
        movem.l (%sp),%d2-%d7/%a2-%a6
        lea 44(%sp),%sp
        rts
9:      lea -44(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        jmp 0x4008f02c

| 4008f0b6: eight tracks. Stock: A3 out, A4 in, A0 gains, A5 increments.
| One frame per iteration, exactly the stock loop minus the increment adds
| and the gain store-back (both no-ops when the increments are zero).
mixfast_k8:
        movea.l 16(%sp),%a0
        move.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        or.l (%a0)+,%d0
        bne.w 9f
        lea -40(%sp),%sp
        movem.l %d2-%d7/%a2-%a5,(%sp)
        movea.l 44(%sp),%a3
        moveq #32,%d5
        movea.l 48(%sp),%a4
        movea.l 52(%sp),%a0
        movem.l (%a0),%d0-%d4/%a0-%a2
        move.l (%a4)+,%d6
1:      msacl %d0,%d6,%a4@(124),%d6,%acc0
        msacl %d1,%d6,%a4@(252),%d6,%acc0
        msacl %d2,%d6,%a4@(380),%d6,%acc0
        msacl %d3,%d6,%a4@(508),%d6,%acc0
        msacl %d4,%d6,%a4@(636),%d6,%acc0
        msacl %a0,%d6,%a4@(764),%d6,%acc0
        msacl %a1,%d6,%a4@(892),%d6,%acc0
        msacl %a2,%d6,%a4@+,%d6,%acc0
        movclrl %acc0,%d7
        move.l %d7,%a3@+
        addq.l #4,%a3
        subq.l #1,%d5
        bgt.s 1b
        movem.l (%sp),%d2-%d7/%a2-%a5
        lea 40(%sp),%sp
        rts
9:      lea -40(%sp),%sp
        movem.l %d2-%d7/%a2-%a5,(%sp)
        jmp 0x4008f0be

| IRQ exclusion and one code-halfword write with the core's cache sequence
| (as modulation-skip/playhead). The caller writes the three patch words.
        .globl mixfast_lock,mixfast_unlock,mixfast_code16,mixfast_sync
mixfast_lock:
        moveq #0,%d0
        move.w %sr,%d0
        move.w #0x2700,%sr
        rts
mixfast_unlock:
        move.l 4(%sp),%d0
        move.w %d0,%sr
        rts
mixfast_code16:
        movea.l 4(%sp),%a0
        move.l 8(%sp),%d0
        move.w %d0,(%a0)
        move.l %a0,%d0
        andi.l #-16,%d0
        movea.l %d0,%a0
        cpushl %dc,(%a0)
        rts
mixfast_sync:
        nop
        move.l #0xa40ce100,%d0          | native MAIN CACR + instruction invalidate
        movec %d0,%cacr
        rts
