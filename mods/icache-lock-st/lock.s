| SPDX-License-Identifier: GPL-2.0-or-later
| void icache_lock_apply(const unsigned short tags[256])
|
| Half-locks the MAIN instruction cache (MCF5441x RM, 6.4.5 "Cache Locking"):
| invalidate it, touch two lines per set so that they fill the invalid ways
| 0 and 1, then set CACR[IHLCK] so that new lines only go to ways 2 and 3.
| CACR is write-only, so both writes rebuild the native value: the boot
| writes a50ce100 (data, instruction and branch caches on and invalidated);
| the mods' code-change sequence writes a40ce100 (instruction and branch
| invalidate, data cache kept). That write also unlocks the cache, which is
| why this runs again from the tick. Interrupts stay masked while ways 0-1
| fill, so no handler takes them. This code's own few lines, fetched after
| the invalidate, may take way 0 of their sets; the line meant for it then
| goes to an unlocked way. Locking changes timing only, never what runs:
| locked lines are invalidated like any other by the a40ce100 write that
| follows every code change.
        .text
        .globl icache_lock_apply
icache_lock_apply:
        movea.l 4(%sp),%a1
        lea -12(%sp),%sp
        movem.l %d2-%d4,(%sp)
        moveq #11,%d3
        move.l #0x40000000,%d4
        move.w %sr,%d2
        move.w #0x2700,%sr
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
        move.w %d2,%sr
        movem.l (%sp),%d2-%d4
        lea 12(%sp),%sp
        rts
