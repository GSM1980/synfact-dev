| SPDX-License-Identifier: GPL-2.0-or-later
| MAIN C ABI, caller holds native exclusion. Integer-only: EMAC untouched.
.text
.globl arp_keyboard_sync
arp_keyboard_sync:
 movea.l 4(%sp),%a0
 movea.l 8(%sp),%a1
 move.l (%a1),%d0
 cmp.l (%a0),%d0
 bne.s arp_keyboard_changed
 move.l 4(%a1),%d0
 cmp.l 4(%a0),%d0
 bne.s arp_keyboard_changed
 move.l 8(%a1),%d0
 cmp.l 8(%a0),%d0
 bne.s arp_keyboard_changed
 move.l 12(%a1),%d0
 cmp.l 12(%a0),%d0
 bne.s arp_keyboard_changed
 move.l 144(%a0),%d0
 rts
arp_keyboard_changed:
 jmp arp_keyboard_sync_changed
