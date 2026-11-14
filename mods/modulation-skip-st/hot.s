| SPDX-License-Identifier: GPL-2.0-or-later
| Exact stock zero-depth projection, in the published address-list order.
| No EMAC, unclaimed SRAM, speculative read or write for an in-range value.
.text
.balign 16
.globl modulation_skip_project_all
modulation_skip_project_all:
 movea.l 4(%sp),%a0
 move.l 8(%sp),%d0
 tst.l %d0
 beq.s 4f
1:
 movea.l (%a0)+,%a1
 move.w (%a1),%d1
 bpl.s 3f
 btst #14,%d1
 bne.s 2f
 move.l #0x7fff,%d1
 move.w %d1,(%a1)
 bra.s 3f
2:
 clr.w (%a1)
3:
 subq.l #1,%d0
 bne.s 1b
4:
 rts
