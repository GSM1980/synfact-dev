| SPDX-License-Identifier: GPL-2.0-or-later
| Sequencer step direction per track, OS 1.42. Written in the core's shape
| (its sites, one table, its own files) so that it can move into the core.
|
| The sequencer interrupt (4008c924) keeps each track's step counting
| forwards as stock: step[t] (43bd73fc + t) is the step it plays next and
| last[t] (43bd7416 + t) the one before; they go 0, 1 ... length - 1, 0, and
| every start, pattern change and resync sets them from the pattern's
| position (docs/MACHINE-REFERENCE.md, section 9). A track in another mode
| plays the step its mode makes of that count (seq-direction.c): backwards
| the mirror, length - 1 - step, so it starts on its last step and stays in
| phase with the pattern; pendulum, random and walk a step they choose once
| per count. The step is changed where it is used, not where it is kept:
|
|   4008cf00, 4008cf14  the step pushed for the trig preparation (4008b882,
|                       4008bcec) at a step's tick 0: d6 = t, d0 = the
|                       pattern, a6 = &step[t]
|   4008c204            the step pushed for 4008b882 when a pattern starts
|                       mid-way: d0 = step, d2 = t, the pattern at 43bd7430
|   4008d1b8            each tick's copy of last[t] and step[t] for the UI
|                       (444e2370 + t, 444e237d + t): the screen, the LEDs and
|                       live recording (4008da6e reads 444e2370). d0 = t,
|                       d4 = the pattern, a6 = &last[t], a5 = &step[t]
|   4008b746            the accessor 4008b734(t) for the step a track plays,
|                       which the trig LEDs' playhead reads (UI task)
|
| Each site is a `jmp abs.l` over whole stock instructions; its stub does
| what they did, with the track's step, and jumps back. They are in only
| while a track is not forwards; with every track forwards the stock bytes are
| there and nothing is added.
|
| The length is the one the stock step advance uses (4008d100): the track's
| (its record, pattern + 499h * t, + 969, a word) when the pattern's byte
| + 21379h is set, else the pattern's (+ 21374h). A count at or past the
| length (a length made shorter while playing; 255, no step yet) stays as it is.
.text
.balign 4
| d1 = the stock count (0..255), d7 = track, a0 = pattern -> d1 = the step
| track d7 plays (playhead_step in seq-direction.c). playhead_mirror may choose
| a new step (sequencer IRQ); playhead_peek only reads one already chosen.
| Every other register kept. playhead_step finds the track's mode in its
| pattern's kit (a forwards track returns at once).
playhead_peek:
        lea -16(%sp),%sp
        movem.l %d0/%a0-%a1,(%sp)
        moveq #0,%d0
        bra.s playhead_fast
playhead_mirror:
        lea -16(%sp),%sp
        movem.l %d0/%a0-%a1,(%sp)
        moveq #1,%d0
playhead_fast:
        move.l %d0,12(%sp)             | compute; unused fourth save slot
        cmpi.l #13,%d7
        bcc.w playhead_slow
        move.l %d7,%d0
        lsl.l #4,%d0                   | 16-byte state, no multiply/divide
        lea playhead_tracks,%a1
        adda.l %d0,%a1
        tst.b 5(%a1)                   | published cache
        beq.w playhead_slow
        cmpa.l 0x43bd8430,%a0
        beq.s playhead_current
        cmpa.l 0x43bd8438,%a0
        bne.w playhead_slow
        move.l 0x43bd8444,%d0
        bra.s playhead_cached_kit
playhead_current:
        move.l 0x43bd843c,%d0
playhead_cached_kit:
        cmp.l 12(%a1),%d0
        bne.w playhead_slow
        mvz.b 6(%a1),%d0
        lsl.l #4,%d0
        add.l %d7,%d0
        lea playhead_modes,%a0
        mvz.b (%a0,%d0.l),%d0
        movea.l 4(%sp),%a0
        cmp.b 7(%a1),%d0               | also safe for a raw mode-table edit
        bne.w playhead_slow
        tst.l %d0
        beq.w playhead_fast_done        | FORWARD: no length or step work
        adda.l #0x20000,%a0
        tst.b 0x1379(%a0)
        beq.s playhead_pattern_length
        movea.l 4(%sp),%a0
        move.l #0x499,%d0
        mulu.l %d7,%d0
        adda.l %d0,%a0
        mvz.w 969(%a0),%d0
        bra.s playhead_cached_length
playhead_pattern_length:
        mvz.w 0x1374(%a0),%d0
playhead_cached_length:
        cmp.b 8(%a1),%d0
        bne.s playhead_slow
        cmp.l %d0,%d1
        bcc.s playhead_fast_done        | outside the track: native count
        btst #1,5(%a1)                 | valid 2: stateless reverse
        beq.s playhead_cached_step
        subq.l #1,%d0                  | REVERSE, entirely in registers
        sub.l %d1,%d0
        move.l %d0,%d1
        bra.s playhead_fast_done
