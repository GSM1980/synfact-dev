| SPDX-License-Identifier: GPL-2.0-or-later
| LFO3 page action entries, ported from Synfact actions_proto/actions_entry.s
| with elekloader IDs. The layout of pages 37/38 comes through the core's
| PAGE_LAYOUT operation (ui.c); the rest are module-installed sites.
        .text
        .set LFO3_ID,0x3d9
| Page ID of a view for actions 4003792c (view): 37/38 on the LFO3 page.
        .globl lfo3_actions_page_entry
lfo3_actions_page_entry:
        move.l 4(%sp),-(%sp)
        jsr lfo3_actions_page
        addq.l #4,%sp
        cmpi.l #-1,%d0
        beq.w 1f
        rts
1:      movea.l 4(%sp),%a0
        move.l 0x90(%a0),%d0
        jmp 0x40037934

| Page copy capture 4001f16c (hidden A0 result, app, page).
        .globl lfo3_actions_capture_entry
lfo3_actions_capture_entry:
        move.l 8(%sp),%d0
        subi.l #37,%d0
        cmpi.l #2,%d0
        bcc.w 1f
        move.l %a0,-(%sp)
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr lfo3_actions_capture
        lea 12(%sp),%sp
        move.l (%sp)+,%d0
        rts
1:      lea -80(%sp),%sp
        movem.l %d2-%d3/%a2-%a6,(%sp)
        jmp 0x4001f174

| Paste compatibility 4001f656 (A0 = clipboard, D5 = target page).
        .globl lfo3_actions_paste_compat
lfo3_actions_paste_compat:
        lea -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l %d5,-(%sp)
        move.l 4(%a0),-(%sp)
        jsr lfo3_actions_compatible
        addq.l #8,%sp
        tst.l %d0
        beq.w 1f
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        clr.l %d7
        jmp 0x4001f6c8
1:      movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        moveq #4,%d1
        move.l 4(%a0),%d0
        cmp.l %d0,%d1
        jmp 0x4001f65e

| Paste translation 4001f6d4 (addq #8,sp; tst.b d7; beq.s 4001f6f8).
        .globl lfo3_actions_paste_translate
lfo3_actions_paste_translate:
        addq.l #8,%sp
        lea -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        movea.l -28(%a6),%a0
        move.l 4(%a0),-(%sp)
        move.l %d5,-(%sp)
        pea -12(%a6)
        jsr lfo3_actions_translate
        lea 12(%sp),%sp
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        tst.b %d7
        beq.w 1f
        jmp 0x4001f6da
1:      jmp 0x4001f6f8

| Randomize metadata 400149b0 (D2 = ID): LFO3 uses its LFO2 descriptor.
        .globl lfo3_actions_random_metadata
lfo3_actions_random_metadata:
        move.l %d2,%d0
        subi.l #LFO3_ID,%d0
        cmpi.l #10,%d0
        bcc.w 1f
        addi.l #0x60,%d0
        jmp 0x400149be
1:      cmpi.l #504,%d2
        jmp 0x400149b6

| Randomize destination 400149e2 (D2 = ID): DEST3 draws with mask 0x200.
        .globl lfo3_actions_random_dest
lfo3_actions_random_dest:
        cmpi.l #LFO3_ID+3,%d2
        bne.w 1f
        move.l #0x200,%d0
        jmp 0x40014a08
1:      moveq #89,%d0
        cmp.l %d2,%d0
        beq.w 2f
        jmp 0x400149e8
2:      jmp 0x400149f0

| Randomize/reload page bounds 400385fc / 40038666 (D2 = page): 37/38 accepted.
        .macro page_bound name,accept,reject
        .globl \name
\name:
        cmpi.l #37,%d2
        beq.w \name\()_accept
        cmpi.l #38,%d2
        beq.w \name\()_accept
        moveq #32,%d0
        cmp.l %d2,%d0
        bge.w \name\()_accept
        jmp \reject
\name\()_accept:
        jmp \accept
        .endm
        page_bound lfo3_actions_random_bound,0x40038602,0x40038636
        page_bound lfo3_actions_reload_bound,0x4003866c,0x400386bc

| Kit reload 4001e412: the reloaded kit buffer is a known kit slot for the
| codec (reload_buffer) during the call; 4001e4ae records the read buffer.
        .globl lfo3_actions_reload_scope,lfo3_actions_reload_read,lfo3_actions_reload_parameter
lfo3_actions_reload_scope:
        move.l reload_buffer,%d0
        move.l %d0,-(%sp)
        move.l 24(%sp),-(%sp)
        move.l 24(%sp),-(%sp)
        move.l 24(%sp),-(%sp)
        move.l 24(%sp),-(%sp)
        move.l 24(%sp),-(%sp)
        jsr 1f
        lea 20(%sp),%sp
        move.l (%sp)+,%d1
        move.l %d1,reload_buffer
        rts
1:      link.w %a6,#-60
        movem.l %d2-%d7/%a2-%a5,(%sp)
        jmp 0x4001e41a
lfo3_actions_reload_read:
        jsr 0x401106ce
        move.l 4(%sp),%d1
        move.l %d1,reload_buffer
        jmp 0x4001e4b4
| Reload of one parameter 4001e574 (D7 = ID, D3 = kit, -8(a6) = source,
| 20(a6) = track): LFO3 words come from the reloaded trailer.
lfo3_actions_reload_parameter:
        move.l %d7,%d0
        subi.l #LFO3_ID,%d0
        cmpi.l #10,%d0
        bcc.w 1f
        move.l %d7,-(%sp)
        move.l 20(%a6),-(%sp)
        move.l %d3,-(%sp)
        move.l -8(%a6),-(%sp)
        jsr lfo3_actions_reload_word
        lea 16(%sp),%sp
        jmp 0x4001e5de
1:      cmpi.l #504,%d7
        jmp 0x4001e57a
