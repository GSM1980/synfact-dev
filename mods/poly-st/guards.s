| Native loop starts with both oscillator states untouched. D0/D1 are dead
| in the displaced prefix. Preserve A0 and SP; the next native block sets D7.
.text
.globl poly_native_lfo_guard,poly_native_dispatch,poly_render_dispatch
.globl poly_mod_dispatch,poly_mod_first_guard,poly_mod_second_guard
poly_mod_dispatch:
 lea poly_fast_owners,%a0
 move.l (%a0),%d0
 cmpi.l #0x00010203,%d0
 bne.s mod_grouped
 move.l 4(%a0),%d0
 cmpi.l #0x04050607,%d0
 bne.s mod_grouped
 | The resident body is untouched. Only its prologue needs replaying.
 lea -60(%sp),%sp
 movem.l %d2-%d7/%a2-%a6,(%sp)
 jmp 0x40097b04
mod_grouped:
 jmp poly_mod_shared
poly_mod_first_guard:
 move.l %a4,%d0
 subi.l #0x80004670,%d0
 lsr.l #1,%d0
 cmpi.l #8,%d0
 bhs.s mod_first_run
 lea poly_fast_owners,%a0
 moveq #0,%d1
 move.b (%a0,%d0.l),%d1
 cmp.l %d0,%d1
 bhs.s mod_first_run
 addq.l #2,%a4
 addi.l #124,%d2
 addi.l #142,%d4
 cmpa.l #0x80004688,%a4
 bne.w poly_mod_first
 jmp poly_mod_after_first
mod_first_run:
 move.l %d2,-(%sp)
 mvs.w (%a4)+,%d0
 addi.l #124,%d2
 jmp poly_mod_first_continue
poly_mod_second_guard:
 move.l %d2,%d0
 lsr.l #1,%d0
 cmpi.l #8,%d0
 bhs.s mod_second_run
 lea poly_fast_owners,%a0
 moveq #0,%d1
 move.b (%a0,%d0.l),%d1
 cmp.l %d0,%d1
 bhs.s mod_second_run
 addq.l #2,%d2
 addi.l #142,%d3
 lea 124(%a2),%a2
 moveq #26,%d0
 cmp.l %d2,%d0
 bne.w poly_mod_second
 jmp poly_mod_return
mod_second_run:
 pea 1892(%a2)
 mvs.w (%a6,%d2.l),%d4
 move.l %d4,-(%sp)
 jmp poly_mod_second_continue
poly_native_dispatch:
 lea poly_fast_owners,%a0
 move.l (%a0),%d0
 cmpi.l #0x00010203,%d0
 bne.s native_grouped
 move.l 4(%a0),%d0
 cmpi.l #0x04050607,%d0
 bne.s native_grouped
 jmp poly_native_original
native_grouped:
 jmp poly_native_shared
poly_render_dispatch:
 lea poly_fast_owners,%a0
 move.l (%a0),%d0
 cmpi.l #0x00010203,%d0
 bne.s third_grouped
 move.l 4(%a0),%d0
 cmpi.l #0x04050607,%d0
 bne.s third_grouped
 lea -96(%sp),%sp
 movem.l %d2-%d7/%a2-%a6,(%sp)
 jmp poly_original_render_continue
third_grouped:
 jmp poly_engine_render
poly_native_lfo_guard:
 move.l %a0,-(%sp)
 move.l 60(%sp),%d0
 cmpi.l #8,%d0
 bhs.s native_run
 lea poly_fast_owners,%a0
 moveq #0,%d1
 move.b (%a0,%d0.l),%d1
 cmp.l %d0,%d1
 bhs.s native_run
 movea.l (%sp)+,%a0
 jmp poly_native_next
native_run:
 movea.l (%sp)+,%a0
 moveq #1,%d0
 moveq #1,%d1
 move.l 76(%sp),%d2
 jmp poly_native_continue
