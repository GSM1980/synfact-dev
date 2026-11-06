| SPDX-License-Identifier: GPL-2.0-or-later
| Machine parameter list 400146bc (hidden A0 vector, type): a bank machine
| type gets its own engine IDs (machine-randomize.c). UI task only.
        .text
        .globl core_machine_parameter_list,core_stock_parameter_list
core_machine_parameter_list:
        tst.l core_machine_active
        beq.w 1f
        move.l 4(%sp),%d0
        subi.l #64,%d0
        cmpi.l #16,%d0
        bcc.w 1f
        move.l 4(%sp),-(%sp)
        move.l %a0,-(%sp)
        jsr core_machine_randomize_list
        addq.l #8,%sp
        movea.l %d0,%a0
        rts
1:      clr.l core_machine_randomize_view
core_stock_parameter_list:
        .byte 0x4f,0xef,0xff,0xec,0x48,0xd7,0x1c,0x04
        jmp 0x400146c4

| SYN view randomize 40044972 (view, page): the list above takes the view's
| current page only.
        .globl core_machine_syn_randomize
core_machine_syn_randomize:
        move.l 4(%sp),%d0
        move.l %d0,core_machine_randomize_view
        .byte 0x2f,0x0a,0x2f,0x02,0x24,0x6f,0x00,0x0c
        jmp 0x4004497a

| Kit-owned check 400a61fa (id): owners 0x31..0x36 resolve to the Kit's
| model in 4001c422. An ID from 0x1f9 takes its owner from 400a5c6c
| (core_machine_owner), so a module's Kit control (the chorus, owner 0x31)
| reaches the Kit's setter; machines (owner = type) stay outside.
        .globl core_machine_kit_owner,core_stock_kit_owner
core_machine_kit_owner:
        tst.l core_machine_active
        beq.w core_stock_kit_owner
        move.l 4(%sp),%d0
        cmpi.l #0x1f9,%d0
        bcs.w core_stock_kit_owner
        move.l %d0,-(%sp)
        jsr 0x400a5c6c
        addq.l #4,%sp
        subi.l #0x31,%d0
        moveq #5,%d1
        cmp.l %d0,%d1
        scc %d0
        mvs.b %d0,%d0
        neg.l %d0
        rts
core_stock_kit_owner:
        .byte 0x22,0x2f,0x00,0x04,0x0c,0x81,0x00,0x00,0x01,0xf9
        jmp 0x400a6204
