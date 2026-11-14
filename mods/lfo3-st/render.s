| Fused 13-track LFO3 render. Imported from Synfact src/features/lfo3/throughput/render.s
| (sha256 65110d2a...), derived from the native OS 1.41 LFO kernel. Position independent:
| it runs from a MAIN SRAM copy; native helpers/tables are absolute. Same EMAC contract.
.text
.globl lfo3_render,lfo3_step_body
lfo3_render:
 lea -96(%sp),%sp
 movem.l %d2-%d7/%a2-%a6,(%sp)
 movea.l 100(%sp),%a5
 lea 28(%a5),%a5
 movea.l 112(%sp),%a3
 addq.l #6,%a3
 movea.l 116(%sp),%a2
 lea 36(%a2),%a2
 move.l 104(%sp),%d0
 move.l %d0,68(%sp)
 | D6 is otherwise unused by this kernel and is callee-saved by helpers.
 | A sentinel above the thirteen trigger bits also terminates the loop.
 move.l 108(%sp),%d6
 andi.l #0x1fff,%d6
 ori.l #0x2000,%d6
 move.l 120(%sp),%d0
 move.l %d0,76(%sp)
lfo3_step_body:
 | Accepted idle policy: suspend the complete oscillator when it cannot
 | affect a real destination. Read the effective bank after p-lock overlay.
 | Native destination slots 1..70 are real; zero/invalid slots are silent.
 mvs.w 8(%a3),%d0
 addi.l #-16384,%d0
 beq.w step_return
 mvz.b (%a3),%d1
 subq.l #1,%d1
 cmpi.l #69,%d1
 bhi.w step_return
 | Keep scaled DEP in a callee-saved register until destination application.
 | State waveform is already addressable as -32(A2); no second state pointer
 | or per-engine A4 increment, and no second parameter load/subtract/double.
 add.l %d0,%d0
 movea.l %d0,%a4
lfo3_full_step:
        mvs.w -6(%a3),%d5
        mvs.b -4(%a3),%d0
        addi.l #-16384,%d5
        add.l %d5,%d5
        tst.l %d0
        bge.s mult_nonnegative
        clr.l %d0
        bra.s tempo_external
mult_nonnegative:
        moveq #23,%d1
        cmp.l %d0,%d1
        bge.s mult_in_range
        moveq #23,%d0
        bra.s tempo_fixed
mult_in_range:
        moveq #11,%d2
        cmp.l %d0,%d2
        bge.s tempo_external
tempo_fixed:
        addi.l #-12,%d0
        move.l #0x3840,%d1
        bra.s tempo_ready
tempo_external:
        move.l 68(%sp),%d1
tempo_ready:
        mvs.b 2(%a3),%d3
        bge.s wave_nonnegative
        clr.l %d3
        bra.s wave_ready
wave_nonnegative:
        moveq #6,%d7
        cmp.l %d3,%d7
        bge.s wave_ready
        moveq #6,%d3
wave_ready:
        muls.l %d1,%d5
        moveq #11,%d7
        mvs.w 4(%a3),%d2
        sub.l %d0,%d7
        mvs.w -2(%a3),%d0
        move.l %d2,44(%sp)
        asr.l %d7,%d5
        move.l %d0,-(%sp)
        jsr 0x40118c74
        addq.l #4,%sp
        mvs.b 6(%a3),%d4
        move.l %d5,%d2
        movea.l -36(%a2),%a0
        add.l %a0,%d2
        movea.l %d2,%a1
        adda.l #0xd6cd2000,%a0
        adda.l #0xd6cd2000,%a1
        move.l %a1,%d7
        move.l %a0,%d1
        eor.l %d1,%d7
        add.l -20(%a2),%d0
        tst.l %d7
        bge.s wrap_phase
        tst.b 1(%a2)
        bne.s wrap_phase
        tst.b (%a2)
        bne.s wrap_phase
        moveq #4,%d7
        cmp.l %d4,%d7
        bne.s wrap_phase
        lea 0x402457e4,%a0
        movea.l (%a0,%d3.l*4),%a0
        move.l %a0,-32(%a2)
        move.b #1,(%a2)
wrap_phase:
        cmpi.l #0x5265bfff,%d2
        bls.s mode_flags
        tst.b 1(%a2)
        bne.s wrap_now
        tst.b (%a2)
        bne.s wrap_now
        moveq #3,%d1
        cmp.l %d4,%d1
        bne.s wrap_now
        lea 0x4024581c,%a1
        tst.l %d5
        blt.s wrap_latch
        lea 0x40245800,%a1
wrap_latch:
        movea.l (%a1,%d3.l*4),%a0
        move.l %a0,-32(%a2)
        move.b #1,(%a2)
wrap_now:
        tst.l %d5
        blt.s wrap_backward
        addi.l #0xad9a4000,%d2
        bra.s mode_flags
wrap_backward:
        addi.l #0x5265c000,%d2
mode_flags:
        moveq #1,%d1
        cmp.l %d4,%d1
        bcs.s mode_started
        clr.b (%a2)
mode_started:
        clr.b 1(%a2)
        btst #0,%d6
        beq.s no_reset
        moveq #1,%d1
        cmp.l %d4,%d1
        beq.s reset_phase
        move.l %d4,%d1
        subq.l #3,%d1
        mvz.w %d1,%d1
        moveq #1,%d7
        cmp.l %d1,%d7
        bcs.s trig_other_mode
reset_phase:
        moveq #6,%d0
        cmp.l %d3,%d0
        beq.s reset_random_phase
        move.l 44(%sp),%d2
        move.l #0x5265c000,%d0
        swap %d2
        clr.w %d2
        .word 0xa400,0x0800,0xa1c2
        bra.s reset_fade
reset_random_phase:
        clr.l %d2
reset_fade:
        moveq #1,%d7
        mvs.w -2(%a3),%d1
        move.l %d1,-(%sp)
        jsr 0x40118cf0
        clr.b (%a2)
        addq.l #4,%sp
        move.b %d7,1(%a2)
trig_other_mode:
        tst.l %d4
        bne.s trig_mode_two
        clr.b (%a2)
        mvs.w -2(%a3),%d0
        move.l %d0,-(%sp)
        jsr 0x40118cf0
        addq.l #4,%sp
        bra.s no_reset
trig_mode_two:
        moveq #2,%d1
        cmp.l %d4,%d1
        bne.s no_reset
        move.b #1,(%a2)
        move.b #1,%d1
        bra.s save_phase
no_reset:
        clr.l %d1
save_phase:
        move.l %d2,-36(%a2)
        movea.l %d0,%a6
        tst.l %d0
        bge.s fade_upper
        suba.l %a6,%a6
        bra.s fade_ready
fade_upper:
        cmpi.l #0x3fffffff,%d0
        ble.s fade_ready
        movea.l #0x3fffffff,%a6
fade_ready:
        move.l %a6,-20(%a2)
        move.l -32(%a2),%d0
        tst.b (%a2)
        beq.s calculate_wave
        tst.l %d1
        beq.s held_wave
calculate_wave:
        moveq #6,%d7
        cmp.l %d3,%d7
        beq.s random_wave
        move.l #0x636ba875,%d0
        .word 0xa400,0x0800,0xa1c2
        lsl.l #2,%d2
        lea 0x40245838,%a1
        movea.l (%a1,%d3.l*4),%a1
        move.l %d2,-(%sp)
        move.l %d1,%d4
        jsr (%a1)
        addq.l #4,%sp
        move.l %d4,%d1
        bra.w wave_ready_output
random_wave:
        tst.l %d5
        blt.s random_backward
        move.l %d2,%d0
        sub.l -16(%a2),%d0
        bra.s random_threshold
random_backward:
        move.l -16(%a2),%d0
        sub.l %d2,%d0
random_threshold:
        cmpi.l #0x05265bff,%d0
        bhi.s random_next
        tst.l %d4
        beq.s random_slew
        btst #0,%d6
        beq.s random_slew
random_next:
        move.l %d2,-16(%a2)
        move.l %d1,48(%sp)
        movea.l 76(%sp),%a0
        bsr.w lfo3_random
        move.l %d0,-28(%a2)
        move.l 48(%sp),%d1
        bra.s random_slew
held_wave:
        moveq #6,%d2
        cmp.l %d3,%d2
        bne.s wave_ready_output
random_slew:
        move.l 44(%sp),%d7
        asr.l #8,%d7
        lea 0x40245850,%a0
        move.l -28(%a2),%d2
        move.l #0x7fffffff,%d0
        move.l -24(%a2),%d3
        movea.l (%a0,%d7.l*4),%a1
        sub.l %a1,%d0
        .word 0xa002,0x0800,0xa243,0x0800,0xa1c0
        move.l %d0,-32(%a2)
wave_ready_output:
        move.l %d0,-24(%a2)
        tst.b (%a2)
        beq.s apply_fade
        tst.l %d1
        beq.s destination
apply_fade:
        .word 0xa00e,0x0800,0xa1c0
        add.l %d0,%d0
        move.l %d0,-32(%a2)
destination:
        mvs.b (%a3),%d1
        moveq #70,%d7
        mvs.w %d1,%d2
        move.l %d2,-12(%a2)
        cmp.l %d2,%d7
        bcs.s phase_copy
        move.l %a4,%d2
        lea (%a5,%d1.l*2),%a0
        .word 0xa002,0x0800,0xa1c0
        move.l %d0,-8(%a2)
        mvs.w (%a0),%d2
        add.l %d2,%d0
        bpl.s clamp_max
        clr.l %d0
        bra.s write_value
clamp_max:
        cmpi.l #0x7f00,%d0
        ble.s write_value
        move.l #0x7f00,%d0
write_value:
        move.w %d0,(%a0)
phase_copy:
        tst.b (%a2)
        beq.s running_phase
        clr.l -4(%a2)
        bra.s step_return
running_phase:
        move.l -36(%a2),-4(%a2)
step_return:
 | The final pointer advances touch no memory and all address registers are
 | restored below. One backward branch replaces a forward test plus branch.
 lea 142(%a5),%a5
 lea 16(%a3),%a3
 lea 40(%a2),%a2
 lsr.l #1,%d6
 cmpi.l #1,%d6
 bne.w lfo3_step_body
render_return:
 movem.l (%sp),%d2-%d7/%a2-%a6
 lea 96(%sp),%sp
 rts
lfo3_random:
 move.l 4(%a0),%d1
 move.l %d1,%d0
 add.l (%a0),%d0
 move.l %d1,(%a0)
 move.l %d0,4(%a0)
 rts

.globl lfo3_render_end
lfo3_render_end:
