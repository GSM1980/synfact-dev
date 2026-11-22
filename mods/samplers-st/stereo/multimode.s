| SPDX-License-Identifier: GPL-2.0-or-later
| The native multimode's second part (OS 1.42 AUDIO 40001ea4..4000231e) on a
| stereo track's L and R at once (samplers-st 1.6, stereo/audio.c).
|
| The native multimode (400019d6) is one routine in two parts: the first
| computes the block's targets from the row (80008900.., and the increments
| at 80008720 the second part rewrites), then falls into 40001ea4, where every
| path of the first part joins, with its frame on the stack (48 the track,
| 52 its type before this block, 68 the buffer, the arguments from 104).
| The engine runs a copy of the first part (made on the unit from its own
| AUDIO image, stereo/audio.c) that ends here instead.
|
| This is the native second part, instruction for instruction, with R added:
| R's three loops use ACC2 and ACC3 where L uses ACC0 and ACC1, with the
| same coefficients, weights and ramps on every frame and the same EMAC
| operations, in the same order on each accumulator. The control words
| (coefficients, increments, weights, flags, type) are computed once, as the
| native computes them for L; R's memory is its own (ST_STEREO_SIGNAL: word
| t for the second loop, words 24 + 2t and 25 + 2t for the first). So L, the
| control words and L's memory are what the native routine leaves, and R and
| its memory are what the native routine would make of R from the same
| starting words (tests/test_st_stereo_natives_emu.py, check_chain).
|
| R works in AUDIO SRAM the stereo engine owns, at fixed distances from L's
| two scratch buffers (A1 = 80008960, B1 = 800089e0), so that one address
| register serves both channels:
|   R_OUT1 8000fe80 = A1 + RA   the first loop's first output
|   R_COPY 8000ff00 = A1 + RB = B1 + RA   R's buffer, in and out
|   R_OUT2 8000ff80 = B1 + RB   the first loop's second output, then the second loop's
        .set RA,0x7520
        .set RB,0x75a0
        .set R_COPY,0x8000ff00
        .set R_BASE,0x8000f400          | ST_STEREO_R(0), 128 bytes a track
        .set R_LOOP1,0x8000f860         | ST_STEREO_SIGNAL + 96: 8 bytes a track
        .set R_LOOP2_FROM_L,0x7280      | ST_STEREO_SIGNAL (8000f800) - 80008580
        .text
        .even
        .globl st_mm_fused_tail
st_mm_fused_tail:
| R's buffer into R_COPY; R's accumulators cleared (the stock chain leaves
| every accumulator cleared; ACC0 and ACC1 are left as the native finds them)
        move.l 48(%sp),%d0
        lsl.l #7,%d0
        movea.l %d0,%a0
        adda.l #R_BASE,%a0
        lea R_COPY,%a1
        moveq #4,%d1
1:      movem.l (%a0),%d2-%d7/%a2-%a3
        movem.l %d2-%d7/%a2-%a3,(%a1)
        lea 32(%a0),%a0
        lea 32(%a1),%a1
        subq.l #1,%d1
        bne.s 1b
        movclr.l %acc2,%d0
        movclr.l %acc3,%d0
| 40001ea4: the reset flags
        move.l 48(%sp),%d7
        lea 0x80008550,%a0
        move.b (%a0,%d7.l),%d7
        move.b %d7,67(%sp)
        beq.s 2f
        tst.l 124(%sp)
        sne %d0
        mvs.b %d0,%d0
        neg.l %d0
        bra.s 3f
2:      clr.l %d0
3:      move.l 48(%sp),%d7
        lsl.l #5,%d7
        move.l %d7,60(%sp)
        move.l #0x80008900,%d7
        btst #0,%d0
        bne.s 4f
        lea 0x80008558,%a0
        movea.l 48(%sp),%a1
        tst.b (%a0,%a1.l)
        beq.s 6f
