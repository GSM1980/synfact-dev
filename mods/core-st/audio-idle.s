| SPDX-License-Identifier: GPL-2.0-or-later
| OS 1.41: replace only the JSR at 400a4fce. Normal path tail-calls the
| original mixer without changing its arguments/registers. The retained
| bank owner supplies the preview replacement, with the same native C ABI.
        .section .run,"ax"
        .globl core_audio_idle_mix,core_audio_idle_mix_end
core_audio_idle_mix:
        tst.l core_audio_idle_active
        bne.s 1f
        jmp 0x4008f1ca
1:      movea.l core_audio_idle_owner,%a0
        movea.l 4(%a0),%a0
        jmp (%a0)
core_audio_idle_mix_end:

| Render-in only. Return both D0 and CCR.Z as the block decision; the final
| MOVE sets Z and RTS preserves it. Same policy as the native-host C fixture.
        .globl core_audio_idle_begin
core_audio_idle_begin:
        moveq #0,%d0
        movea.l core_audio_idle_owner,%a0
        tst.l %a0
        beq.s 3f
        moveq #2,%d1
        cmp.l 0x43bd73bc,%d1
        bne.s 3f
        movea.l (%a0),%a0
        jsr (%a0)
3:      move.l %d0,core_audio_idle_active
        rts
