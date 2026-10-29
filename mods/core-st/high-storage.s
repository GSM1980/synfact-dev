| SPDX-License-Identifier: GPL-2.0-or-later
        .section .run,"ax"
        .globl core_storage_boot,core_high_validate
core_storage_boot:
        jsr core_hzfs_startup
        jmp core_machine_boot
| Native std::function BoolResult invoke(fn*, header*), hidden A0 result.
core_high_validate:
        move.l %a2,-(%sp)
        movea.l %a0,%a2
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        move.l %a2,-(%sp)
        jsr core_high_check_header
        lea 12(%sp),%sp
        move.l %a2,%d0
        movea.l (%sp)+,%a2
        rts
