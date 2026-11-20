| Adapted from Synthack-OS master-comp, MIT. See upstream/LICENSE.
| Origin c12cf23ad88e68d3f85422a7d09c2a911659997f. Changes: shared FX-CV
| context, Synfact settings, six controls, linear dry/wet gain, no sidechain.
        .include "dsp-constants.inc"
        .equ EXP_K,-356689313
        .equ CV_MAX,65535
        .equ METER_DECAY,111848
        .text
        .globl comp_audio,comp_process,comp_kernel
comp_audio:
comp_process:
comp_kernel:
        | Disabled steady state uses caller-saved registers only: no frame,
        | no spill traffic and no MAC state access. Kit changes take setup.
        movea.l comp_hot,%a0
        move.l  0x800030b8,%d0
        cmp.l   40(%a0),%d0
        bne.s   .Lsetup
        tst.l   24(%a0)
        bne.s   .Lsetup
        clr.l   44(%a0)
        clr.l   48(%a0)
        move.l  52(%a0),%d0
        beq.s   .Loff_return
        subi.l  #METER_DECAY,%d0
        bpl.s   .Loff_store
        moveq   #0,%d0
.Loff_store:
        move.l  %d0,52(%a0)
.Loff_return:
        rts
.Lsetup:
        lea     -28(%sp),%sp
        movem.l %d2-%d4/%d7/%a2-%a3,(%sp)
        movea.l 32(%sp),%a3
        movea.l %a0,%a2
        cmp.l   40(%a2),%d0
        beq.s   .Lready
        move.l  %d0,40(%a2)
        move.l  %d0,-(%sp)
        jsr     comp_current_config
        addq.l  #4,%sp
        movea.l %d0,%a0
        movem.l (%a0),%d0-%d4/%d7
        movem.l %d0-%d4/%d7,(%a2)
        movem.l 24(%a0),%d0-%d3
        movem.l %d0-%d3,24(%a2)
.Lready:
        tst.l   24(%a2)
        bne.s   .Lactive
        clr.l   44(%a2)
        clr.l   48(%a2)
        tst.l   52(%a2)
        beq.w   .Lreturn
        bsr.w   comp_meter
        bra.w   .Lreturn
.Lactive:
        movea.l comp_peak_entry,%a1
        jsr     (%a1)
        move.l 44(%a2),%d1
        or.l    48(%a2),%d1
        bne.s   .Lcompress
        cmp.l   32(%a2),%d0
        bhi.s   .Lcompress
        bsr.w   comp_meter
        move.l  36(%a2),%d1
        bra.w   comp_apply
.Lcompress:
        move.l  %macsr,%d1
        move.l  %d1,24(%sp)
        move.l  #0xa0,%macsr
        movea.l comp_curve_entry,%a1
        jsr     (%a1)
1:      | ---- tenuta con rilascio e attacco su 32 campioni a ingresso costante, d = min(st - gr, 0):
        |      st = gr + r^32 * d, at = gr + a^32 * (at - gr) + K * d
        move.l  44(%a2),%d2
        sub.l   %d0,%d2
        ble.s   2f
        moveq   #0,%d2
2:      move.l  48(%a2),%d7
        sub.l   %d0,%d7
        move.l  12(%a2),%d3                     | a^32
        mac.l   %d3,%d7,%acc0
        move.l  20(%a2),%d3                     | K
        mac.l   %d3,%d2,%acc0
        movclr.l %acc0,%d7
        add.l   %d0,%d7                         | at
        move.l  16(%a2),%d3                     | r^32
        mac.l   %d3,%d2,%acc0
        movclr.l %acc0,%d2
        add.l   %d0,%d2                         | st
        move.l  %d2,44(%a2)
        move.l  %d7,48(%a2)
        bsr.w   comp_meter
        | ---- guadagno G (Q12, 65536 = x16) = 2^(16 - v/2^23), v = EXP_K * (MK + at)
        add.l   8(%a2),%d7
        move.l  #EXP_K,%d3
        mac.l   %d3,%d7,%acc0
        movclr.l %acc0,%d0
        move.l  %d0,%d4
        moveq   #23,%d1
        asr.l   %d1,%d4                         | ottave intere
        move.l  %d0,%d2
        moveq   #19,%d1
        lsr.l   %d1,%d2
        moveq   #15,%d1
        and.l   %d1,%d2                         | i
        and.l   #0x0007FFFF,%d0
        moveq   #12,%d1
        lsl.l   %d1,%d0                         | frazione (Q31)
        lea     comp_k_expt,%a1
        move.l  4(%a1,%d2.l*4),%d3
        move.l  (%a1,%d2.l*4),%d1
        sub.l   %d1,%d3
        mac.l   %d3,%d0,%acc0
        movclr.l %acc0,%d3
        add.l   %d3,%d1
        asr.l   %d4,%d1                         | G
        move.l  24(%a2),%d2
        mulu.w  %d2,%d1
        moveq   #15,%d2
        lsr.l   %d2,%d1
        add.l   28(%a2),%d1
        move.l  24(%sp),%d0
        move.l  %d0,%macsr
