| SPDX-License-Identifier: GPL-2.0-or-later
| Syntakt's nontrivial std::string return slot is A0, not a C argument.
        .text
        .globl si_label
si_label:
        link %a6,#-4
        move.l %d2,-(%sp)
        move.l %a0,%d2
        jsr si_label_text
        pea -1(%a6)
        move.l %d0,-(%sp)
        move.l %d2,-(%sp)
        jsr fw_string_ctor
        lea 12(%sp),%sp
        move.l %d2,%d0
        move.l -8(%a6),%d2
        unlk %a6
        rts
