| SPDX-License-Identifier: GPL-2.0-or-later
| CPU2 firmware companion, separate memory from MAIN. Native boot transport,
| sample parser, and real-time scheduler retain their original ownership.
| Absolute-target glue lives in the upper reserve (sdk/syntakt/audio.py).
        .section .text.upper,"ax"
        .globl audio_machine_boot,audio_machine_silent,audio_machine_flush
audio_machine_boot:
        jsr 0x40000d06
        jsr audio_machine_install
        jmp 0x40001860
audio_machine_silent:
        rts
        .globl audio_machine_exit
audio_machine_exit:
        jsr audio_machine_receive
        .ifdef WITH_METER
        jmp audio_exit
        .else
        tst.l 0x40040050
        rts
        .endif
audio_machine_flush:
        movea.l 4(%sp),%a0
        move.l 8(%sp),%d0
1:      cpushl %dc,(%a0)
        lea 16(%a0),%a0
        subi.l #16,%d0
        bgt.s 1b
        rts
