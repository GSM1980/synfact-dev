| SPDX-License-Identifier: GPL-2.0-or-later
| Internal routing uses existing flag bytes, bits 0..6 = subscriber indices.
| Bit 7 means legacy full-list traversal (also the pre-init fail-open state).
| TYPE track bytes 18/19 keep their original layout and semantics.
| Entirely resident: never put application-time functions in .boot.
.section .run,"ax"
.globl core_native_route_fill,core_event_native
core_native_route_fill:
 lea -36(%sp),%sp
 movem.l %d2-%d7/%a2-%a4,(%sp)
 movea.l 40(%sp),%a2
 movea.l 44(%sp),%a3
 movea.l %a2,%a0
 moveq #0,%d0
 moveq #5,%d1
native_route_clear:
 move.l %d0,(%a0)+
 subq.l #1,%d1
 bpl.s native_route_clear
 moveq #0,%d3
 moveq #1,%d4
 moveq #0,%d5
 moveq #0,%d6
 suba.l %a1,%a1
native_route_callback:
 move.l (%a3)+,%d0
 beq.w native_route_finish
 moveq #-1,%d2
 moveq #0,%d1
 lea core_native_masks,%a4
 moveq #3,%d7
native_route_lookup:
 cmp.l (%a4),%d0
 beq.s native_route_found
 lea 12(%a4),%a4
 subq.l #1,%d7
 bpl.s native_route_lookup
 bra.s native_route_type
native_route_found:
 move.l 4(%a4),%d2
 move.l 8(%a4),%d1
native_route_type:
 btst #4,%d2
 beq.s native_route_page
 move.l %d2,%d0
 moveq #24,%d7
 lsr.l %d7,%d0
 beq.s native_route_all_types
 cmpi.l #255,%d0
 bne.s native_route_some_types
native_route_all_types:
 move.l #256,%d0
native_route_some_types:
 or.l %d0,%d3
native_route_page:
 move.l %d2,%d0
 andi.l #0x600,%d0
 beq.s native_route_bytes
 tst.l %d1
 beq.s native_route_any_view
 tst.l %d5
 beq.s native_route_save_view
 cmp.l %d1,%d5
 beq.s native_route_save_view
native_route_any_view:
 moveq #1,%d6
native_route_save_view:
 move.l %d1,%d5
native_route_bytes:
 move.l %a1,%d0
 or.l %d4,%d0
 movea.l %d0,%a1
 movea.l %a2,%a0
 moveq #23,%d7
native_route_byte:
 lsr.l #1,%d2
 bcc.s native_route_next_byte
 move.b (%a0),%d0
 or.l %d4,%d0
 move.b %d0,(%a0)
native_route_next_byte:
 addq.l #1,%a0
 subq.l #1,%d7
 bpl.s native_route_byte
 btst #7,%d4
 bne.w native_route_callback
 add.l %d4,%d4
 bra.w native_route_callback
native_route_finish:
 | A route containing the entire list uses the cheaper unfiltered loop.
 | Build that decision once, not on each native event. No extra state.
 move.l %a1,%d0
 beq.s native_route_type_bytes
 movea.l %a2,%a0
 moveq #23,%d7
native_route_full_byte:
 cmp.b (%a0),%d0
 bne.s native_route_full_next
 move.b #128,(%a0)
native_route_full_next:
 addq.l #1,%a0
 subq.l #1,%d7
 bpl.s native_route_full_byte
native_route_type_bytes:
 move.b %d3,18(%a2)
 lsr.l #8,%d3
 move.b %d3,19(%a2)
 move.l %d5,%d0
 tst.l %d6
 beq.s native_route_return
 moveq #0,%d0
native_route_return:
 movem.l (%sp),%d2-%d7/%a2-%a4
 lea 36(%sp),%sp
 rts
.size core_native_route_fill,.-core_native_route_fill

