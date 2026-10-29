| SPDX-License-Identifier: GPL-2.0-or-later
| Minimal port of Synfact audio/system_info_meter.s, Syntakt OS 1.41.
| AUDIO CPU, NOT MAIN. Fixed companion in the existing Synfact image padding
| 40040860..40041050. State is initialized in the image, outside native BSS.
| No machines, native tables, receiver for writes, boot or vector changes.
| A valid read-only CMD4 request enables timing. Every other command is ignored.
        .text
        .globl audio_enter,audio_exit,audio_code_end,audio_state
audio_enter:
        lea -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        lea audio_state,%a0
        clr.l 20(%a0)
        lea 0x800,%a1
        move.l (%a1),%d0
        cmpi.l #0x48543133,%d0
        bne.s enter_done
        move.l 4(%a1),%d0
        move.l %d0,%d1
        andi.l #65535,%d1
        cmpi.l #4,%d1
        bne.s enter_done
        tst.l 8(%a1)
        bne.s enter_done
        tst.l 12(%a1)
        bne.s enter_done
        addi.l #0x48543133,%d0
        cmp.l 16(%a1),%d0
        bne.s enter_done
        moveq #0,%d0
        move.w 0xfc070000,%d0
        cmpi.l #0xc1,%d0
        bne.s check_mode
        move.l #0xc3,%d0
        move.w %d0,0xfc070000
check_mode:
        cmpi.l #0xc3,%d0
        bne.s enter_done
        move.l 0xfc07000c,%d0
        move.l %d0,(%a0)
        move.l %sp,%d0
        cmp.l 12(%a0),%d0
        bcc.s stack_kept
        move.l %d0,12(%a0)
stack_kept:
        moveq #1,%d0
        move.l %d0,20(%a0)
enter_done:
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        lea 0x400010e4,%a5             | native displaced instruction; D0=1
        rts
audio_exit:
        lea audio_state,%a0
        tst.l 20(%a0)
        beq.w exit_done
        move.l 0xfc07000c,%d0
        move.l %d0,8(%a0)
        sub.l (%a0),%d0
        add.l %d0,4(%a0)
        addq.l #1,16(%a0)
        lea 0x800,%a1
        | Recheck the complete immutable request before touching its extension.
        move.l (%a1),%d0
        cmpi.l #0x48543133,%d0
        bne.w exit_done
        move.l 4(%a1),%d0
        move.l %d0,%d1
        andi.l #65535,%d1
        cmpi.l #4,%d1
        bne.s exit_done
        tst.l 8(%a1)
        bne.s exit_done
        tst.l 12(%a1)
        bne.s exit_done
        move.l %d0,%d1
        addi.l #0x48543133,%d1
        cmp.l 16(%a1),%d1
        bne.s exit_done
        lsr.l #8,%d0
        lsr.l #8,%d0
        beq.s exit_done
        cmp.l 164(%a1),%d0
        beq.s exit_done
        clr.l 164(%a1)
        move.l #0x53493134,%d1
        move.l %d1,160(%a1)
        move.l 4(%a0),%d1
        move.l %d1,168(%a1)
        move.l 8(%a0),%d1
        move.l %d1,172(%a1)
        move.l 12(%a0),%d1
        move.l %d1,176(%a1)
        move.l 16(%a0),%d1
        move.l %d1,180(%a1)
        move.l 20(%a0),%d1
        move.l %d1,184(%a1)
        move.l %d0,164(%a1)            | generation last, then acknowledgement
        lsl.l #8,%d0
        lsl.l #8,%d0
        addq.l #1,%d0
        move.l %d0,24(%a1)
exit_done:
        tst.l 0x40040050              | exact native flags for BEQ at 40000b0a
        rts
audio_code_end:
        .balign 4
audio_state:
        .long 0,0,0,0x48000000,0,0
