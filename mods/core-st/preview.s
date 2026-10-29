| SPDX-License-Identifier: GPL-2.0-or-later
| Lock preview 40039160 (view, ID, locked*) with trigs selected, without the
| heap. Stock builds a 28-byte std::function (operator new), walks the
| selection with 400c3670 calling 40037a34 per selected trig, then frees it:
| on every value read while a trig is held, by every page. This is that walk
| with the same reads and calls in the same order: track length, selection
| test, both selection words per position, the lock query and (pool IDs,
| except 0x52) the pool query, stopping at the first lock or pool sound.
| Nothing is cached between calls. Entered at 400391a8, after the trig test;
| the native tail at 4003921e follows. Native frame F: +14 pool found, +15
| pool query, +16 lock value, +20 pool value, +48 ID; A2 view, A3 locked*.
| D2 is the native body's scratch. Track length is at most 64 steps.
        .section .run,"ax"
        .globl core_preview_selected
core_preview_selected:
        lea -12(%sp),%sp                    | F = SP+12
        movem.l %d3-%d4/%a4,(%sp)
        jsr 0x4016e804                      | application
        move.l %d0,-(%sp)
        jsr 0x40019036                      | its track model
        move.l %d0,(%sp)
        jsr 0x40027f3a                      | track length
        move.l %d0,%d3
        movea.l 0x6c(%a2),%a4               | selection
        move.l %a4,(%sp)
        jsr 0x400c34d6
        addq.l #4,%sp
        tst.b %d0
        beq.s 9f
        moveq #0,%d2                        | position
1:      cmp.l %d3,%d2
        bge.s 9f
        move.l 0x15c(%a4),%d0               | low word (positions 0..31)
        move.l 0x158(%a4),%d1               | high word (32..63)
        moveq #32,%d4
        cmp.l %d4,%d2
        bcs.s 2f
        move.l %d1,%d0
        moveq #64,%d4
        cmp.l %d4,%d2
        bcc.s 8f
2:      btst %d2,%d0                        | bit number modulo 32
        beq.s 8f
        moveq #0,%d4                        | stop
        movea.l (%a2),%a0
        move.l 60(%sp),-(%sp)               | ID
        move.l %d2,-(%sp)
        move.l %a2,-(%sp)
        movea.l 0x90(%a0),%a0
        jsr (%a0)                           | lock value at this position
        lea 12(%sp),%sp
        move.l %d0,28(%sp)
        moveq #-1,%d1
        cmp.l %d0,%d1
        beq.s 3f
        moveq #1,%d4
        move.b %d4,(%a3)
3:      tst.b 27(%sp)
        beq.s 4f
        moveq #0x52,%d0
        cmp.l 60(%sp),%d0
        beq.s 4f
        movea.l (%a2),%a0
        move.l %d2,-(%sp)
        move.l %a2,-(%sp)
        movea.l 0xa4(%a0),%a0
        jsr (%a0)                           | pool sound at this position
        addq.l #8,%sp
        move.l %d0,32(%sp)
        moveq #-1,%d1
        cmp.l %d0,%d1
        beq.s 4f
        moveq #1,%d4
        move.b %d4,26(%sp)
4:      tst.l %d4
        bne.s 9f
8:      addq.l #1,%d2
        bra.s 1b
9:      movem.l (%sp),%d3-%d4/%a4
        lea 12(%sp),%sp
        jmp 0x4003921e
        .globl core_preview_selected_end
core_preview_selected_end:
