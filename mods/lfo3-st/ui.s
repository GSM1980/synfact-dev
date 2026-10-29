| SPDX-License-Identifier: GPL-2.0-or-later
| LFO3 entry glue, ported from Synfact page/ui_bridge/widget/frame prototypes.
| Each lfo3_*_entry replaces whole stock instructions at a module-installed
| site (main.c, preimages checked); the replay labels execute the displaced
| instructions and continue at the next stock instruction. IDs: 0x3d9.. (10).
        .text
        .set LFO3_ID,0x3d9

| LfoPageView constructor 400f38f2: extend the page vector before the caller
| asks for the page count.
        .globl lfo3_constructor
lfo3_constructor:
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        jsr lfo3_constructor_native
        lea 12(%sp),%sp
        lea -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l 20(%sp),-(%sp)
        jsr lfo3_page_sync
        addq.l #4,%sp
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        rts
lfo3_constructor_native:
        lea -40(%sp),%sp
        movem.l %d2-%d3/%a2-%a3,(%sp)
        jmp 0x400f38fa

| Inside the native LFO draw (A2 = view, D2 = canvas), 400f37f8: the six
| displaced bytes load the page index and branch to the LFO2 wave.
        .globl lfo3_wave_site
lfo3_wave_site:
        lea -12(%sp),%sp
        movem.l %d1/%a0-%a1,(%sp)
        move.l %a2,-(%sp)
        jsr lfo3_page_is_third
        addq.l #4,%sp
        movem.l (%sp),%d1/%a0-%a1
        lea 12(%sp),%sp
        tst.l %d0
        bne.w 3f
        move.l 0x90(%a2),%d0
        tst.l %d0
        bne.w 2f
        jmp 0x400f37fe
2:      jmp 0x400f3848
3:      lea -12(%sp),%sp
        movem.l %d1/%a0-%a1,(%sp)
        move.l %d2,-(%sp)
        move.l %a2,-(%sp)
        jsr lfo3_page_wave
        addq.l #8,%sp
        movem.l (%sp),%d1/%a0-%a1
        lea 12(%sp),%sp
        jmp 0x400f38c0

| Value getter 40039160 (view, id, locked*).
        .globl lfo3_ui_get_entry
lfo3_ui_get_entry:
        move.l 8(%sp),%d0
        subi.l #LFO3_ID,%d0
        cmpi.l #10,%d0
        bcc.w 1f
        jmp lfo3_ui_get
1:      lea -40(%sp),%sp
        moveq #-1,%d0
        jmp 0x40039166

| Native value editor 40037f9a (view, id, delta, alternate, blocked*): LFO3
| IDs run the native editor (FUNC snap, alternate encoders, popups) unless the
| guard refuses (no record).
        .globl lfo3_editor_entry
lfo3_editor_entry:
        move.l 8(%sp),%d0
        subi.l #LFO3_ID,%d0
        cmpi.l #10,%d0
        bcc.w 1f
        move.l 20(%sp),-(%sp)
        move.l 20(%sp),-(%sp)
        move.l 20(%sp),-(%sp)
        move.l 20(%sp),-(%sp)
        move.l 20(%sp),-(%sp)
        jsr lfo3_editor_guard
        lea 20(%sp),%sp
        cmpi.l #-1,%d0
        beq.w 1f
        rts
1:      lea -52(%sp),%sp
        movem.l %d2-%d7/%a2-%a4,(%sp)
        jmp 0x40037fa2

| Generic range validator 400123e6 (model, id, value).
        .globl lfo3_widget_valid_entry,lfo3_native_range_check
lfo3_widget_valid_entry:
        move.l 8(%sp),%d0
        subi.l #LFO3_ID,%d0
        cmpi.l #10,%d0
        bcc.w lfo3_native_range_check
        jmp lfo3_widget_valid
lfo3_native_range_check:
        lea -24(%sp),%sp
        movem.l %d2-%d3/%a2,(%sp)
        jmp 0x400123ee

| Page header 40037d80 (view): "LFO3" on the added page.
        .globl lfo3_widget_header_entry
lfo3_widget_header_entry:
        move.l 4(%sp),-(%sp)
        jsr lfo3_widget_header
        addq.l #4,%sp
        tst.l %d0
        beq.w 1f
        rts
1:      movea.l 4(%sp),%a0
        move.l 0x90(%a0),%d0
        jmp 0x40037d88

| Direct view getter 40037e50 (view, id, track).
        .globl lfo3_widget_view_get_entry
lfo3_widget_view_get_entry:
        move.l 8(%sp),%d0
        subi.l #LFO3_ID,%d0
        cmpi.l #10,%d0
        bcc.w 1f
        jmp lfo3_widget_view_get
1:      lea -24(%sp),%sp
        movem.l %d2-%d5/%a2-%a3,(%sp)
        jmp 0x40037e58

| Wave widget flags at 400f3708 (not 3706: native 400f3704 branches to 3708,
| Synfact field fix). D5 = control ID, A6 = wave flag set by 3706.
        .globl lfo3_widget_wave_flags
lfo3_widget_wave_flags:
        cmpi.l #LFO3_ID+4,%d5
        beq.w 1f
        cmpi.l #LFO3_ID+6,%d5
        bne.w 2f
1:      movea.l #1,%a6
2:      move.l 0x74(%a2),-(%sp)
        jsr 0x4001ff74
        jmp 0x400f3712

| Sound publication 400a31a2 (source, track) and its success tail 400a3226.
        .globl lfo3_sound_publication,lfo3_sound_success
lfo3_sound_publication:
        lea -8(%sp),%sp
        clr.l 4(%sp)
        jsr lfo3_publication_begin
        move.l 16(%sp),-(%sp)
        move.l 16(%sp),-(%sp)
        jsr 3f
lfo3_sound_complete:
        lea 8(%sp),%sp
        lea -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.w %ccr,%d0
        move.w %d0,16(%sp)
        move.l 20(%sp),-(%sp)
        clr.l -(%sp)
        move.l 40(%sp),-(%sp)
        move.l 40(%sp),-(%sp)
        jsr lfo3_publication_end
        lea 16(%sp),%sp
        move.w 16(%sp),%d0
        move.w %d0,%ccr
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 24(%sp),%sp
        rts
3:      lea -12(%sp),%sp
        movem.l %d2-%d3/%a2,(%sp)
        jmp 0x400a31aa
lfo3_sound_success:
        move.l (%sp),%d0
        cmpi.l #lfo3_sound_complete,%d0
        bne.w 1f
        moveq #1,%d0
        move.l %d0,16(%sp)
1:      jmp 0x400976b4

| FX publication 400a1d18 (source).
        .globl lfo3_fx_publication
lfo3_fx_publication:
        lea -8(%sp),%sp
        jsr lfo3_publication_begin
        move.l 12(%sp),-(%sp)
        jsr 3f
        lea 4(%sp),%sp
        lea -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.w %ccr,%d0
        move.w %d0,16(%sp)
        pea 1
        pea 1
        pea 12
        move.l 40(%sp),-(%sp)
        jsr lfo3_publication_end
        lea 16(%sp),%sp
        move.w 16(%sp),%d0
        move.w %d0,%ccr
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 24(%sp),%sp
        rts
3:      move.l 0x43bde444,%d0
        move.l %d0,%d1
        jmp 0x400a1d20

| Hook installation: IRQ exclusion and one code word write with the D-cache
| line pushed (as fast-main-st); lfo3_sync then invalidates I/branch caches.
        .globl lfo3_lock,lfo3_unlock,lfo3_code16
lfo3_lock:
        moveq #0,%d0
        move.w %sr,%d0
        move.w #0x2700,%sr
        rts
lfo3_unlock:
        move.l 4(%sp),%d0
        move.w %d0,%sr
        rts
lfo3_code16:
        movea.l 4(%sp),%a0
        move.l 8(%sp),%d0
        move.w %d0,(%a0)
        move.l %a0,%d0
        andi.l #-16,%d0
        movea.l %d0,%a0
        cpushl %dc,(%a0)
        rts

| Model resolver 4001c422 (owner, id, track): an LFO3 ID resolves the same
| model as its LFO2 counterpart (Synfact lfo3_resolver_entry).
        .globl lfo3_resolver_entry
lfo3_resolver_entry:
        move.l 8(%sp),%d0
        subi.l #LFO3_ID,%d0
        cmpi.l #10,%d0
        bcc.w 1f
        addi.l #0x60,%d0
        move.l 12(%sp),-(%sp)
        move.l %d0,-(%sp)
        move.l 12(%sp),-(%sp)
        jsr 1f
        lea 12(%sp),%sp
        rts
1:      lea -12(%sp),%sp
        moveq #13,%d0
        movem.l %d2-%d4,(%sp)
        jmp 0x4001c42c

| Encoder/popup metadata (Synfact ui_runtime.s): the native code accepts only
| IDs <= 0x1f8; LFO3 IDs use their LFO2 descriptors (snap with FUNC) and DEST
| opens the native destination popup with mask 0x200.
| 4003b8d4 (D0 = D2 = ID): cmpi.l #0x1f8,d0; bls.s; clr.l d0.
        .globl lfo3_encoder_metadata
lfo3_encoder_metadata:
        subi.l #LFO3_ID,%d0
        cmpi.l #10,%d0
        bcc.w 1f
        addi.l #0x60,%d0
        jmp 0x4003b8de
1:      move.l %d2,%d0
        cmpi.l #0x1f8,%d0
        bls.w 2f
        clr.l %d0
2:      jmp 0x4003b8de
| 4003baae (D2 = ID): cmpi.l #0x1f8,d2; bhi.w 4003bb76; then move.l d2,d0.
        .globl lfo3_encoder_finish_metadata
lfo3_encoder_finish_metadata:
        move.l %d2,%d0
        subi.l #LFO3_ID,%d0
        cmpi.l #10,%d0
        bcc.w 1f
        addi.l #0x60,%d0
        jmp 0x4003baba
1:      cmpi.l #0x1f8,%d2
        bhi.w 2f
        jmp 0x4003bab8
2:      jmp 0x4003bb76
| 4003b6be (D2 = ID): cmpi.l #0x1f8,d2; bhi.s 4003b6ca; move.l d2,d0.
        .globl lfo3_popup_metadata
lfo3_popup_metadata:
        cmpi.l #LFO3_ID+3,%d2
        bne.w 1f
        move.l #0x200,%d0
        move.l %d0,-28(%a6)
        jmp 0x4003b6fa
1:      cmpi.l #0x1f8,%d2
        bhi.w 2f
        move.l %d2,%d0
        jmp 0x4003b6cc
2:      jmp 0x4003b6ca
| 4003b11a (D3 = ID): moveq #0x59,d0; cmp.l d3,d0; beq.s 4003b128 (DEST group).
        .globl lfo3_group_destination
lfo3_group_destination:
        cmpi.l #LFO3_ID+3,%d3
        beq.w 1f
        moveq #0x59,%d0
        cmp.l %d3,%d0
        beq.w 1f
        jmp 0x4003b120
1:      jmp 0x4003b128

| Proxy backend (Synfact actions_entry.s): Sound/FX getters 40012986/400129ec
| (proxy, id), FX setter 40013516 (the Sound setter is the core's, query 24)
| and the relative calculator 40012316 (proxy, id, value, delta).
        .macro parameter_entry name,target,fallback
        .globl \name
\name:
        move.l 8(%sp),%d0
        subi.l #LFO3_ID,%d0
        cmpi.l #10,%d0
        bcc.w \fallback
        jmp \target
        .endm
        parameter_entry lfo3_sound_get,lfo3_actions_get,lfo3_sound_get_native
lfo3_sound_get_native:
        move.l %a2,-(%sp)
        move.l %d2,-(%sp)
        movea.l 12(%sp),%a2
        jmp 0x4001298e
        parameter_entry lfo3_fx_get,lfo3_actions_get,lfo3_fx_get_native
lfo3_fx_get_native:
        move.l %a2,-(%sp)
        move.l %d2,-(%sp)
        movea.l 12(%sp),%a2
        jmp 0x400129f4
        parameter_entry lfo3_fx_store,lfo3_actions_store,lfo3_fx_store_native
lfo3_fx_store_native:
        link.w %a6,#-36
        movem.l %d2-%d6/%a2,(%sp)
        jmp 0x4001351e
        parameter_entry lfo3_relative_entry,lfo3_actions_relative,lfo3_native_relative
        .globl lfo3_native_relative
lfo3_native_relative:
        lea -44(%sp),%sp
        movem.l %d2-%d7/%a2-%a3,(%sp)
        jmp 0x4001231e

| Alternate encoder dispatch 4001306e / 40038124 (D2 = ID, A0 = callback
| object): DEST3 uses the native group selector with mask 0x200.
        .macro alternate_entry name,next
        .globl \name
\name:
        cmpi.l #LFO3_ID+3,%d2
        bne.w \name\()_native
        jsr lfo3_editor_destination_alt
        jmp \next
\name\()_native:
        movea.l 0x50(%a0),%a0
        jsr (%a0)
        jmp \next
        .endm
        alternate_entry lfo3_editor_proxy_alt,0x40013074
        alternate_entry lfo3_editor_pattern_alt,0x4003812a

| Relative destination 40014c48 / 40014e32 (proxy, id, value, delta).
        .macro relative_entry name,next
        .globl \name
\name:
        move.l 8(%sp),%d0
        cmpi.l #LFO3_ID+3,%d0
        bne.w \name\()_native
        jmp lfo3_editor_destination_relative
\name\()_native:
        move.l %a2,-(%sp)
        move.l %d2,-(%sp)
        moveq #0x59,%d2
        move.l 12(%sp),%d1
        jmp \next
        .endm
        relative_entry lfo3_relative_destination,0x40014c52
        relative_entry lfo3_relative_fx_destination,0x40014e3c

| Destination validation 40014ad8 / 40014cc2 / 40014eac (model, id, raw).
        .globl lfo3_validate_entry,lfo3_validate_alt_entry,lfo3_validate_third_entry
lfo3_validate_entry:
        move.l 8(%sp),%d0
        cmpi.l #LFO3_ID+3,%d0
        bne.w 1f
2:      move.l 12(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr lfo3_destination_valid
        addq.l #8,%sp
        rts
1:      link.w %a6,#-28
        movem.l %d2-%d3/%a2-%a3,(%sp)
        jmp 0x40014ae0
lfo3_validate_alt_entry:
        move.l 8(%sp),%d0
        cmpi.l #LFO3_ID+3,%d0
        beq.w 2b
        link.w %a6,#-28
        movem.l %d2-%d3/%a2-%a3,(%sp)
        jmp 0x40014cca
lfo3_validate_third_entry:
        move.l 8(%sp),%d0
        cmpi.l #LFO3_ID+3,%d0
        beq.w 2b
        link.w %a6,#-28
        movem.l %d2-%d3/%a2,(%sp)
        jmp 0x40014eb4

| Native target list (model, mask, shared_vector* via A0 hidden result) and release.
        .globl lfo3_native_targets,lfo3_native_release
lfo3_native_targets:
        movea.l 12(%sp),%a0
        move.l 8(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr 0x4001471c
        addq.l #8,%sp
        rts
lfo3_native_release:
        move.l 4(%sp),%d0
        addq.l #4,%d0
        move.l %d0,-(%sp)
        jsr 0x4016dde8
        addq.l #4,%sp
        rts