| 40001eec: reset: coefficients <- targets, increments 0, the first loop's
| memory 0 (L's and R's)
4:      movea.l 60(%sp),%a1
        clr.l %d0
        adda.l #0x80008740,%a1
        lea 0x80008720,%a0
5:      movea.l %d7,%a2
        adda.l %d0,%a2
        move.l (%a2),(%a1,%d0.l)
        clr.l (%a0,%d0.l)
        moveq #32,%d1
        addq.l #4,%d0
        cmp.l %d0,%d1
        bne.s 5b
        move.l 48(%sp),%d0
        clr.b %d2
        lea 0x80008920,%a0
        lsl.l #3,%d0
        move.l 48(%sp),%d5
        clr.l (%a0,%d0.l)
        movea.l %d0,%a0
        adda.l #0x80008924,%a0
        clr.l (%a0)
        lea R_LOOP1,%a0
        clr.l (%a0,%d0.l)
        clr.l 4(%a0,%d0.l)
        lea 0x80008558,%a0
        move.b %d2,(%a0,%d5.l)
        bra.s 8f
| 40001f3e: increments = (target - coefficient) / 32
6:      movea.l 60(%sp),%a1
        clr.l %d0
        adda.l #0x80008740,%a1
        lea 0x80008720,%a0
7:      movea.l %d7,%a2
        move.l (%a2,%d0.l),%d1
        sub.l (%a1,%d0.l),%d1
        sats.l %d1
        tst.l %d1
        bge.s 70f
        addi.l #31,%d1
70:     asr.l #5,%d1
        move.l %d1,(%a0,%d0.l)
        moveq #32,%d1
        addq.l #4,%d0
        cmp.l %d0,%d1
        bne.s 7b
| 40001f74: the first loop
8:      movea.l 68(%sp),%a0
        movea.l #0x80008740,%a1
        move.l 116(%sp),%d0
        moveq #32,%d1
        muls.l %d1,%d0
        adda.l %d0,%a1
        movea.l (%a1)+,%a2
        movea.l (%a1)+,%a3
        movea.l (%a1)+,%a4
        movea.l (%a1)+,%a5
        move.l (%a1)+,%d4
        move.l (%a1),%d5
        movea.l #0x80008920,%a1
        move.l 116(%sp),%d0
        asl.l #3,%d0
        adda.l %d0,%a1
        move.l (%a1),%d2
        move.l 4(%a1),%d3
        lea R_LOOP1,%a1
        move.l (%a1,%d0.l),%d7          | R: its d2
        move.l 4(%a1,%d0.l),%d0         | R: its d3
        movea.l #0x80008960,%a1
        movea.l #0x800089e0,%fp
        move.l (%a0)+,%d1
        moveq #32,%d6
        move.l %d6,80(%sp)
9:      mac.l %a2,%d1,%acc0
        mac.l %a2,%d1,%acc0
        mac.l %a2,%d1,%acc0
        mac.l %a2,%d1,%acc0
        msac.l %a5,%d1,%acc1
        msac.l %a5,%d1,%acc1
        msac.l %a5,%d1,%acc1
        msac.l %a5,%d1,RB(%a1),%d1,%acc1        | R's input sample
        mac.l %a2,%d1,%acc2
        mac.l %a2,%d1,%acc2
        mac.l %a2,%d1,%acc2
        mac.l %a2,%d1,%acc2
        msac.l %a5,%d1,%acc3
        msac.l %a5,%d1,%acc3
        msac.l %a5,%d1,%acc3
        msac.l %a5,%d1,(%a0)+,%d1,%acc3         | L's next input sample
        adda.l 0x80008720,%a2
        adda.l 0x8000872c,%a5
        mac.l %a3,%d2,%acc0
        mac.l %a3,%d7,%acc2
        adda.l 0x80008724,%a3
        msac.l %a4,%d3,%acc0
        msac.l %a4,%d0,%acc2
        adda.l 0x80008728,%a4
        mac.l %d4,%d2,%acc1
        mac.l %d4,%d7,%acc3
        add.l 0x80008730,%d4
        mac.l %d5,%d3,%acc1
        mac.l %d5,%d0,%acc3
        add.l 0x80008734,%d5
        movclr.l %acc2,%d7
        move.l %d7,%d6
        asr.l #1,%d6
        move.l %d6,RA(%a1)
        movclr.l %acc0,%d2
        move.l %d2,%d6
        asr.l #1,%d6
        move.l %d6,(%a1)+
        movclr.l %acc3,%d0
        move.l %d0,RB(%fp)
        movclr.l %acc1,%d3
        move.l %d3,(%fp)+
        subq.l #1,80(%sp)
        bne.w 9b