playhead_cached_step:
        cmp.b (%a1),%d1
        beq.s playhead_same_step
        cmp.b 2(%a1),%d1
        bne.s playhead_new_count
        tst.l 12(%sp)
        beq.s playhead_previous_step
        cmp.b 10(%a1),%d1              | two-step next/previous ambiguity
        beq.s playhead_advance_count
playhead_previous_step:
        mvz.b 3(%a1),%d1
        bra.s playhead_fast_done
playhead_same_step:
        mvz.b 1(%a1),%d1
        bra.s playhead_fast_done
playhead_new_count:
        tst.l 12(%sp)
        beq.s playhead_same_step        | a peek never chooses a new step
playhead_advance_count:
        move.l %d1,-(%sp)
        move.l %a1,-(%sp)
        jsr playhead_advance
        addq.l #8,%sp
        move.l %d0,%d1
playhead_fast_done:
        movem.l (%sp),%d0/%a0-%a1
        lea 16(%sp),%sp
        rts
playhead_slow:
        movea.l 4(%sp),%a0
        move.l 12(%sp),-(%sp)          | compute
        move.l %a0,-(%sp)               | the pattern
        move.l %d1,-(%sp)               | the count
        move.l %d7,-(%sp)               | the track
        jsr playhead_step
        lea 16(%sp),%sp
        move.l %d0,%d1
        movem.l (%sp),%d0/%a0-%a1
        lea 16(%sp),%sp
        rts

| 4008cf00 and 4008cf14: move.l d5,-(sp) / clr.l -(sp) / mvz.b (a6),d1 /
| move.l d1,-(sp); then the stock goes on with the other three arguments.
.globl playhead_trig_a,playhead_trig_b
playhead_trig_a:
        lea -8(%sp),%sp
        movem.l %d7/%a0,(%sp)
        move.l %d6,%d7
        movea.l %d0,%a0
        moveq #0,%d1
        move.b (%a6),%d1
        bsr.w playhead_mirror
        movem.l (%sp),%d7/%a0
        lea 8(%sp),%sp
        move.l %d5,-(%sp)
        clr.l -(%sp)
        move.l %d1,-(%sp)
        jmp 0x4008cf80
playhead_trig_b:
        lea -8(%sp),%sp
        movem.l %d7/%a0,(%sp)
        move.l %d6,%d7
        movea.l %d0,%a0
        moveq #0,%d1
        move.b (%a6),%d1
        bsr.w playhead_mirror
        movem.l (%sp),%d7/%a0
        lea 8(%sp),%sp
        move.l %d5,-(%sp)
        clr.l -(%sp)
        move.l %d1,-(%sp)
        jmp 0x4008cf94

| 4008c204: move.l d0,-(sp) / move.l a0,-(sp) / move.l 43bd7430,-(sp).
| d0 (the step, sign-extended) is pushed as it is unless the track's mode
| changes it; the call's result replaces d0 afterwards.
.globl playhead_trig_c
playhead_trig_c:
        lea -12(%sp),%sp
        movem.l %d1/%d7/%a0,(%sp)
        moveq #0,%d1
        move.b %d0,%d1
        move.l %d2,%d7
        movea.l 0x43bd8430,%a0
        bsr.w playhead_mirror
        moveq #0,%d7
        move.b %d0,%d7                  | the step as it came
        cmp.l %d1,%d7                   | mirrored? (else d0 stays sign-extended)
        beq.s 1f
        move.l %d1,%d0
1:      movem.l (%sp),%d1/%d7/%a0
        lea 12(%sp),%sp
        move.l %d0,-(%sp)
        move.l %a0,-(%sp)
        move.l 0x43bd8430,-(%sp)
        jmp 0x4008c286

| 4008d1b8: move.b (a6)+,(a1,d0.l) / movea.l d7,a1 / addq.l #1,d0 /
| move.b (a5)+,(a4)+   (a1 = 444e2370 on entry)
.globl playhead_ui_copy
playhead_ui_copy:
        lea -16(%sp),%sp
        movem.l %d1/%d7/%a0/%a1,(%sp)
        | Most ticks do not advance a step. Validate its metadata once and
        | copy both cached indices, instead of resolving the same kit twice.
        cmp.l 0x43bd8430,%d4
        bne.w playhead_ui_slow
        move.l %d0,%d7
        lsl.l #4,%d7
        lea playhead_tracks,%a1
        adda.l %d7,%a1
        tst.b 5(%a1)
        beq.w playhead_ui_slow
        move.l 12(%a1),%d1
        cmp.l 0x43bd843c,%d1
        bne.w playhead_ui_slow
        mvz.b 6(%a1),%d7
        lsl.l #4,%d7
        add.l %d0,%d7
        lea playhead_modes,%a0
        mvz.b (%a0,%d7.l),%d1
        cmp.b 7(%a1),%d1
        bne.w playhead_ui_slow
        tst.l %d1
        beq.w playhead_ui_forward
        movea.l %d4,%a0
        adda.l #0x20000,%a0
        tst.b 0x1379(%a0)
        beq.s playhead_ui_pattern_length
        movea.l %d4,%a0
        move.l #0x499,%d7
        mulu.l %d0,%d7
        adda.l %d7,%a0
        mvz.w 969(%a0),%d7
        bra.s playhead_ui_length
