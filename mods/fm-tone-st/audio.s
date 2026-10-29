| Synfact FM_CACHE adapter, imported as symbolic assembler source.
| Adaptation: generic elekloader extended-control receiver. See imported provenance.
| FM TONE (runtime-support machine): Digitone 1.44 FM voice, dn-resident adapter semantics.
        .text
.include "imported/bindings.s"
        .globl  fm_update, fm_render
| ---------------------------------------------------------------- update(pitch, voice, params)
fm_update:
        lea     -20(%sp),%sp
        movem.l %d2-%d3/%a2-%a4,(%sp)
        movea.l 28(%sp),%a2                 | voice (scheduler: 80000000 + track*708)
        bsr.w   fm_track
        tst.l   %d0
        bmi.w   fm_update_done
        move.l  %d0,%d2
        move.l  0x6f8(%a2),%d0
        cmpi.l  #0x464d3801,%d0
        beq.s   fm_voice_installed
        lea     fm_templates,%a0
        movea.l 0(%a0,%d2.l*4),%a0
        lea     0x570(%a2),%a1
        moveq   #97,%d0
        bsr.w   fm_copy_long
        move.l  #0x464d3801,%d0
        move.l  %d0,0x6f8(%a2)
fm_voice_installed:
        lea     fm_ready,%a0
        tst.b   0(%a0,%d2.l)
        bne.s   1f
        bsr.w   fm_init_track               | first update of this track since boot
        lea     fm_ready,%a0
        moveq   #1,%d0
        move.b  %d0,0(%a0,%d2.l)
1:      lea     fm_contexts,%a0
        movea.l 0(%a0,%d2.l*4),%a3          | this track's saved FM context
        lea     fm_params,%a0
        movea.l 0(%a0,%d2.l*4),%a4          | this track's Digitone parameter block
        bsr.w   fm_parameters_changed
        move.l  %d0,-(%sp)
        bsr.w   fm_snapshot_receive
        move.l  (%sp)+,%d1
        or.l    %d1,%d0
        move.l  %d0,-(%sp)
        bsr.w   fm_offsets_receive
        move.l  (%sp)+,%d1
        or.l    %d1,%d0
        tst.l   %d0
        beq.w   fm_parameters_ready
| transport: received SYN1 words (params = working+1c: slot s at +2*s) -> Digitone +34..+42
        movea.l 32(%sp),%a0
        move.l  0x24(%a0),0x34(%a4)         | ALGO, RATIO C   <- slots 12, 13
        move.w  0x28(%a0),0x38(%a4)         | RATIO A         <- slot 14
        move.l  0x2c(%a0),0x3c(%a4)         | HARM, DTUN      <- slots 16, 17
        move.w  0x34(%a0),0x40(%a4)         | FDBK            <- slot 1a (knob 7)
        move.w  0x32(%a0),0x42(%a4)         | MIX             <- slot 19 (knob 8)
        moveq   #0,%d3
        move.w  0x2a(%a0),%d3               | RATIO B, one stored control <- slot 15
        divu.w  #88,%d3                     | index = stored / FM_RATIO_STEP (ratio.h)
        mvz.w   %d3,%d3
        cmpi.l  #360,%d3
        bls.s   14f
        move.l  #360,%d3
14:     add.l   %d3,%d3                     | Digitone raw = 2*index = 2*(19*B1 + B2)
        clr.w   0x34(%a0)                   | slot-1a working word consumed (dn-resident)
12:     lea     fm_ext,%a0
        move.l  %d2,%d0
        lsl.l   #5,%d0
        adda.l  %d0,%a0                     | this track's values of parameters 9..24
        move.l  (%a0),0x48(%a4)             | ATKA DECA       (Digitone slots 1b 1c)
        move.l  4(%a0),0x4c(%a4)            | ENDA LEVA       (1d 1e)
        move.l  8(%a0),0x50(%a4)            | ATKB DECB       (1f 20)
        move.l  12(%a0),0x54(%a4)           | ENDB LEVB       (21 22)
        move.l  16(%a0),0x58(%a4)           | ADEL ATRG       (23 24)
        move.w  20(%a0),0x5c(%a4)           | ARST            (25)
        move.w  22(%a0),%d1                 | PHRT (19); bit 0 is MAIN's ECO toggle
        andi.l  #0xfffe,%d1
        move.w  %d1,0x44(%a4)
        move.l  24(%a0),0x5e(%a4)           | BDEL BTRG       (26 27)
        move.w  28(%a0),0x62(%a4)           | BRST            (28)
        move.w  %d3,0x3a(%a4)               | RATIO B (word 30, parameter 23, is RATIO OFFSET C)
