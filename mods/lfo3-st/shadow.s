| SPDX-License-Identifier: GPL-2.0-or-later
| Shadow-layer entries, ported from Synfact research-runtime/lfo3-coherence-
| 20261021/shadow_proto/shadow_entry.s. Each entry replaces whole stock
| instructions at a module-installed site (main.c, preimages checked); the
| replay labels run the displaced instructions and continue natively. Every
| wrapper preserves the native D0 and all callee-saved registers. Task-side
| mutations republish the engines (ui.c lfo3_republish).
        .text
| ---- memcpy 40115f78(dst, src, n). Fast path: sizes below an FX record.
        .globl  lfo3_memcpy_entry,lfo3_irq_mask
lfo3_memcpy_entry:
        move.l  12(%sp),%d0
        cmpi.l  #0x8e,%d0
        bcs.s   1f
        jsr     lfo3_irq_mask
        bne.s   1f                      | ISR or masked task: native copy only
        move.l  %d2,-(%sp)
        move.l  16(%sp),-(%sp)          | n
        move.l  16(%sp),-(%sp)          | src
        move.l  16(%sp),-(%sp)          | dst
        jsr     lfo3_memcpy_native
        move.l  %d0,%d2
        jsr     lfo3_after_copy
        lea     12(%sp),%sp
        jsr     lfo3_republish
        move.l  %d2,%d0
        move.l  (%sp)+,%d2
        rts
1:      jmp     lfo3_memcpy_native
        .globl  lfo3_memcpy_native
lfo3_memcpy_native:
        movea.l 4(%sp),%a1
        movea.l 8(%sp),%a0
        jmp     0x40115f80

| Read-only privilege check: native task frames use SR=0x2000; a nonzero IPL
| (ISR or masked task) runs no shadow work.
lfo3_irq_mask:
        move.w  %sr,%d0
        andi.l  #0x700,%d0
        rts

| ---- constructors: Sound 400b3c38(sound,a,b), kit 400b330c(kit), FX 400b32b0(fx)
        .globl  lfo3_sound_new_entry,lfo3_kit_new_entry,lfo3_fx_new_entry
lfo3_sound_new_entry:
        jsr     lfo3_irq_mask
        bne.w   2f
        move.l  %d2,-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        jsr     2f
        lea     12(%sp),%sp
        move.l  %d0,%d2
        move.l  8(%sp),-(%sp)
        jsr     lfo3_sound_new
        addq.l  #4,%sp
        jsr     lfo3_republish
        move.l  %d2,%d0
        move.l  (%sp)+,%d2
        rts
2:      lea     -40(%sp),%sp
        movem.l %d2-%d3/%a2-%a6,(%sp)
        jmp     0x400b3c40
lfo3_kit_new_entry:
        jsr     lfo3_irq_mask
        bne.w   2f
        move.l  %d2,-(%sp)
        move.l  8(%sp),-(%sp)
        jsr     2f
        move.l  %d0,%d2
        jsr     lfo3_kit_new
        addq.l  #4,%sp
        jsr     lfo3_republish
        move.l  %d2,%d0
        move.l  (%sp)+,%d2
        rts
2:      lea     -36(%sp),%sp
        clr.l   %d0
        movem.l %d2-%d5/%a2-%a6,(%sp)
        jmp     0x400b3316
lfo3_fx_new_entry:
        jsr     lfo3_irq_mask
        bne.w   2f
        move.l  %d2,-(%sp)
        move.l  8(%sp),-(%sp)
        jsr     2f
        move.l  %d0,%d2
        jsr     lfo3_fx_new
        addq.l  #4,%sp
        jsr     lfo3_republish
        move.l  %d2,%d0
        move.l  (%sp)+,%d2
        rts
2:      lea     -28(%sp),%sp
        movem.l %d2/%a2-%a4,(%sp)
        jmp     0x400b32b8

| ---- vector<Kit> relocation helper 40177794(src, end, dst); only its two
| reallocation callers (return 40177852 / 40177918) move side-table keys.
        .globl  lfo3_relocate_entry
lfo3_relocate_entry:
        move.l  (%sp),%d0
        cmpi.l  #0x40177852,%d0
        beq.s   2f
        cmpi.l  #0x40177918,%d0
        bne.w   3f