playhead_ui_pattern_length:
        mvz.w 0x1374(%a0),%d7
playhead_ui_length:
        cmp.b 8(%a1),%d7
        bne.w playhead_ui_slow
        btst #1,5(%a1)
        bne.s playhead_ui_reverse
        mvz.b (%a5),%d1
        cmp.l %d7,%d1
        bcc.w playhead_ui_slow
        cmp.b (%a1),%d1
        bne.w playhead_ui_slow
        mvz.b (%a6),%d1
        cmp.l %d7,%d1
        bcc.w playhead_ui_slow
        cmp.b (%a1),%d1
        beq.s playhead_ui_same
        cmp.b 2(%a1),%d1
        bne.w playhead_ui_slow
        mvz.b 3(%a1),%d1
        bra.s playhead_ui_cached
playhead_ui_same:
        mvz.b 1(%a1),%d1
playhead_ui_cached:
        movea.l 12(%sp),%a0
        move.b %d1,(%a0,%d0.l)
        move.b 1(%a1),(%a4)+
        addq.l #1,%a5
        addq.l #1,%a6
        bra.w playhead_ui_done
playhead_ui_reverse:
        mvz.b (%a6)+,%d1
        cmp.l %d7,%d1
        bcc.s 1f
        neg.l %d1
        add.l %d7,%d1
        subq.l #1,%d1
1:      movea.l 12(%sp),%a0
        move.b %d1,(%a0,%d0.l)
        mvz.b (%a5)+,%d1
        cmp.l %d7,%d1
        bcc.s 2f
        neg.l %d1
        add.l %d7,%d1
        subq.l #1,%d1
2:      move.b %d1,(%a4)+
        bra.s playhead_ui_done
playhead_ui_forward:
        movea.l 12(%sp),%a1
        move.b (%a6)+,(%a1,%d0.l)
        move.b (%a5)+,(%a4)+
        bra.s playhead_ui_done
playhead_ui_slow:
        move.l %d0,%d7
        movea.l %d4,%a0
        moveq #0,%d1
        move.b (%a6)+,%d1
        bsr.w playhead_peek              | the step before: already chosen
        movea.l 12(%sp),%a1
        move.b %d1,(%a1,%d0.l)          | the UI's last[t]
        moveq #0,%d1
        move.b (%a5)+,%d1
        bsr.w playhead_mirror
        move.b %d1,(%a4)+               | the UI's step[t]
playhead_ui_done:
        movem.l (%sp),%d1/%d7/%a0/%a1
        lea 16(%sp),%sp
        movea.l %d7,%a1
        addq.l #1,%d0
        jmp 0x4008d23a

| 4008b746: lea 43bd7414,a0, then mvz.b 2(a0,d0.l),d0 / rts. The accessor
| 4008b734(t) gives the step a track plays, last[t], for t 0..12: the
| playhead the trig LEDs show (400c20ee). It runs in the UI task. d0 = t;
| d0-d1/a0-a1 are the caller's scratch.
.globl playhead_step_shown
playhead_step_shown:
        lea 0x43bd8416,%a0
        moveq #0,%d1
        move.b (%a0,%d0.l),%d1
        move.l %d7,-(%sp)
        move.l %d0,%d7
        movea.l 0x43bd8430,%a0
        bsr.w playhead_peek              | UI task: never chooses a step
        move.l (%sp)+,%d7
        move.l %d1,%d0
        rts

| IRQ exclusion and one code halfword write with the core's cache sequence
| (mods/core-st/machine-boot.s).
.globl playhead_seq_lock,playhead_seq_unlock,playhead_seq_code16,playhead_seq_sync
playhead_seq_lock:
        moveq #0,%d0
        move.w %sr,%d0
        move.w #0x2700,%sr
        rts
playhead_seq_unlock:
        move.l 4(%sp),%d0
        move.w %d0,%sr
        rts
playhead_seq_code16:
        movea.l 4(%sp),%a0
        move.l 8(%sp),%d0
        move.w %d0,(%a0)
        move.l %a0,%d0
        andi.l #-16,%d0
        movea.l %d0,%a0
        cpushl %dc,(%a0)
        rts
playhead_seq_sync:
        nop
        move.l #0xa40ce100,%d0          | native MAIN CACR + instruction invalidate
        movec %d0,%cacr
        rts