| 4000201e: the first loop's memory; coefficients 0..5 <- targets
        movea.l #0x80008920,%a1
        move.l 116(%sp),%d6
        asl.l #3,%d6
        adda.l %d6,%a1
        move.l %d2,(%a1)
        move.l %d3,4(%a1)
        lea R_LOOP1,%a1
        move.l %d7,(%a1,%d6.l)
        move.l %d0,4(%a1,%d6.l)
        move.l #0x80008900,%d7
        clr.l %d0
        movea.l 60(%sp),%a0
        adda.l #0x80008740,%a0
10:     movea.l %d7,%a1
        adda.l %d0,%a1
        move.l (%a1),(%a0,%d0.l)
        moveq #24,%d1
        addq.l #4,%d0
        cmp.l %d0,%d1
        bne.s 10b
| 4000204e: the second loop's target weight
        moveq #1,%d2
        cmp.l 52(%sp),%d2
        bcc.s 11f
        moveq #7,%d5
        cmp.l 52(%sp),%d5
        bne.s 12f
11:     move.l 0x4000a388,%d7
        move.l %d7,56(%sp)
        bra.s 13f
12:     movea.l 104(%sp),%a0
        lea 0x4000a288,%a1
        mvs.b 4(%a0),%d0
        move.l (%a1,%d0.l*4),%d0
        move.l %d0,56(%sp)
13:     move.l 48(%sp),%d5
        lea 0x800088e0,%a5
        move.l (%a5,%d5.l*4),%d5
        move.l 56(%sp),%d7
        movea.l 68(%sp),%a0
        movea.l #0x800089e0,%a1
        movea.l #0x80008740,%a2
        move.l 116(%sp),%d0
        asl.l #5,%d0
        adda.l %d0,%a2
        movea.l 24(%a2),%a4
        movea.l 28(%a2),%a3
        movea.l #0x80008580,%a2
        asr.l #3,%d0
        adda.l %d0,%a2
        move.l (%a2),%d4
        move.l R_LOOP2_FROM_L(%a2),%d3          | R: its d4
        sub.l %d5,%d7
        asr.l #5,%d7
        moveq #32,%d6
        move.l %d6,80(%sp)
| 400020c4: the second loop
14:     mac.l %a3,%d4,(%a0)+,%d0,%acc0          | L: input
        mac.l %a3,%d3,RA(%a1),%d6,%acc2         | R: input
        mac.l %a4,%d0,(%a1),%d4,%acc0
        mac.l %a4,%d6,RB(%a1),%d3,%acc2
        adda.l 0x80008738,%a4
        adda.l 0x8000873c,%a3
        asr.l #1,%d0
        asr.l #7,%d0
        movclr.l %acc0,%d1
        move.l %d0,%d2
        sub.l %d1,%d2
        mac.l %d5,%d2,%acc0
        add.l %d0,%d1
        asr.l #2,%d1
        asr.l #1,%d6
        asr.l #7,%d6
        movclr.l %acc2,%d0
        move.l %d6,%d2
        sub.l %d0,%d2
        mac.l %d5,%d2,%acc2
        add.l %d6,%d0
        asr.l #2,%d0
        add.l %d7,%d5
        sats.l %d5
        movclr.l %acc0,%d2
        add.l %d1,%d2
        asl.l #6,%d2
        add.l %d2,%d2
        sats.l %d2
        movclr.l %acc2,%d6
        add.l %d0,%d6
        asl.l #6,%d6
        add.l %d6,%d6
        sats.l %d6
        move.l %d6,RB(%a1)
        move.l %d2,(%a1)+
        subq.l #1,80(%sp)
        bne.w 14b
| 400020fe: memory; coefficients 6, 7 <- targets; the type and the weights
        move.l %d4,(%a2)
        move.l %d3,R_LOOP2_FROM_L(%a2)
        lea 0x80008918,%a1
        movea.l 60(%sp),%a0
        adda.l #0x80008740,%a0
        lea 0x8000891c,%a2
        move.l (%a1),24(%a0)
        move.l (%a2),28(%a0)
        movea.l 104(%sp),%a0
        lea 0x4000a288,%a1
        mvs.b 4(%a0),%d0
        move.l 48(%sp),%d7
        lea (%a1,%d0.l*4),%a1
        move.l (%a1),(%a5,%d7.l*4)
        mvs.b (%a0),%d0
        bge.s 15f
        clr.l %d0
        bra.s 16f
15:     moveq #7,%d1
        cmp.l %d0,%d1
        bge.s 16f
        moveq #7,%d0
16:     cmp.l 52(%sp),%d0
        beq.s 18f
        move.l 52(%sp),%d1
        moveq #4,%d2
        subq.l #2,%d1
        cmp.l %d1,%d2
        bcs.s 17f
        move.l %d0,%d1
        subq.l #2,%d1
        cmp.l %d1,%d2
        bcc.s 18f
17:     moveq #1,%d5
        move.l 48(%sp),%d7
        lea 0x80008558,%a0
        clr.b %d1
        move.b %d5,(%a0,%d7.l)
        bra.s 19f
18:     moveq #1,%d1
19:     tst.b 67(%sp)
        beq.s 20f
        clr.b %d1
        move.l 48(%sp),%d7
        lea 0x80008558,%a0
        move.b %d1,(%a0,%d7.l)
        lea 0x80008550,%a0
        move.b %d1,(%a0,%d7.l)
        moveq #1,%d1
20:     move.l 120(%sp),%d5
        move.l 48(%sp),%d7
        btst %d7,%d5
        beq.s 21f
        clr.b %d1
        moveq #1,%d2
        lea 0x80008558,%a0
        move.b %d1,(%a0,%d7.l)
        lea 0x80008550,%a0
        move.b %d2,(%a0,%d7.l)
        moveq #1,%d1
21:     move.l 48(%sp),%d7
        lea 0x80008560,%a1
        move.l %d0,(%a1,%d7.l*4)
        tst.b %d1
        beq.s 25f
        moveq #7,%d0
        cmp.l 52(%sp),%d0
        beq.s 26f
        tst.l 52(%sp)
        beq.s 28f
        moveq #1,%d1
        cmp.l 52(%sp),%d1
        bne.s 29f
        move.l #0x80000000,%d7
        clr.l 52(%sp)
        move.l %d7,56(%sp)
        bra.s 27f
25:     clr.l 52(%sp)
        bra.s 30f
26:     clr.l 52(%sp)
        clr.l 56(%sp)
27:     movea.l #0x20000000,%a5
        bra.s 32f
28:     move.l #0x7fffffff,%d7
        clr.l 52(%sp)
        move.l %d7,56(%sp)
        bra.s 31f
29:     move.l #0x7fffffff,%d7
        move.l %d7,52(%sp)
30:     clr.l 56(%sp)
31:     suba.l %a5,%a5
| 4000222e: the third loop's weight increments
32:     lea 0x80008840,%a0
        move.l 48(%sp),%d0
        move.l 48(%sp),%d7
        lsl.l #2,%d0
        lsl.l #4,%d7
        sub.l %d0,%d7
        move.l %a5,%d0
        sub.l (%a0,%d7.l),%d0
        sats.l %d0
        tst.l %d0
        bge.s 33f
        addi.l #31,%d0
