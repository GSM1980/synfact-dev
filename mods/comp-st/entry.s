| SPDX-License-Identifier: GPL-2.0-or-later
        .text
        .globl comp_lock,comp_unlock
comp_lock:
        moveq #0,%d0
        move.w %sr,%d0
        move.w #0x2700,%sr
        rts
comp_unlock:
        move.l 4(%sp),%d0
        move.w %d0,%sr
        rts
        .globl comp_sync
comp_sync:
        nop
        move.l #0xa40ce100,%d0
        movec %d0,%cacr
        rts
