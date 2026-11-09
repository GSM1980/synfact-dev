| SPDX-License-Identifier: GPL-2.0-or-later
| The base-width filter's low-pass lookup, bounded to the stock table.
|
| Stock AUDIO 40002320 filters the eight digital tracks after the scheduler.
| Per track and block it reads BASE and WDTH from the parameter row and takes
| two Q31 poles from the table at 40040060: T[BASE>>6] for the high-pass and
| T[(BASE+WDTH)>>6] for the low-pass. The stock table has 508 poles and
| zeros up to index 1023 (40041060): a zero pole leaves the low-pass open,
| which is what every BASE+WDTH of 127 or more selects. The companion's
| runtime window [40040860,40041050) is that zero tail (indices 512..1019),
| so the stock lookup would read Synfact's code as poles.
|
| Both lookups of the low-pass pole (40002364 for the pair's first track,
| 400023e8 for the second) replace `add.l d1,d0; asr.l #6,d0;
| lea (a0,d0.l*4),a0` with a call here. The index is bounded to 508, the
| first zero of the stock table, which is below the window: every index the
| stock would read gives the pole it gave on the stock OS. The high-pass
| index BASE>>6 never exceeds 511 (40040850..4004085c, also outside the
| window) and keeps its stock lookup. a0 is the table, d0 BASE, d1 WDTH;
| only d0 and a0 change, as in the stock instructions. The condition codes
| are not live at either site. sdk/syntakt/audio.py checks the stock zeros.
        .section .text.upper,"ax"
        .globl audio_bw_index
        .type audio_bw_index,@function
audio_bw_index:
        add.l %d1,%d0
        asr.l #6,%d0
        cmpi.l #508,%d0
        ble.s 1f
        moveq #127,%d0
        lsl.l #2,%d0
1:      lea (%a0,%d0.l*4),%a0
        rts
        .size audio_bw_index,.-audio_bw_index
