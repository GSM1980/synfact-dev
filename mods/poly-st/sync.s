| SPDX-License-Identifier: GPL-2.0-or-later
| Existing cache and private immediate command, C ABI. No EMAC instructions.
.text
.globl poly_sync
poly_sync:
 movea.l 0x43bd7430,%a0
 tst.l %a0
 beq.w poly_sync_submit
 cmpa.l poly_sync_sequence_cache,%a0
 bne.w poly_sync_submit
 movea.l 0x43bd743c,%a1
 move.l #0xffff,%d0
 tst.l %a1
 beq.s poly_sync_midi_ready
 mvz.w 0x1550(%a1),%d0
poly_sync_midi_ready:
 cmp.l poly_sync_midi_cache,%d0
 bne.w poly_sync_submit
 adda.l #0x21381,%a0
 lea poly_sync_record_cache,%a1
 | Native record is odd-addressed: every read stays a byte access.
 .rept 7
 move.b (%a0)+,%d0
 cmp.b (%a1)+,%d0
 bne.s poly_sync_submit
 .endr
 moveq #1,%d0
 rts
poly_sync_submit:
 | Six command words plus one argument slot, matching the native exclusion
 | boundary. Only the slow path allocates a stack frame; no pool allocation.
 lea -28(%sp),%sp
 move.l #0x504f4c59,%d0
 move.l %d0,4(%sp)
 move.l #0x53594e43,%d0
 move.l %d0,8(%sp)
 clr.l 12(%sp)
 clr.l 16(%sp)
 clr.l 20(%sp)
 clr.l 24(%sp)
 lea 4(%sp),%a0
 move.l %a0,(%sp)
 jsr native_immediate
 move.l 24(%sp),%d0
 lea 28(%sp),%sp
 rts
.globl poly_sync_end
poly_sync_end:
.size poly_sync,poly_sync_end-poly_sync
