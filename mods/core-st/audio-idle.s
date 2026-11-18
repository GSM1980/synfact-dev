| SPDX-License-Identifier: GPL-2.0-or-later
| OS 1.42: replace only the JSR at 400a5046. Normal path tail-calls the
| original mixer without changing its arguments/registers. The retained
| bank owner supplies preview; project workers select core-owned silence.
        .section .run,"ax"
        .globl core_audio_idle_mix,core_audio_idle_mix_end
core_audio_idle_mix:
        tst.l core_audio_idle_active
        bne.s 1f
        jmp 0x4008f242
1:      moveq #2,%d0
        cmp.l core_audio_idle_active,%d0
        beq.s core_project_idle_mix
        movea.l core_audio_idle_owner,%a0
        movea.l 4(%a0),%a0
        jmp (%a0)
core_audio_idle_mix_end:

| Render-in only. Return both D0 and CCR.Z as the block decision; the final
| MOVE sets Z and RTS preserves it. Same policy as the native-host C fixture.
        .globl core_audio_idle_begin
core_audio_idle_begin:
        moveq #0,%d0
        moveq #2,%d1
        cmp.l 0x43bd83bc,%d1
        bne.s 3f
        tst.l core_project_depth
        beq.s 2f
        moveq #2,%d0
        bra.s 3f
2:      movea.l core_audio_idle_owner,%a0
        tst.l %a0
        beq.s 3f
        movea.l (%a0),%a0
        jsr (%a0)
3:      move.l %d0,core_audio_idle_active
        rts

| Same reduced mixer/output ABI as the recorder, with silent project output.
| No bank owner is required. The decision above survives a callback returning
| between render-in and the mixer; never consult the live depth here.
        .globl core_project_idle_mix,core_project_idle_mix_end
core_project_idle_mix:
        link %a6,#-556
        movem.l %d2-%d7/%a2-%a5,(%sp)
        movea.l 16(%a6),%a2
        lea 0x8000dad0,%a0
        move.l #384,%d0
1:      clr.l (%a0)+
        subq.l #1,%d0
        bne.s 1b
        move.l #0xa0,%macsr
        moveq #0,%d0
        move.l %d0,%acc0
        move.l %d0,%acc1
        move.l %d0,%acc2
        move.l %d0,%acc3
        move.l #0x80000000,%d3
        jmp 0x4008fbb0
core_project_idle_mix_end:
