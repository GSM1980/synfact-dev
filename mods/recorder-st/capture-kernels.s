| SPDX-License-Identifier: GPL-2.0-or-later
| Integer MAIN return correction. Each product is shifted BEFORE summing,
| exactly as C: -((gD*d)>>1)-((gR*r)>>1). Integer MSAC.L >> preserves odd
| negative products and +32768 rounded operands; no 16-bit narrowing.
| Save physical ACC0/ACCext01 in integer unsaturated mode, restore MACSR
| last, including flags. ACC1..3 and MASK retain their complete state.
        .text
        .balignw 4,0x4e71
        .globl recorder_return_bank
recorder_return_bank:
        lea -40(%sp),%sp
        movem.l %d2-%d7/%a2,(%sp)
        move.l %macsr,%d0
        move.l %d0,28(%sp)
        moveq #0,%d0
        move.l %d0,%macsr
        move.l %acc0,%d0
        move.l %d0,32(%sp)
        move.l %accext01,%d0
        move.l %d0,36(%sp)
        moveq #0,%d0
        move.l %d0,%acc0
        movea.l 44(%sp),%a0
        lea 0x8000dcd0,%a1
        lea 0x8000ddd0,%a2
        move.l 48(%sp),%d2
        move.l 52(%sp),%d3
        move.l 56(%sp),%d4
        move.l 60(%sp),%d5
        moveq #32,%d6
        moveq #15,%d7
        tst.l 64(%sp)
        beq.w recorder_return_pre

        .macro return_side gd,gr,shift,post
        move.l (%a1)+,%d0
        move.l (%a2)+,%d1
        asr.l %d7,%d0
        addq.l #1,%d0
        asr.l #1,%d0
        asr.l %d7,%d1
        addq.l #1,%d1
        asr.l #1,%d1
        msac.l \gd,%d0,>>,%acc0
        msac.l \gr,%d1,>>,%acc0
        movclr.l %acc0,%d0
        asr.l #8,%d0
        asr.l #\shift,%d0
        .if \post
        move.l %d0,%d1
        lsl.l #2,%d0
        add.l %d1,%d0
        .endif
        move.l %d0,(%a0)+
        .endm
recorder_return_post:
        return_side %d2,%d3,5,1
        return_side %d4,%d5,5,1
        subq.l #1,%d6
        bne.w recorder_return_post
        bra.w recorder_return_restore
recorder_return_pre:
        return_side %d2,%d3,3,0
        return_side %d4,%d5,3,0
        subq.l #1,%d6
        bne.w recorder_return_pre
recorder_return_restore:
        moveq #0,%d0
        move.l %d0,%macsr
        move.l 32(%sp),%d0
        move.l %d0,%acc0
        move.l 36(%sp),%d0
        move.l %d0,%accext01
        move.l 28(%sp),%d0
        move.l %d0,%macsr
        movem.l (%sp),%d2-%d7/%a2
        lea 40(%sp),%sp
        rts

| Full 32-frame MAIN LR capture: (PCM*, corrections*, flags, peaks[2]).
| flags bit 0: POST (otherwise PRE), bit 1: known-zero correction bank.
| Register bounds implement exact PCM16 saturation (ColdFire ASL clears V,
| so ASL/SATS cannot implement narrowing saturation on this processor).
| Four frames per iteration; no stack scratch PCM, no per-frame state tests.
        .balignw 4,0x4e71
        .globl recorder_stereo_pcm
recorder_stereo_pcm:
        lea -32(%sp),%sp
        movem.l %d2-%d7/%a2-%a3,(%sp)
        lea 0x80004760,%a0
        movea.l 36(%sp),%a2
        movea.l 40(%sp),%a1
        move.l 44(%sp),%d0
        movea.l 48(%sp),%a3
        moveq #0,%d2
        moveq #0,%d3
        move.l #32767,%d4
        move.l #-32768,%d5
        moveq #16,%d6
        moveq #8,%d7
        btst #1,%d0
        bne.w recorder_pcm_raw
        btst #0,%d0
        beq.w recorder_pcm_pre
        .macro pcm_side off,peak,kind
        .if \kind == 2
        move.w \off(%a0),%d0
        ext.l %d0
        .else
        move.l \off(%a0),%d0
        asr.l %d6,%d0
        .if \kind == 1
        add.l (%a1)+,%d0
        .else
        sub.l (%a1)+,%d0
        .endif
        cmp.l %d4,%d0
        ble.s 4f
        move.l %d4,%d0
        bra.s 5f
4:      cmp.l %d5,%d0
        bge.s 5f
        move.l %d5,%d0
5:
        .endif
        move.w %d0,(%a2)+
        bpl.s 1f
        neg.l %d0
1:      cmp.l \peak,%d0
        bls.s 2f
        move.l %d0,\peak
2:
        .endm
        .macro pcm_run name,kind
\name:
        .irp frame,0,80,160,240
        pcm_side \frame,%d2,\kind
        pcm_side \frame+4,%d3,\kind
        .endr
        lea 320(%a0),%a0
        subq.l #1,%d7
        bne.w \name
        .endm
        pcm_run recorder_pcm_post,1
        bra.w recorder_pcm_done
        pcm_run recorder_pcm_pre,0
        bra.w recorder_pcm_done
        pcm_run recorder_pcm_raw,2
recorder_pcm_done:
        movem.l %d2-%d3,(%a3)
        move.l %d2,%d0
        cmp.l %d3,%d0
        bcc.s 3f
        move.l %d3,%d0
3:      movem.l (%sp),%d2-%d7/%a2-%a3
        lea 32(%sp),%sp
        rts