| sanitizer: clamp every transported word to its Digitone 1.44 range (table: offset, min, max)
        lea     fm_bounds2,%a1
        moveq   #22,%d3
2:      moveq   #0,%d1
        move.w  (%a1)+,%d1
        lea     0(%a4,%d1.l),%a0
        moveq   #0,%d0
        move.w  (%a0),%d0
        moveq   #0,%d1
        move.w  (%a1)+,%d1
        cmp.l   %d1,%d0
        bcc.s   3f
        move.l  %d1,%d0
3:      move.w  (%a1)+,%d1
        cmp.l   %d1,%d0
        bls.s   4f
        move.l  %d1,%d0
4:      move.w  %d0,(%a0)
        subq.l  #1,%d3
        bpl.s   2b
fm_parameters_ready:
        lea     FM_OFFSETS,%a0
        move.l  %d2,%d0
        lsl.l   #3,%d0
        adda.l  %d0,%a0
        move.l  (%a0),0x76(%a4)
        move.l  4(%a0),0x7a(%a4)
        movea.l 32(%sp),%a0
        clr.w 0x34(%a0)
| tempo-dependent envelope divisor from the received MAIN frame (+0x184), stock default if <= 0
        move.l  FM_CLOCK_SOURCE,%d0
        bgt.s   5f
        move.l  #FM_DEFAULT_CLOCK,%d0
5:      move.l  %d0,WS_CLOCK
| Native per-voice state; no context or parameter swaps.
        move.l  0x38(%a2),%d0
        move.l  %d0,0x178(%a3)
        tst.l   %d0
        beq.s   fm_eco_keep
        lea     FM_ECO,%a0
        moveq   #0,%d1
        move.b  0(%a0,%d2.l),%d1
        move.b  8(%a0,%d2.l),%d0
        move.b  %d1,8(%a0,%d2.l)
        tst.l   %d1
        beq.s   fm_eco_keep
        tst.b   %d0
        bne.s   fm_eco_keep
        movea.l #FM_DECIM_STATE,%a0
        moveq   #48,%d0
        muls.l  %d2,%d0
        adda.l  %d0,%a0
        moveq   #11,%d1
fm_eco_clear:
        clr.l   (%a0)+
        subq.l  #1,%d1
        bpl.s   fm_eco_clear
fm_eco_keep:
        moveq   #0,%d0
        move.w  0x40(%a4),%d0
        swap    %d0                         | FDBK Q8 -> Q24
        move.l  %d0,0x16c(%a3)
        pea     WS_PARAMS
        move.l  24+4(%sp),-(%sp)            | pitch (Q16 MIDI note)
        move.l  %d2,-(%sp)
        jsr     DSP_PITCH
        lea     12(%sp),%sp
        pea     WS_PARAMS
        move.l  0x3c(%a2),-(%sp)            | release
        move.l  0x38(%a2),-(%sp)            | trigger
        move.l  %d2,-(%sp)
        jsr     DSP_ENV
        lea     16(%sp),%sp
        pea     WS_PARAMS
        move.l  0x38(%a2),-(%sp)
        move.l  %d2,-(%sp)
        jsr     DSP_TRIG
        lea     12(%sp),%sp
fm_update_done:
        movem.l (%sp),%d2-%d3/%a2-%a4
        lea     20(%sp),%sp
        rts
| ---------------------------------------------------------------- render(out, voice)
fm_render:
        lea     -20(%sp),%sp
        movem.l %d2-%d3/%a2-%a4,(%sp)
        movea.l 28(%sp),%a2
        bsr.w   fm_track
        tst.l   %d0
        bmi.s   fm_render_silence
        lea     fm_ready,%a0
        tst.b   0(%a0,%d0.l)
        beq.s   fm_render_silence           | never updated: no workspace state of ours
        move.l  %d0,%d2
        lea     fm_contexts,%a0
        movea.l 0(%a0,%d2.l*4),%a3
        lea     FM_ECO,%a0
        move.b  8(%a0,%d2.l),%d0
        move.b  %d0,16(%a0)
        lea     fm_params,%a0
        movea.l 0(%a0,%d2.l*4),%a4
        pea     WS_PARAMS
        move.l  %d2,-(%sp)
        jsr     DSP_OSC
        addq.l  #8,%sp
        moveq   #0,%d0
        move.w  0x42(%a4),%d0
        swap    %d0                         | MIX Q8 -> Q24
        move.l  %d0,-(%sp)
        move.l  %d2,-(%sp)
        jsr     DSP_MIX
        addq.l  #8,%sp
        move.l  %d2,-(%sp)
        jsr     DSP_OUT
        addq.l  #4,%sp
        lea     WS_OUTPUT,%a0
        move.l  %d2,%d0
        lsl.l   #7,%d0
        adda.l  %d0,%a0
        movea.l 24(%sp),%a1
        move.l  %a1,%d0
        move.l  %a0,%d1
        cmp.l   %d1,%d0
        beq.s   fm_render_done              | output already in the scheduler slot
        moveq   #31,%d0
        bsr.w   fm_copy_long
        bra.s   fm_render_done
