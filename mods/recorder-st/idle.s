| SPDX-License-Identifier: GPL-2.0-or-later
| Preview-only mixer, selected by the core's Host API 15 owner slot.
| Native OS 1.42 mixer ABI: three stack words; third is the mixer/Kit record.
| Own the same 556-byte frame/callee saves as 4008f1ca; 4008fb38 initializes
| its output locals, handles analog/digital output and USB, and unwinds it.
        .text
        .globl recorder_idle_mix
recorder_idle_mix:
        link %a6,#-556
        movem.l %d2-%d7/%a2-%a5,(%sp)
        movea.l 16(%a6),%a2
        lea 0x8000dad0,%a0
        move.l #384,%d0
1:      clr.l (%a0)+
        subq.l #1,%d0
        bne.s 1b
        jsr recorder_audio
        move.l #0xa0,%macsr
        moveq #0,%d0
        move.l %d0,%acc0
        move.l %d0,%acc1
        move.l %d0,%acc2
        move.l %d0,%acc3
        move.l #0x80000000,%d3
        jmp 0x4008fbb0
