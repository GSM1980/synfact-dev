| SPDX-License-Identifier: GPL-2.0-or-later
| Fixed-size raw copies. No ownership, allocation, metadata or EMAC changes.
| Since 1.7 no Sound record is copied (voice setup only publishes).
.text
.globl poly_copy_static_event
poly_copy_static_event:
 movea.l 4(%sp),%a0
 movea.l 8(%sp),%a1
 .rept 20
 move.l (%a1)+,(%a0)+
 .endr
 rts
.globl poly_copy_static_event_end
poly_copy_static_event_end:
.size poly_copy_static_event,poly_copy_static_event_end-poly_copy_static_event

| Same read-only return-PC census as poly_live_from_keys: 1024 overlapping
| pairs of native halfwords, first recognized origin wins. A0 points at the
| caller's captured frame, not at this leaf's argument/return-address slots.
.globl poly_scan_key_origin
poly_scan_key_origin:
 movea.l 4(%sp),%a0
 move.l #1024,%d1
poly_scan_key_origin_next:
 move.w (%a0)+,%d0
 swap %d0
 move.w (%a0),%d0
 cmpi.l #0x4002ed0a,%d0
 beq.s poly_scan_key_press
 cmpi.l #0x401055cc,%d0
 beq.s poly_scan_key_press
 cmpi.l #0x4002d602,%d0
 beq.s poly_scan_key_release
 cmpi.l #0x40105488,%d0
 beq.s poly_scan_key_release
 subq.l #1,%d1
 bne.s poly_scan_key_origin_next
 moveq #0,%d0
 rts
poly_scan_key_press:
 moveq #2,%d0
 rts
poly_scan_key_release:
 moveq #1,%d0
 rts
.globl poly_scan_key_origin_end
poly_scan_key_origin_end:
.size poly_scan_key_origin,poly_scan_key_origin_end-poly_scan_key_origin