fm_render_silence:
        movea.l 24(%sp),%a1
        moveq   #31,%d0
6:      clr.l   (%a1)+
        subq.l  #1,%d0
        bpl.s   6b
fm_render_done:
        movem.l (%sp),%d2-%d3/%a2-%a4
        lea     20(%sp),%sp
        rts
| ---------------------------------------------------------------- per-track initialisation
| d2 = track, a2 = voice. dn-resident reset (CompactResetGates.fm_reset + parameter_initializer)
| minus the stock voice reset, which the scheduler already ran.
fm_init_track:
        lea     fm_ready,%a0
        tst.l   (%a0)
        bne.s   fm_scratch_ready
        tst.l   4(%a0)
        bne.s   fm_scratch_ready
        lea     WS_SCRATCH,%a0
        move.l  #701,%d0
fm_scratch_clear:
        clr.l   (%a0)+
        subq.l  #1,%d0
        bpl.s   fm_scratch_clear
        nop                                 | keeps fm_contexts long-aligned after the glue code
fm_scratch_ready:
        lea     fm_ext,%a1                  | parameters 9..24 start at their defaults
        move.l  %d2,%d0
        lsl.l   #5,%d0
        adda.l  %d0,%a1
        lea     fm_ext_defaults,%a0
        moveq   #15,%d0
        bsr.w   fm_copy_word
        lea     fm_params,%a0
        movea.l 0(%a0,%d2.l*4),%a1
        lea     fm_template,%a0
        moveq   #78,%d0
        bsr.w   fm_copy_word
        lea     fm_params,%a0
        movea.l 0(%a0,%d2.l*4),%a1
| Native fixed state was initialized by the builder at this exact
| relocation. Runtime only initializes the selected track's transport defaults.
        rts
| ---------------------------------------------------------------- helpers
| a2 = voice -> d0 = track 0..7, or -1 (d1/a0 clobbered)
fm_track:
        movea.l #0x80000000,%a0
        moveq   #0,%d0
8:      cmpa.l  %a0,%a2
        beq.s   9f
        adda.l  #0x708,%a0
        addq.l  #1,%d0
        moveq   #8,%d1
        cmp.l   %d1,%d0
        blt.s   8b
        moveq   #-1,%d0
9:      rts
| Cache raw transport words, never sanitized words (invalid inputs
| must compare equal on subsequent blocks too). Each track owns 20 bytes.
fm_parameters_changed:
        move.l  %d2,%d0
        lsl.l   #4,%d0
        move.l  %d2,%d1
        lsl.l   #2,%d1
        add.l   %d1,%d0
        lea     FM_CACHE,%a1
        adda.l  %d0,%a1
        moveq   #0,%d3
        tst.w   18(%a1)
        bne.s   fm_cache_valid
        moveq   #1,%d3
        move.w  %d3,18(%a1)
fm_cache_valid:
        movea.l 36(%sp),%a0       | caller's params, including BSR return
        lea     0x24(%a0),%a0
        moveq   #8,%d1
fm_cache_loop:
        move.w  (%a0)+,%d0
        cmp.w   (%a1),%d0
        beq.s   fm_cache_same
        moveq   #1,%d3
fm_cache_same:
        move.w  %d0,(%a1)+
        subq.l  #1,%d1
        bpl.s   fm_cache_loop
fm_cache_done:
        move.l  %d3,%d0
        rts

fm_offsets_receive:
        moveq #0,%d0
        rts
fm_snapshot_receive:
        | update's 20-byte save, one cached flag, and this BSR's return.
        move.l 40(%sp),-(%sp)
        move.l %d2,-(%sp)
        jsr fm_receive_controls
        addq.l #8,%sp
        rts
fm_copy_long:
        move.l  (%a0)+,(%a1)+
        subq.l  #1,%d0
        bpl.s   fm_copy_long
        rts
fm_copy_word:
        move.w  (%a0)+,(%a1)+
        subq.l  #1,%d0
        bpl.s   fm_copy_word
        rts