33:     asr.l #5,%d0
        movea.l %d7,%a0
        adda.l #0x80008844,%a0
        move.l %d0,88(%sp)
        move.l 56(%sp),%d0
        sub.l (%a0),%d0
        sats.l %d0
        tst.l %d0
        bge.s 34f
        addi.l #31,%d0
34:     asr.l #5,%d0
        movea.l %d7,%a0
        adda.l #0x80008848,%a0
        move.l %d0,92(%sp)
        move.l 52(%sp),%d0
        sub.l (%a0),%d0
        sats.l %d0
        tst.l %d0
        bge.s 35f
        addi.l #31,%d0
35:     asr.l #5,%d0
        movea.l %d7,%a0
        adda.l #0x80008840,%a0
        lea 88(%sp),%a1
        move.l %a0,80(%sp)
        move.l %d0,96(%sp)
        move.l %a1,84(%sp)
        movea.l 80(%sp),%a0
        movea.l (%a0)+,%a3
        movea.l (%a0)+,%a4
        move.l (%a0)+,%d2
        movea.l 84(%sp),%a0
        move.l (%a0)+,%d3
        move.l (%a0)+,%d4
        move.l (%a0)+,%d5
        movea.l 68(%sp),%a0
        movea.l #0x80008960,%a1
        movea.l #0x800089e0,%a2
        move.l (%a0),%d0
| 400022d6: the third loop (d7, 12t, is kept for after it)
        move.l %d7,84(%sp)
        move.l RB(%a1),%d7                      | R: its first input sample
        moveq #32,%d6
        move.l %d6,80(%sp)
36:     mac.l %a3,%d0,RA(%a1),%d6,%acc0         | L: dry; R's first output
        mac.l %a3,%d7,(%a1)+,%d1,%acc2          | R: dry; L's first output
        mac.l %a4,%d1,(%a2),%d1,%acc0           | L: the second loop's output
        mac.l %a4,%d6,RB(%a2),%d6,%acc2         | R: the second loop's output
        mac.l %d2,%d1,4(%a0),%d0,%acc0          | L: its next dry sample
        mac.l %d2,%d6,RB(%a1),%d7,%acc2         | R: its next dry sample
        addq.l #4,%a2
        adda.l %d3,%a3
        adda.l %d4,%a4
        add.l %d5,%d2
        movclr.l %acc0,%d1
        move.l %d1,(%a0)+
        movclr.l %acc2,%d6
        move.l %d6,RB-4(%a1)
        subq.l #1,80(%sp)
        bne.s 36b
        move.l 84(%sp),%d7
| 400022f2: the weights reach their targets
        lea 0x80008840,%a2
        move.l %a5,(%a2,%d7.l)
        movea.l %d7,%a0
        adda.l %a2,%a0
        move.l 56(%sp),%d7
        move.l %d7,4(%a0)
        move.l 52(%sp),%d7
        move.l %d7,8(%a0)
| The frame as the native leaves it (its words 80 and 84 held the loops'
| count and 12t here): 80 the weights' address, 84 the increments'.
        move.l %a0,80(%sp)
        lea 88(%sp),%a1
        move.l %a1,84(%sp)
| R's output back to its buffer
        move.l 48(%sp),%d0
        lsl.l #7,%d0
        movea.l %d0,%a1
        adda.l #R_BASE,%a1
        lea R_COPY,%a0
        moveq #4,%d1
37:     movem.l (%a0),%d2-%d7/%a2-%a3
        movem.l %d2-%d7/%a2-%a3,(%a1)
        lea 32(%a0),%a0
        lea 32(%a1),%a1
        subq.l #1,%d1
        bne.s 37b
        move.l #32,%macsr
        movem.l (%sp),%d2-%d7/%a2-%a6
        lea 100(%sp),%sp
        rts