2:      jsr     lfo3_irq_mask
        bne.w   3f
        move.l  %d2,-(%sp)
        move.l  %d3,-(%sp)
        move.l  16(%sp),%d0
        sub.l   12(%sp),%d0
        move.l  %d0,-(%sp)              | n
        move.l  16(%sp),-(%sp)          | src
        move.l  28(%sp),-(%sp)          | dst
        jsr     lfo3_prepare_relocate
        lea     12(%sp),%sp
        move.l  %d0,%d3
        move.l  20(%sp),-(%sp)          | dst
        move.l  20(%sp),-(%sp)          | end
        move.l  20(%sp),-(%sp)          | src
        jsr     3f
        lea     12(%sp),%sp
        move.l  %d0,%d2
        move.l  16(%sp),%d0
        sub.l   12(%sp),%d0
        move.l  %d3,-(%sp)              | mask
        move.l  %d0,-(%sp)              | n
        move.l  20(%sp),-(%sp)          | src
        move.l  32(%sp),-(%sp)          | dst
        jsr     lfo3_finish_relocate
        lea     16(%sp),%sp
        move.l  %d2,%d0
        move.l  (%sp)+,%d3
        move.l  (%sp)+,%d2
        rts
3:      move.l  %d3,-(%sp)
        move.l  #0xa4073deb,%d3
        jmp     0x4017779c

| ---- kit encode 400a787c(storage, kit, flag) -> D0 ok
        .globl  lfo3_kit_encode_entry
lfo3_kit_encode_entry:
        move.l  %d2,-(%sp)
        move.l  16(%sp),-(%sp)          | flag
        move.l  16(%sp),-(%sp)          | kit
        move.l  16(%sp),-(%sp)          | storage
        addq.l  #1,kit_codec_depth
        jsr     2f
        subq.l  #1,kit_codec_depth
        lea     12(%sp),%sp
        move.l  %d0,%d2
        move.l  %d0,-(%sp)              | ok
        move.l  16(%sp),-(%sp)          | kit
        move.l  16(%sp),-(%sp)          | storage
        jsr     lfo3_kit_encoded
        lea     12(%sp),%sp
        move.l  %d2,%d0
        move.l  (%sp)+,%d2
        rts
2:      lea     -28(%sp),%sp
        movem.l %d2-%d4/%a2-%a5,(%sp)
        jmp     0x400a7884

| ---- kit decode 400a7828(kit, storage) -> D0.b ok
        .globl  lfo3_kit_decode_entry
lfo3_kit_decode_entry:
        move.l  %d2,-(%sp)
        move.l  12(%sp),-(%sp)          | storage
        move.l  12(%sp),-(%sp)          | kit
        addq.l  #1,kit_codec_depth
        jsr     2f
        subq.l  #1,kit_codec_depth
        addq.l  #8,%sp
        move.l  %d0,%d2
        move.l  %d0,-(%sp)              | ok
        move.l  16(%sp),-(%sp)          | storage
        move.l  16(%sp),-(%sp)          | kit
        jsr     lfo3_kit_decoded
        lea     12(%sp),%sp
        jsr     lfo3_republish
        move.l  %d2,%d0
        move.l  (%sp)+,%d2
        rts
2:      move.l  4(%sp),%d0
        movea.l 8(%sp),%a0
        jmp     0x400a7830              | CCR from the first move, as natively

| ---- serializer 400ae338(container, live, ?, flags, progress) -> D0:
| registers the destination container for the kit codec during the call.
        .globl  lfo3_serialize_entry
lfo3_serialize_entry:
        move.l  containers,-(%sp)
        move.l  8(%sp),%d0
        move.l  %d0,containers
        move.l  24(%sp),-(%sp)
        move.l  24(%sp),-(%sp)
        move.l  24(%sp),-(%sp)
        move.l  24(%sp),-(%sp)
        move.l  24(%sp),-(%sp)
        jsr     2f
        lea     20(%sp),%sp
        move.l  (%sp)+,containers
        rts
2:      lea     -44(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        jmp     0x400ae340

| ---- deserializer 400b0368(live, container, progress) -> D0.b
        .globl  lfo3_deserialize_entry
lfo3_deserialize_entry:
        move.l  containers,-(%sp)
        move.l  12(%sp),%d0
        move.l  %d0,containers
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        jsr     2f
        lea     12(%sp),%sp
        move.l  (%sp)+,containers
        rts
2:      lea     -40(%sp),%sp
        movem.l %d2-%d5/%a2,(%sp)
        jmp     0x400b0370

| ---- LOAD PROJECT decodes through 400adff8(live, container, progress).
        .globl  lfo3_decode_project_entry
lfo3_decode_project_entry:
        move.l  containers+4,-(%sp)
        move.l  12(%sp),%d0
        move.l  %d0,containers+4
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  4(%sp),-(%sp)           | container, not the live project
        jsr     lfo3_project_open
        addq.l  #4,%sp
        jsr     2f
        lea     12(%sp),%sp
        move.l  (%sp)+,containers+4
        rts
2:      lea     -36(%sp),%sp
        movem.l %d2-%d5/%a2-%a6,(%sp)
        jmp     0x400ae000

| ---- +Drive sounds (Synfact S1, final form of build_package.py): the save
| 40111b08(mmcfs, record, index) writes a 512-byte stack scratch holding the
| record + LFO3 trailer (0x11f B, one sector); the load 40110ac4(mmcfs,
| buffer, index) reads into the scratch and hands the caller its 0x107 B.
| lfo3_s1_prepare(record, output) / lfo3_s1_loaded(buffer, ok, input).
| main.c patches the two size operands 0x107 -> 0x11f with the hooks.
        .globl  lfo3_s1_save_entry,lfo3_s1_load_entry
lfo3_s1_save_entry:
        lea     -516(%sp),%sp
        move.l  %d2,(%sp)
        lea     4(%sp),%a0
        move.l  %a0,-(%sp)              | scratch
        move.l  528(%sp),-(%sp)         | record
        jsr     lfo3_s1_prepare
        addq.l  #8,%sp
        move.l  528(%sp),-(%sp)         | index
        move.l  %d0,-(%sp)              | scratch in place of the record
        move.l  528(%sp),-(%sp)         | mmcfs
        jsr     lfo3_s1_save_native
        lea     12(%sp),%sp
        move.l  (%sp),%d2
        lea     516(%sp),%sp
        rts
lfo3_s1_save_native:
        lea     -44(%sp),%sp
        movem.l %d2-%d5/%a2,(%sp)
        jmp     0x40111b10
lfo3_s1_load_entry:
        lea     -516(%sp),%sp
        move.l  %d2,(%sp)
        lea     4(%sp),%a0
        move.l  528(%sp),-(%sp)         | index
        move.l  %a0,-(%sp)              | scratch in place of the caller's buffer
        move.l  528(%sp),-(%sp)         | mmcfs
        jsr     lfo3_s1_load_native
        lea     12(%sp),%sp
        move.l  %d0,%d2
        lea     4(%sp),%a0
        move.l  %a0,-(%sp)              | input = scratch
        move.l  %d0,-(%sp)              | ok
        move.l  532(%sp),-(%sp)         | caller's buffer
        jsr     lfo3_s1_loaded
        lea     12(%sp),%sp
        move.l  %d2,%d0
        move.l  (%sp),%d2
        lea     516(%sp),%sp
        rts
lfo3_s1_load_native:
        lea     -44(%sp),%sp
        movem.l %d2-%d5,(%sp)
        jmp     0x40110acc

| ---- FX record codec used by incremental model refreshes (Synfact
| task_scopes): encode 400a6d96(record, fx, flag), decode 400a6d2e(fx, record).
        .globl  lfo3_fx_encode_entry,lfo3_fx_decode_entry
lfo3_fx_encode_entry:
        move.l  %d2,-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        move.l  16(%sp),-(%sp)
        jsr     2f
        lea     12(%sp),%sp
        move.l  %d0,%d2
        move.l  %d0,-(%sp)              | ok
        move.l  16(%sp),-(%sp)          | fx
        move.l  16(%sp),-(%sp)          | record
        jsr     lfo3_fx_encoded
        lea     12(%sp),%sp
        move.l  %d2,%d0
        move.l  (%sp)+,%d2
        rts
2:      move.l  %a3,-(%sp)
        move.l  %a2,-(%sp)
        movea.l 12(%sp),%a2
        jmp     0x400a6d9e
lfo3_fx_decode_entry:
        move.l  %d2,-(%sp)
        move.l  12(%sp),-(%sp)
        move.l  12(%sp),-(%sp)
        jsr     2f
        addq.l  #8,%sp
        move.l  %d0,%d2
        move.l  %d0,-(%sp)              | ok
        move.l  16(%sp),-(%sp)          | record
        move.l  16(%sp),-(%sp)          | fx
        jsr     lfo3_fx_decoded
        lea     12(%sp),%sp
        jsr     lfo3_republish
        move.l  %d2,%d0
        move.l  (%sp)+,%d2
        rts
2:      lea     -12(%sp),%sp
        movem.l %a2-%a4,(%sp)
        jmp     0x400a6d36

| ---- native LFO state reset 40118e24 (lea 444da751,a0): LFO3 states reset
| with the same field writes (Synfact lfo3_frame_native_reset).
        .globl  lfo3_native_reset_entry
lfo3_native_reset_entry:
        move.l  %d1,-(%sp)
        jsr     lfo3_native_reset
        move.l  (%sp)+,%d1
        lea     0x444da751,%a0
        jmp     0x40118e2a