| ---------------------------------------------------------------- read-only data

| ---------------------------------------------------------------- T5 HARM bake
.globl fm_bake_mod
fm_bake_mod:
        lea     FAST_40001298,%a0           | modulated one-table kernel
        lea     FAST_40001410,%a1           | modulated morph kernel (fallback)
        bra.s   fm_bake_common
.globl fm_bake_car
fm_bake_car:
        lea     FAST_400017FE,%a0           | carrier one-table kernel
        lea     FAST_40001950,%a1           | carrier morph kernel (fallback)
fm_bake_common:                             | 4(sp) track, 8(sp) operator parameters
        lea     -32(%sp),%sp
        movem.l %d2-%d5/%a2-%a5,(%sp)
        movea.l %a0,%a4
        movea.l %a1,%a5
        move.l  36(%sp),%d2
        movea.l 40(%sp),%a2
        move.l  (%a2),%d3                   | operator index
        moveq   #3,%d0
        cmp.l   %d0,%d3
        bhi.w   fm_bake_fallback
        moveq   #7,%d0
        cmp.l   %d0,%d2
        bhi.w   fm_bake_fallback
        | Feedback can amplify table-bake rounding on every algorithm.
        | Keep the original morph kernel whenever FDBK is nonzero; bake
        | remains available for steady HARM without feedback.
        lea     fm_params,%a0
        movea.l 0(%a0,%d2.l*4),%a0
        move.w  0x40(%a0),%d0               | FDBK
        bne.w   fm_bake_fallback
        move.l  #FM_VOICE_STRIDE,%d0
        muls.l  %d2,%d0
        moveq   #0x58,%d1
        muls.l  %d3,%d1
        add.l   %d1,%d0
        movea.l #FM_VOICE_SRAM,%a3
        adda.l  %d0,%a3                     | operator state
        move.l  0x48(%a3),%d0
        cmp.l   0x50(%a3),%d0
        bne.w   fm_bake_fallback
        move.l  0x4c(%a3),%d0
        cmp.l   0x54(%a3),%d0
        bne.w   fm_bake_fallback
        move.l  0x40(%a3),%d0
        cmp.l   0x44(%a3),%d0
        bne.w   fm_bake_fallback
        move.l  %d2,%d4
        lsl.l   #2,%d4
        add.l   %d3,%d4                     | key 0..31 = track*4 + operator
        movea.l #FM_BAKE_MAP,%a0
        moveq   #0,%d5
        move.b  0(%a0,%d4.l),%d5
        bne.s   fm_bake_slot
        movea.l #FM_BAKE_NEXT,%a1
        move.l  (%a1),%d5
        moveq   #BAKE_SLOTS,%d1
        cmp.l   %d1,%d5
        bcc.w   fm_bake_fallback            | all slots taken: keep the morph kernel
        addq.l  #1,%d5
        move.l  %d5,(%a1)
        move.b  %d5,0(%a0,%d4.l)
fm_bake_slot:
        subq.l  #1,%d5
        move.l  #BAKE_RECORD,%d1
        muls.l  %d1,%d5
        movea.l #FM_BAKE_SLOTS,%a1
        adda.l  %d5,%a1                     | record: T1, T2, c, B[257]
        move.l  0x48(%a3),%d0
        cmp.l   (%a1),%d0
        bne.s   fm_bake_build
        move.l  0x4c(%a3),%d0
        cmp.l   4(%a1),%d0
        bne.s   fm_bake_build
        move.l  0x40(%a3),%d0
        cmp.l   8(%a1),%d0
        beq.s   fm_bake_run
fm_bake_build:
        move.l  0x48(%a3),%d0
        move.l  %d0,(%a1)
        movea.l %d0,%a0                     | T1
        move.l  0x4c(%a3),%d0
        move.l  %d0,4(%a1)
        movea.l %d0,%a2                     | T2
        move.l  0x40(%a3),%d3               | w2 = c
        move.l  %d3,8(%a1)
        move.l  #0x7f000000,%d2
        sub.l   %d3,%d2                     | w1
        lea     12(%a1),%a5
        | FM_CACHE: MAC with load, two points per iteration (CFPRM EMAC). The same
        | acc0 operations in the same order: B[] and MACSR are bit-identical,
        | and every register the code below reads ends as before.
        moveq   #127,%d4                    | 128 x 2 points + 1 (BAKE_POINTS = 257)
        movclr.l %acc0,%d0
        move.l  (%a0)+,%d0                  | T1[0]
        move.l  (%a2)+,%d1                  | T2[0]
fm_bake_loop:
        mac.l   %d2,%d0,(%a0)+,%d0,%acc0    | w1*T1[i]; d0 = T1[i+1]
        mac.l   %d3,%d1,(%a2)+,%d1,%acc0    | w2*T2[i]; d1 = T2[i+1]
        movclr.l %acc0,%d5
        move.l  %d5,(%a5)+
        mac.l   %d2,%d0,(%a0)+,%d0,%acc0
        mac.l   %d3,%d1,(%a2)+,%d1,%acc0
        movclr.l %acc0,%d5
        move.l  %d5,(%a5)+
        subq.l  #1,%d4
        bpl.s   fm_bake_loop
        mac.l   %d2,%d0,%acc0               | point 256, already loaded
        mac.l   %d3,%d1,%acc0
        movclr.l %acc0,%d0
        move.l  %d0,(%a5)+
fm_bake_run:
        lea     12(%a1),%a0
        move.l  0x48(%a3),%d5               | T1, restored after the call
        move.l  %a0,0x48(%a3)
        move.l  40(%sp),-(%sp)
        move.l  40(%sp),-(%sp)
        jsr     (%a4)
        addq.l  #8,%sp
        move.l  %d5,0x48(%a3)
        movem.l (%sp),%d2-%d5/%a2-%a5
        lea     32(%sp),%sp
        rts
fm_bake_fallback:
        movea.l %a5,%a0
        movem.l (%sp),%d2-%d5/%a2-%a5
        lea     32(%sp),%sp
        jmp     (%a0)
        .balignw 4,0x4e71
.section .rodata,"a"
.balign 4
.globl fm_contexts
fm_contexts:
        .long   FM_CONTEXT0, FM_CONTEXT1, FM_CONTEXT2, FM_CONTEXT3, FM_CONTEXT4, FM_CONTEXT5, FM_CONTEXT6, FM_CONTEXT7
.globl fm_templates
fm_templates:
        .long   FM_VOICE_TEMPLATE0, FM_VOICE_TEMPLATE1, FM_VOICE_TEMPLATE2, FM_VOICE_TEMPLATE3, FM_VOICE_TEMPLATE4, FM_VOICE_TEMPLATE5, FM_VOICE_TEMPLATE6, FM_VOICE_TEMPLATE7
.globl fm_params
fm_params:
        .long   FM_PARAMS0, FM_PARAMS1, FM_PARAMS2, FM_PARAMS3, FM_PARAMS4, FM_PARAMS5, FM_PARAMS6, FM_PARAMS7
.globl fm_bounds
fm_bounds:
        .short  0x0, 0x700, 0x0, 0x1200, 0x0, 0x2300, 0x0, 0x2d0, 0x2500, 0x5900, 0x0, 0x7f00, 0x0, 0x7f00, 0x0, 0x7f00
.globl fm_template
fm_template:
        .short  0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x0, 0x300, 0x300, 0x0, 0x3f00, 0x0, 0x4000, 0x0, 0x100, 0x0, 0x0, 0x6000, 0x7f00, 0x4000, 0x0, 0x0, 0x7f00, 0x4000, 0x4000, 0x0, 0x0, 0x7f00, 0x0, 0x0, 0x0, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000, 0x4000
.globl fm_bounds2
fm_bounds2:
        .short  0x34, 0x0, 0x700, 0x36, 0x0, 0x1200, 0x38, 0x0, 0x2300, 0x3a, 0x0, 0x2d0, 0x3c, 0x2500, 0x5900, 0x3e, 0x0, 0x7f00, 0x40, 0x0, 0x7f00, 0x42, 0x0, 0x7f00, 0x48, 0x0, 0x7f00, 0x4a, 0x0, 0x7f00, 0x4c, 0x0, 0x7f00, 0x4e, 0x0, 0x7f00, 0x50, 0x0, 0x7f00, 0x52, 0x0, 0x7f00, 0x54, 0x0, 0x7f00, 0x56, 0x0, 0x7f00, 0x58, 0x0, 0x7f00, 0x5a, 0x0, 0x100, 0x5c, 0x0, 0x100, 0x44, 0x0, 0x400, 0x5e, 0x0, 0x7f00, 0x60, 0x0, 0x100, 0x62, 0x0, 0x100
.globl fm_ext_defaults
fm_ext_defaults:
        .short  0x0, 0x2000, 0x7f00, 0x0, 0x0, 0x2000, 0x7f00, 0x0, 0x0, 0x100, 0x100, 0x100, 0x0, 0x100, 0x100, 0x300
        .balign 16
fm_glue_end:
