| SPDX-License-Identifier: GPL-2.0-or-later
| Syntakt 1.41: generic machine queries. C convention at function entries.
| No machine-specific code is linked here; no SMZ means stock replays.
        .section .run,"ax"
| A native lookup never needs a C machine query. Keep the exact stock replay
| and hidden result registers; extended IDs still use the existing registry.
        .macro STOCK_FIRST name,arg,first
        tst.l core_machine_active
        beq.w core_stock_\name
        .if \first
        move.l \arg(%sp),%d0
        cmpi.l #\first,%d0
        bcs.w core_stock_\name
        .endif
        .endm
| Core 1.2: also ask the providers for parameter 72, the dormant Digitone
| chorus send, which has no stock labels or formatter (the chorus's CHO).
        .macro STOCK_FIRST_SEND name,arg
        tst.l core_machine_active
        beq.w core_stock_\name
        move.l \arg(%sp),%d0
        cmpi.l #0x1f9,%d0
        bcc.s 1f
        cmpi.l #72,%d0
        bne.w core_stock_\name
1:
        .endm
        .globl core_machine_name_full,core_stock_name_full
core_machine_name_full:
        STOCK_FIRST name_full,4,64
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 0
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_name_full:
        .byte 0x72,0x2c,0x20,0x2f,0x00,0x04,0xb2,0x80
        jmp 0x400a6694
        .globl core_machine_name_short,core_stock_name_short
core_machine_name_short:
        STOCK_FIRST name_short,4,64
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 1
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_name_short:
        .byte 0x72,0x2c,0x20,0x2f,0x00,0x04,0xb2,0x80
        jmp 0x400a66b4
        .globl core_machine_group_name,core_stock_group_name
core_machine_group_name:
        STOCK_FIRST group_name,4,64
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 2
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_group_name:
        .byte 0x72,0x2c,0x20,0x2f,0x00,0x04,0xb2,0x80
        jmp 0x400a66d4
        .globl core_machine_menu_category,core_stock_menu_category
core_machine_menu_category:
        STOCK_FIRST menu_category,4,64
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 3
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_menu_category:
        .byte 0x72,0x2c,0x20,0x2f,0x00,0x04,0xb2,0x80
        jmp 0x4002eebc
        .globl core_machine_availability,core_stock_availability
core_machine_availability:
        STOCK_FIRST availability,4,64
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 4
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_availability:
        .byte 0x2f,0x02,0x74,0x2c,0x20,0x2f,0x00,0x08
        jmp 0x400a6482
        .globl core_machine_digital_kind,core_stock_digital_kind
core_machine_digital_kind:
        STOCK_FIRST digital_kind,4,64
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 5
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_digital_kind:
        .byte 0x72,0x23,0x20,0x2f,0x00,0x04,0xb2,0x80
        jmp 0x400a63cc
        .globl core_machine_ui_to_engine,core_stock_ui_to_engine
core_machine_ui_to_engine:
        STOCK_FIRST ui_to_engine,4,0x2e0
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 6
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_ui_to_engine:
        .byte 0x20,0x2f,0x00,0x04,0x0c,0x80,0x00,0x00,0x02,0xdf
        jmp 0x400a6358
        .globl core_machine_local_slot,core_stock_local_slot
core_machine_local_slot:
        STOCK_FIRST local_slot,4,0x1f9
        move.l core_machine_action_codec,%d0
        beq.s 9f
        move.l %a1,-(%sp)
        movea.l %d0,%a1
        move.l (%a1),%d0
        cmp.l 8(%sp),%d0
        bne.s 8f
        move.l 4(%sp),%d0
        cmpi.l #0x400129b0,%d0
        beq.s 7f
        cmpi.l #0x40013348,%d0
        bne.s 8f
        move.l %d6,-(%sp)
        move.l %d7,-(%sp)
        move.l %d5,-(%sp)
        move.l %d3,-(%sp)
        move.l %a2,-(%sp)
        movea.l 12(%a1),%a1
        jsr (%a1)
        lea 20(%sp),%sp
        movea.l (%sp)+,%a1
        move.l %d0,%d3
        move.l 4(%sp),-(%sp)
        jsr core_machine_parameter_slot
        addq.l #4,%sp
        rts
7:      move.l %a2,-(%sp)
        movea.l 8(%a1),%a1
        jsr (%a1)
        addq.l #4,%sp
        movea.l (%sp)+,%a1
        | Return from native 40012986 after its two saved registers.
        addq.l #8,%sp
        move.l (%sp)+,%d2
        movea.l (%sp)+,%a2
        rts
8:      movea.l (%sp)+,%a1
9:
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 7
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_local_slot:
        .byte 0x22,0x2f,0x00,0x04,0x0c,0x81,0x00,0x00,0x01,0xf9
        jmp 0x400a5c54
        .globl core_machine_owner,core_stock_owner
core_machine_owner:
        STOCK_FIRST owner,4,0x1f9
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 8
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_owner:
        .byte 0x22,0x2f,0x00,0x04,0x0c,0x81,0x00,0x00,0x01,0xf9
        jmp 0x400a5c76
        .globl core_machine_engine_to_ui,core_stock_engine_to_ui
core_machine_engine_to_ui:
        STOCK_FIRST engine_to_ui,4,0x1f9
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 9
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_engine_to_ui:
        .byte 0x20,0x2f,0x00,0x04,0x0c,0x80,0x00,0x00,0x01,0xf8
        jmp 0x400a6374
        .globl core_machine_slot_to_engine,core_stock_slot_to_engine
core_machine_slot_to_engine:
        STOCK_FIRST slot_to_engine,8,64
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
| Reuse this lookup boundary for the native confirmed-selection transaction.
| A2=model, D5.b=quiet; all other lookups (including loads/previews) stay pure.
        move.l 12(%sp),%d0
        cmpi.l #0x40024e04,%d0
        bne.s 99f
        move.l 16(%sp),%d0
        cmpi.l #0x1a,%d0
        bne.s 99f
        move.l %d5,-(%sp)
        move.l %a2,-(%sp)
        move.l 28(%sp),-(%sp)
        jsr core_machine_defaults
        lea 12(%sp),%sp
99:
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 10
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_slot_to_engine:
        .byte 0x2f,0x0a,0x2f,0x02,0x24,0x2f,0x00,0x10
        jmp 0x400a63f4
        .globl core_machine_page_local,core_stock_page_local
core_machine_page_local:
        STOCK_FIRST page_local,8,64
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 11
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_page_local:
        .byte 0x2f,0x02,0x74,0x0c,0x22,0x2f,0x00,0x08
        jmp 0x400a6562
        .globl core_machine_page_copy,core_stock_page_copy
core_machine_page_copy:
        STOCK_FIRST page_copy,4,64
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 12
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_page_copy:
        .byte 0x2f,0x02,0x70,0x2c,0x24,0x08,0x22,0x2f,0x00,0x08
        jmp 0x400a652a
        .globl core_machine_range_copy,core_stock_range_copy
core_machine_range_copy:
        STOCK_FIRST range_copy,4,0x1f9
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 13
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_range_copy:
        .byte 0x2f,0x02,0x20,0x2f,0x00,0x08,0x0c,0x80,0x00,0x00,0x01,0xf9
        jmp 0x400a62d4
        .globl core_machine_valid_owner,core_stock_valid_owner
core_machine_valid_owner:
        STOCK_FIRST valid_owner,4,0x1f9
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 14
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_valid_owner:
        .byte 0x20,0x2f,0x00,0x04,0x0c,0x80,0x00,0x00,0x01,0xf8
        jmp 0x400a64e8
        .globl core_machine_source_layout,core_stock_source_layout
core_machine_source_layout:
        STOCK_FIRST source_layout,4,0
        | SourcePageView resolves controls directly through this getter,
        | bypassing the base ParameterPageView navigation method.
        move.l (%sp),%d0
        cmpi.l #0x40044cc6,%d0
        beq.s 7f
        cmpi.l #0x40044ce6,%d0
        bne.s 8f
7:      lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        move.l %a2,-(%sp)
        jsr core_machine_view_layout
        addq.l #4,%sp
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        tst.l %d0
        bne.s 9f
8:
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 15
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
9:      rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_source_layout:
        .byte 0x72,0x2c,0x20,0x2f,0x00,0x04,0xb2,0x80
        jmp 0x4007b9f2
        .globl core_machine_cached_parameter,core_stock_cached_parameter
core_machine_cached_parameter:
| The randomize draw (40014954) asks machine-randomize.c first: a bank
| control, or an ID a provider answers query 31 for, gets the generic
| randomizer; every other caller and ID keeps its own or its alias's entry.
        move.l (%sp),%d1
        cmpi.l #0x40014970,%d1
        bne.s 2f
        tst.l core_machine_active
        beq.s 2f
        move.l 4(%sp),-(%sp)
        jsr core_machine_randomize_entry
        addq.l #4,%sp
        tst.l %d0
        beq.s 2f
        rts
2:
        STOCK_FIRST_SEND cached_parameter,4
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        move.l 24(%sp),%d0
        moveq #16,%d1
        cmpi.l #0x4003b972,%d0
        bne.s 9f
        moveq #23,%d1
9:      move.l %d1,-(%sp)
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_cached_parameter:
        .byte 0x22,0x2f,0x00,0x04,0x0c,0x81,0x00,0x00,0x01,0xf9
        jmp 0x4007b9bc
        .globl core_machine_format_value,core_stock_format_value
core_machine_format_value:
        STOCK_FIRST_SEND format_value,4
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 17
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_format_value:
        .byte 0x22,0x2f,0x00,0x04,0x0c,0x81,0x00,0x00,0x01,0xf9
        jmp 0x4007bb74
        .globl core_machine_parameter_label,core_stock_parameter_label
core_machine_parameter_label:
        STOCK_FIRST_SEND parameter_label,8
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 18
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_parameter_label:
        .byte 0x22,0x2f,0x00,0x08,0x0c,0x81,0x00,0x00,0x01,0xf9
        jmp 0x40012b2e
        .globl core_machine_synth_parameter,core_stock_synth_parameter
core_machine_synth_parameter:
        STOCK_FIRST synth_parameter,4,0x1f9
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 19
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        | 40038dae receives an already resolved engine ID. Its native SYN
        | branch would replace it from source_layout(type), i.e. SYN page 1.
        | Only a registered parameter reaches here; skip that second lookup
        | at this caller, including explicit compound edits. Other SYN tests
        | retain their ordinary result and stock/unregistered IDs fall through.
        move.l 12(%sp),%d1
        cmpi.l #0x40038dc2,%d1
        bne.s 8f
        moveq #0,%d0
8:
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_synth_parameter:
        .byte 0x22,0x2f,0x00,0x04,0x0c,0x81,0x00,0x00,0x01,0xf9
        jmp 0x400a6182
        .globl core_machine_parameter_long_label,core_stock_parameter_long_label
core_machine_parameter_long_label:
        STOCK_FIRST_SEND parameter_long_label,8
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 20
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_parameter_long_label:
        .byte 0x2f,0x02,0x70,0x5c,0x20,0x6f,0x00,0x08
        jmp 0x40011a8a
        .globl core_machine_modulation_flags,core_stock_modulation_flags
core_machine_modulation_flags:
        STOCK_FIRST modulation_flags,4,0x1f9
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 21
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_modulation_flags:
        .byte 0x22,0x2f,0x00,0x04,0x0c,0x81,0x00,0x00,0x01,0xf9
        jmp 0x400a6674
        .globl core_machine_menu_rank,core_stock_menu_rank
core_machine_menu_rank:
        STOCK_FIRST menu_rank,4,0
        lea -12(%sp),%sp
        move.l %a0,(%sp)
        move.l %a1,4(%sp)
        pea 8(%sp)
        move.l 4(%sp),-(%sp)
        lea 24(%sp),%a0
        move.l %a0,-(%sp)
        pea 22
        jsr core_machine_query
        lea 16(%sp),%sp
        tst.l %d0
        beq.s 1f
        move.l 8(%sp),%d0
        movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
        rts
1:      movea.l (%sp),%a0
        movea.l 4(%sp),%a1
        lea 12(%sp),%sp
core_stock_menu_rank:
        .byte 0x2f,0x0a,0x20,0x6f,0x00,0x08,0x20,0x08
        jmp 0x40178b1a
