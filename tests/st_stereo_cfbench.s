| Test-only helpers: preserve/check the complete C ABI and physical EMAC state.
.text
.globl st_probe_call,st_probe_mac_get,st_probe_mac_set
st_probe_call:
 lea -44(%sp),%sp
 movem.l %d2-%d7/%a2-%a6,(%sp)
 movea.l 48(%sp),%a0
 .rept 5
 move.l 68(%sp),-(%sp)
 .endr
 move.l #0x12340002,%d2
 move.l #0x12340003,%d3
 move.l #0x12340004,%d4
 move.l #0x12340005,%d5
 move.l #0x12340006,%d6
 move.l #0x12340007,%d7
 movea.l #0x12340012,%a2
 movea.l #0x12340013,%a3
 movea.l #0x12340014,%a4
 movea.l #0x12340015,%a5
 movea.l #0x12340016,%a6
 jsr (%a0)
 lea 20(%sp),%sp
 moveq #1,%d0
 cmpi.l #0x12340002,%d2
 bne.w probe_call_done
 cmpi.l #0x12340003,%d3
 bne.w probe_call_done
 cmpi.l #0x12340004,%d4
 bne.w probe_call_done
 cmpi.l #0x12340005,%d5
 bne.w probe_call_done
 cmpi.l #0x12340006,%d6
 bne.w probe_call_done
 cmpi.l #0x12340007,%d7
 bne.w probe_call_done
 cmpa.l #0x12340012,%a2
 bne.w probe_call_done
 cmpa.l #0x12340013,%a3
 bne.w probe_call_done
 cmpa.l #0x12340014,%a4
 bne.w probe_call_done
 cmpa.l #0x12340015,%a5
 bne.w probe_call_done
 cmpa.l #0x12340016,%a6
 bne.w probe_call_done
 moveq #0,%d0
probe_call_done:
 movem.l (%sp),%d2-%d7/%a2-%a6
 lea 44(%sp),%sp
 rts
st_probe_mac_get:
 movea.l 4(%sp),%a0
 move.l %macsr,%d1
 move.l %d1,(%a0)+
 moveq #0,%d0
 move.l %d0,%macsr
 move.l %acc0,%d0
 move.l %d0,(%a0)+
 move.l %acc1,%d0
 move.l %d0,(%a0)+
 move.l %acc2,%d0
 move.l %d0,(%a0)+
 move.l %acc3,%d0
 move.l %d0,(%a0)+
 move.l %accext01,%d0
 move.l %d0,(%a0)+
 move.l %accext23,%d0
 move.l %d0,(%a0)+
 move.l %mask,%d0
 move.l %d0,(%a0)
 move.l %d1,%macsr
 rts
st_probe_mac_set:
 movea.l 4(%sp),%a0
 move.l (%a0)+,%d1
 moveq #0,%d0
 move.l %d0,%macsr
 move.l (%a0)+,%d0
 move.l %d0,%acc0
 move.l (%a0)+,%d0
 move.l %d0,%acc1
 move.l (%a0)+,%d0
 move.l %d0,%acc2
 move.l (%a0)+,%d0
 move.l %d0,%acc3
 move.l (%a0)+,%d0
 move.l %d0,%accext01
 move.l (%a0)+,%d0
 move.l %d0,%accext23
 move.l (%a0),%d0
 move.l %d0,%mask
 move.l %d1,%macsr
 rts