| Snapshot the route and list before any callback (nested updates take effect
| on the next dispatch). Negative route bytes use the original unfiltered
| traversal for full routes and arbitrarily long fallback collections.
core_event_native:
 | Keep the original 28-byte frame. POLY's live-origin census observes its
 | parent stack; an extra spill would lengthen that scan on every key.
 | Full routes keep OP in D2. Filtered routes reuse it for the snapshot and
 | reload only OP from the unchanged caller arguments before each callback.
 lea -28(%sp),%sp
 movem.l %d2-%d4/%a2,12(%sp)
 movem.l 32(%sp),%d2-%d4
 movea.l core_native_list,%a2
 lea core_native_interest,%a0
 mvs.b (%a0,%d2.l),%d0
 bmi.s native_dispatch_open
 cmpi.l #18,%d2
 beq.s native_dispatch_open
 cmpi.l #19,%d2
 beq.s native_dispatch_open
 move.l %d0,%d2
 bra.s native_dispatch_loop
native_dispatch_open:
 move.l (%a2)+,%d0
 beq.s native_dispatch_none
 movea.l %d0,%a0
 movem.l %d2-%d4,(%sp)
 jsr (%a0)
 tst.l %d0
 beq.s native_dispatch_open
 bra.s native_dispatch_consumed
native_dispatch_loop:
 tst.l %d2
 beq.s native_dispatch_none
 move.l (%a2)+,%d0
 beq.s native_dispatch_none
 lsr.l #1,%d2
 bcc.s native_dispatch_loop
 movea.l %d0,%a0
 move.l 32(%sp),(%sp)
 movem.l %d3-%d4,4(%sp)
 jsr (%a0)
 tst.l %d0
 beq.s native_dispatch_loop
native_dispatch_consumed:
 moveq #1,%d0
 bra.s native_dispatch_return
native_dispatch_none:
 moveq #0,%d0
native_dispatch_return:
 movem.l 12(%sp),%d2-%d4/%a2
 lea 28(%sp),%sp
 rts
.size core_event_native,.-core_event_native

| Public native service: do not build a C frame for an uninterested operation.
| The original dispatcher and its 28-byte callback frame remain unchanged.
| The interested fast path needs only three outgoing words and a result word;
| the profiled path retains the entry buffer/timer across arbitrary callbacks.
| No EMAC state, native operation order or callback snapshot policy is changed.
.globl core_native
core_native:
 move.l core_native_profile_buffer,%d0
 bne.s native_service_profiled
 move.l 4(%sp),%d0
 cmpi.l #22,%d0
 bcc.s native_service_stock
 lea core_native_interest,%a0
 tst.b (%a0,%d0.l)
 beq.s native_service_stock
 lea -16(%sp),%sp
 move.l %d0,(%sp)
 move.l 24(%sp),4(%sp)
 lea 12(%sp),%a0
 move.l %a0,8(%sp)
 clr.l 12(%sp)
 jsr core_event_native
 tst.l %d0
 beq.s native_service_declined
 move.l 12(%sp),%d0
 lea 16(%sp),%sp
 rts
native_service_declined:
 lea 16(%sp),%sp
native_service_stock:
 jmp core_native_original
native_service_profiled:
 lea -32(%sp),%sp
 movem.l %d2-%d4/%a2,16(%sp)
 movem.l 36(%sp),%d2-%d3
 movea.l %d0,%a2
 move.l 0xfc07000c,%d4
 clr.l 12(%sp)
 cmpi.l #22,%d2
 bcc.s native_service_profile_invalid
 lea core_native_interest,%a0
 tst.b (%a0,%d2.l)
 beq.s native_service_profile_stock
 movem.l %d2-%d3,(%sp)
 lea 12(%sp),%a0
 move.l %a0,8(%sp)
 jsr core_event_native
 tst.l %d0
 bne.s native_service_profile_done
native_service_profile_stock:
 movem.l %d2-%d3,(%sp)
 jsr core_native_original
 move.l %d0,12(%sp)
native_service_profile_done:
 cmpi.l #18,%d2
 bcc.s native_service_profile_return
 move.l 0xfc07000c,%d0
 sub.l %d4,%d0
 lea (%a2,%d2.l*4),%a0
 add.l %d0,(%a0)
 addq.l #1,72(%a0)
native_service_profile_return:
 move.l 12(%sp),%d0
 movem.l 16(%sp),%d2-%d4/%a2
 lea 32(%sp),%sp
 rts
native_service_profile_invalid:
 | Invalid operations still take the entry timer snapshot, but cannot index
 | the legacy buffer. Restore the caller's arguments for the stock tail call.
 movem.l 16(%sp),%d2-%d4/%a2
 lea 32(%sp),%sp
 bra.w native_service_stock
.size core_native,.-core_native
