| SPDX-License-Identifier: GPL-2.0-or-later
| unsigned icache_lock_apply(const unsigned short tags[256])
|
| Half-locks the MAIN instruction cache (MCF5441x RM, 6.4.5 "Cache Locking"):
| invalidate it, touch two lines per set so that they fill the invalid ways
| 0 and 1, then set CACR[IHLCK] so that new lines only go to ways 2 and 3.
| CACR is write-only, so both writes rebuild the native value: the boot
| writes a50ce100 (data, instruction and branch caches on and invalidated);
| the mods' code-change sequence writes a40ce100 (instruction and branch
| invalidate, data cache kept). That write also unlocks the cache, which is
| why this runs again from the tick. Leave the incoming interrupt priority
| alone throughout the fill. Preemption can change which valid lines are
| retained. The next routine cache pass can improve the selection. Code
| updates still invalidate the cached instructions through the native policy.
|
| The native codec TX DMA uses 0x80000000..0x80000800, two halves of 32
| frames with 32 bytes per frame. It requests audio work at each boundary.
| Defer while render is pending, on the first frame, or on the last four.
| Sample the phase before invalidating anything. An IRQ during the fill can
| still run at its native priority. Return zero only when no fill was tried.
        .text
        .globl icache_lock_apply
icache_lock_apply:
        moveq #0,%d0
        move.w %sr,%d0
        andi.l #0x0700,%d0
        cmpi.l #0x0500,%d0              | the MAIN render interrupt is level 5
        bcc.w .Ldefer
        move.l 0xfc0456c0,%d0           | eDMA 54, codec TX source address
        subi.l #0x80000000,%d0
        cmpi.l #0x800,%d0
        bcc.w .Ldefer
        andi.l #0x3ff,%d0
        cmpi.l #0x20,%d0                | skip first frame after the half interrupt
        bcs.w .Ldefer
        cmpi.l #0x380,%d0               | four complete frames before it repeats
        bcc.w .Ldefer
        tst.l 0xfc04c010                | INTC1 forced MAIN render pending
        bmi.w .Ldefer
        movea.l 4(%sp),%a1
        lea -8(%sp),%sp
        movem.l %d3-%d4,(%sp)
        moveq #11,%d3
        move.l #0x40000000,%d4
        nop                             | drain the store buffer
        move.l #0xa40ce100,%d0          | native MAIN CACR + I/branch invalidate
        movec %d0,%cacr
        moveq #0,%d1                    | set << 4
1:      moveq #0,%d0
        move.w (%a1)+,%d0               | way 0
        lsl.l %d3,%d0
        add.l %d4,%d0
        add.l %d1,%d0
        movea.l %d0,%a0
        .word 0xf428                    | intouch (%a0)
        moveq #0,%d0
        move.w (%a1)+,%d0               | way 1
        lsl.l %d3,%d0
        add.l %d4,%d0
        add.l %d1,%d0
        movea.l %d0,%a0
        .word 0xf428                    | intouch (%a0)
        moveq #16,%d0
        add.l %d0,%d1
        cmpi.l #0x800,%d1
        bne.s 1b
        move.l #0xa408e800,%d0          | native MAIN CACR + IHLCK
        movec %d0,%cacr
        nop
        movem.l (%sp),%d3-%d4
        lea 8(%sp),%sp
        moveq #1,%d0
        rts
.Ldefer:
        moveq #0,%d0
        rts