comp_apply:
        movea.l comp_scale_entry,%a1
        move.l  4(%a3),%d0
        jsr     (%a1)
        move.l  %d0,4(%a3)
        move.l  8(%a3),%d0
        jsr     (%a1)
        move.l  %d0,8(%a3)
.Lreturn:
        movem.l (%sp),%d2-%d4/%d7/%a2-%a3
        lea     28(%sp),%sp
        rts

        .balignw 4,0x4e71
        .globl comp_detector_start,comp_peak,comp_curve,comp_scale,comp_detector_end
comp_detector_start:
comp_peak:
8:      | Exact block peak: MOVE supplies the sign flag, NOT gives x^(x>>31).
        | The two ADC channels are one contiguous span of 64 Q31 words.
        lea     0x80003c50,%a0
        moveq   #0,%d3
        moveq   #7,%d7
.Lpeak:
        .rept 8
        move.l  (%a0)+,%d0
        bpl.s   1f
        not.l   %d0
1:      cmp.l   %d0,%d3
        bge.s   2f
        move.l  %d0,%d3
2:
        .endr
        subq.l  #1,%d7
        bge.s   .Lpeak
        move.l  %d3,%d0
6:      cmpi.l  #256,%d0                        | pavimento come la DT
        bge.s   7f
        move.l  #256,%d0
7:      rts
comp_curve:
        lea     comp_k_logt,%a1
        | ---- log: L = T[i] + interp - lz*C_OCT
        move.l  %d0,%d4
        ff1     %d4                             | lz
        move.l  %d4,%d2
        subq.l  #1,%d2
        asl.l   %d2,%d0                         | m in [2^30, 2^31)
        move.l  %d0,%d2
        moveq   #26,%d1
        lsr.l   %d1,%d2
        moveq   #15,%d1
        and.l   %d1,%d2                         | i
        move.l  4(%a1,%d2.l*4),%d3
        move.l  (%a1,%d2.l*4),%d1
        sub.l   %d1,%d3
        and.l   #0x03FFFFFF,%d0
        lsl.l   #5,%d0                          | frazione (Q31)
        mac.l   %d3,%d0,%acc0
        movclr.l %acc0,%d3
        add.l   %d3,%d1
        move.l  #C_OCT,%d0
        muls.l  %d0,%d4                         | lz * C_OCT
        sub.l   %d4,%d1                         | L
        | ---- curva: gr = 0 sotto soglia, altrimenti thr + slope*(L-thr) - L
        moveq   #0,%d0
        move.l  %d1,%d2
        sub.l   0(%a2),%d2
        blt.s   1f
        move.l  4(%a2),%d3
        mac.l   %d3,%d2,%acc0
        movclr.l %acc0,%d0
        add.l   0(%a2),%d0
        sub.l   %d1,%d0
1:      rts
comp_scale:  cmpi.l  #CV_MAX,%d0
        ble.s   6f
        move.l  #CV_MAX,%d0
6:      tst.l   %d0
        blt.s   7f
        mulu.w  %d1,%d0
        moveq   #12,%d2
        lsr.l   %d2,%d0
        cmpi.l  #CV_MAX,%d0
        bls.s   7f
        move.l  #CV_MAX,%d0
7:      rts
        .balignw 4,0x4e71
comp_detector_end:

| Smoothed GR only. UI tick quantizes/invalidate a visible COMP view.
| No division, pixel conversion or global display work in the audio IRQ.
comp_meter:
        move.l  48(%a2),%d0
        neg.l   %d0
        bpl.s   1f
        moveq   #0,%d0
1:      move.l  52(%a2),%d1
        subi.l  #METER_DECAY,%d1
        bpl.s   2f
        moveq   #0,%d1
2:      cmp.l   %d1,%d0
        bge.s   3f
        move.l  %d1,%d0
3:      move.l  %d0,52(%a2)
        rts




        .data
        .balign 16
        .globl comp_ddr_runtime,comp_gr_meter
comp_ddr_runtime:
        .long 0,0,0,0,0,0,0,0,0,0 | prepared config
        .long -1,0,0              | Kit, hold, attack
comp_gr_meter: .long 0
comp_k_logt:
        .long LOG_T0,LOG_T1,LOG_T2,LOG_T3,LOG_T4,LOG_T5,LOG_T6,LOG_T7,LOG_T8,LOG_T9,LOG_T10,LOG_T11,LOG_T12,LOG_T13,LOG_T14,LOG_T15,LOG_T16
        .globl comp_k_expt
comp_k_expt:
        .long EXP_T0,EXP_T1,EXP_T2,EXP_T3,EXP_T4,EXP_T5,EXP_T6,EXP_T7,EXP_T8,EXP_T9,EXP_T10,EXP_T11,EXP_T12,EXP_T13,EXP_T14,EXP_T15,EXP_T16
