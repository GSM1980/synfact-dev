| SPDX-License-Identifier: GPL-2.0-or-later
| Generic application hooks; no machine-specific hooks are carried in SMZ4.
        .section .run,"ax"
        .globl core_pool_blob,core_pool_count,core_pool_map
core_pool_blob:
        jsr 0x400822bc
        jsr core_pool_prepare
        jmp 0x40082e24
core_pool_count:
        move.l core_pool_boot_pages,%d0
        add.l core_machine_pages,%d0
        beq.s 1f
        move.l 0x4d84f2c1,%d2
        addi.l #2047,%d2
        lsr.l #8,%d2
        lsr.l #3,%d2
        add.l %d0,%d2
        lsl.l #8,%d2
        lsl.l #3,%d2
        rts
1:      move.l 0x4d84f2c1,%d2
        rts
core_pool_map:
        lea -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l %d2,-(%sp)
        move.l 0x41bbe424,-(%sp)
        jsr core_sample_progress
        addq.l #8,%sp
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        move.l 0x4d84f2c1,%d1
        addi.l #2047,%d1
        lsr.l #8,%d1
        lsr.l #3,%d1
        add.l core_machine_pages,%d1
        move.l 0x41bbe424,%d0
        sub.l %d1,%d0
        bcs.s 1f
        tst.l core_pool_boot_pages
        beq.s 1f
        lea -20(%sp),%sp
        movem.l %a0-%a2,(%sp)
        lea 12(%sp),%a0
        move.l %a0,-(%sp)
        move.l %d0,-(%sp)
        jsr core_pool_boot_page
        addq.l #8,%sp
        move.l 12(%sp),%d1
        move.w %d1,0x1000000c
        subi.l #0x4d84f2c5,%d0
        movem.l (%sp),%a0-%a2
        lea 20(%sp),%sp
        rts
| The native sender enters with D1=11 (2048-byte pages). Our pool boundary
| calculation replaces it with the stock page count. Restore it even when
| core_machine_pages is zero: that fallback shifts D0 by D1 directly.
1:      moveq #11,%d1
        jmp core_machine_page_map

        .globl core_machine_raw,core_machine_length
core_machine_raw:
        lea -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l %d0,-(%sp)
        jsr core_machine_present
        addq.l #4,%sp
        tst.l %d0
        beq.s 1f
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        move.l 0x40(%a1),0x1c(%a0)
        move.l 0x44(%a1),0x20(%a0)
        move.l 0x48(%a1),0x24(%a0)
        move.w 0x4e(%a1),0x2a(%a0)
        move.w 0x50(%a1),0x2c(%a0)
        jmp 0x400979f6
1:      movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        cmp.l %d0,%d1
        bne.s 2f
        move.w 0x42(%a1),0x1e(%a0)
        jmp 0x400979f0
2:      jmp 0x400979f6
core_machine_length:
        lea -16(%sp),%sp
        movem.l %d0-%d1/%a0-%a1,(%sp)
        move.l %d0,-(%sp)
        jsr core_machine_present
        addq.l #4,%sp
        tst.l %d0
        beq.s 1f
        movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        cmp.l %d0,%d0
        rts
1:      movem.l (%sp),%d0-%d1/%a0-%a1
        lea 16(%sp),%sp
        move.b #44,%d1
        cmp.l %d0,%d1
        rts

        .globl core_pool_initialize,core_stock_pool_initialize
core_pool_initialize:
        move.l 8(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_stock_pool_initialize
        addq.l #8,%sp
        move.l %d0,-(%sp)
        jsr core_pool_created
        move.l (%sp)+,%d0
        rts
core_stock_pool_initialize:
        .byte 0x4e,0x56,0xff,0xa8,0x48,0xd7,0x1c,0x3c
        jmp 0x4001d172

        .globl core_pool_new,core_pool_snapshot,core_pool_validate,core_pool_load,core_pool_save
core_pool_new:
        move.l 4(%sp),-(%sp)
        jsr pool_native_new
        move.l %d0,(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_pool_empty
        addq.l #4,%sp
        move.l (%sp)+,%d0
        rts
pool_native_new:
        lea -24(%sp),%sp
        movem.l %d2-%d4/%a2-%a4,(%sp)
        jmp 0x40018ab8
core_pool_snapshot:
        move.l 8(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr pool_native_snapshot
        addq.l #8,%sp
        move.l 8(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_pool_after_snapshot
        addq.l #8,%sp
        rts
pool_native_snapshot:
        movea.l 4(%sp),%a0
        move.l 8(%sp),%d0
        jmp 0x4019ffdc
core_pool_validate:
        move.l 4(%sp),-(%sp)
        jsr core_pool_before_validate
        addq.l #4,%sp
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        jsr pool_native_validate
        lea 12(%sp),%sp
        move.l %d0,-(%sp)
        move.l %d0,-(%sp)
        move.l 12(%sp),-(%sp)
        jsr core_pool_after_validate
        addq.l #8,%sp
        move.l (%sp)+,%d0
        rts
pool_native_validate:
        lea -44(%sp),%sp
        movem.l %d2-%d3/%a2-%a6,(%sp)
        jmp 0x400afa86
core_pool_load:
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        jsr pool_native_load
        lea 12(%sp),%sp
        move.l %d0,-(%sp)
        move.l %d0,-(%sp)
        move.l 16(%sp),-(%sp)
        jsr core_pool_after_load
        addq.l #8,%sp
        move.l (%sp)+,%d0
        rts
pool_native_load:
        lea -36(%sp),%sp
        movem.l %d2-%d5/%a2,(%sp)
        jmp 0x4001d706
core_pool_save:
        move.l 8(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_pool_before_save
        addq.l #8,%sp
        jmp 0x400c1c10

        .globl core_pool_sound_menu,core_pool_label
core_pool_sound_menu:
        move.l 4(%sp),-(%sp)
        jsr pool_native_menu
        addq.l #4,%sp
        move.l 4(%sp),-(%sp)
        jsr core_pool_menu
        addq.l #4,%sp
        rts
pool_native_menu:
        lea -168(%sp),%sp
        movem.l %d2-%d7/%a2-%a6,(%sp)
        jmp 0x40064920
core_pool_label:
        link %a6,#-4
        move.l %d2,-(%sp)
        move.l %a0,%d2
        pea -1(%a6)
        pea pool_label_text
        move.l %d2,-(%sp)
        jsr fw_string_ctor
        lea 12(%sp),%sp
        move.l %d2,%d0
        move.l -8(%a6),%d2
        unlk %a6
        rts
        .section .rodata,"a"
pool_label_text: .asciz "SAMPLE MANAGER"
        .balign 2
        .section .run,"ax"
        .globl core_pool_value
core_pool_value:
        move.l 8(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_pool_format
        addq.l #8,%sp
        tst.l %d0
        bne.s 1f
        jmp core_machine_format_value
1:      rts
