| SPDX-License-Identifier: GPL-2.0-or-later
| C ABI probe for the exact register ABI used by the five step stubs.
.text
.globl st_playhead_probe,st_playhead_identity,st_playhead_ui_probe
st_playhead_probe:
        move.l %d7,-(%sp)
        movea.l 8(%sp),%a1             | mirror or peek entry
        move.l 12(%sp),%d7             | track
        move.l 16(%sp),%d1             | native count
        movea.l 20(%sp),%a0            | pattern
        jsr (%a1)
        move.l %d1,%d0
        move.l (%sp)+,%d7
        rts
st_playhead_identity:
        rts
st_playhead_ui_probe:
        lea -20(%sp),%sp
        movem.l %d4/%d7/%a4-%a6,(%sp)
        movea.l 24(%sp),%a0
        move.l 28(%sp),%d0
        move.l 32(%sp),%d4
        lea 0x43bd83fc,%a5
        adda.l %d0,%a5
        lea 0x43bd8416,%a6
        adda.l %d0,%a6
        lea 0x444e337d,%a4
        adda.l %d0,%a4
        lea 0x444e3370,%a1
        move.l #0x444e33a0,%d7
        jsr (%a0)
        mvz.b -1(%a4),%d0
        movem.l (%sp),%d4/%d7/%a4-%a6
        lea 20(%sp),%sp
        rts
