| SPDX-License-Identifier: GPL-2.0-or-later
| The chorus's state block: byte offsets, the same as struct chorus_state
| (chorus.h, which checks each one). In MAIN SRAM at ST_SRAM_CHORUS, with the
| per-block code (hot.s) right after it; else in the bank's DDR, right before
| that code's original. hot.s reaches it PC-relative, kernels.s through A6.
| The block's buffers are in the bank's DDR (dsp.c: chorus_in, chorus_up,
| chorus_wet), as 1.1's: written before they are read in each block, they
| cost less there than the code would.
        .equ S_TGT,0            | 12 track send gains, from ev_track_send
        .equ S_CUR,48           | 12 gains of the last mixed block
        .equ S_TMASK,96         | tracks with a target gain
        .equ S_CMASK,100        | tracks with a current gain
        .equ S_IDLE,104
        .equ S_QUIET,108
        .equ S_DIRTY,112        | records to rebuild: 2 after a change, then 1
        .equ S_RAMP,116         | the records' gains move this block
        .equ S_NGRP,120         | groups of four tracks in the records
        .equ S_RETG,124         | DEL, REV, VOL return gains
        .equ S_DES,136          | and their targets
        .equ S_HP,148           | HPF x', y'
        .equ S_HPC,156          | HPF coefficient
        .equ S_PHASE,160
        .equ S_WIDTH,164
        .equ S_DRAW,168         | depth_raw
        .equ S_AMP,172
        .equ S_PTRI,176         | previous_triangle[2]
        .equ S_POLE,184         | pole_state[2]
        .equ S_PDEL,192         | previous_delay[2]
        .equ S_POS,200          | position[2][2]
        .equ S_WPOS,216
        .equ S_INIT,220
        .equ S_FX,224           | the FX record decoded last
        .equ S_KEY,228          | its eight words then (u16)
        .equ S_VAL,244          | the seven settings (u16)
        .equ S_END,260          | scratch: the mix's end pointer
        .equ S_POST,264         | the wet: 0 rendered, 1 at ev_main_mix with VOL, 2 not needed
        .equ S_REC,268          | 3 records of four tracks
        .equ R_GAIN,0
        .equ R_PTR,16           | PCM pointers (the multi-group mix moves them, then back)
        .equ R_STEP,32
        .equ R_ENTS,48          | entry offsets: the first group skips its
        .equ R_ENTR,52          | empty slots (stable, ramp kernel)
        .equ R_SIZE,56
        .equ S_TAB,436          | kernel addresses (kept by the reset)
        .equ K_MIXS,0
        .equ K_MIXR,4
        .equ K_DEC,8
        .equ K_DELAY,12
        .equ K_UP,16
        .equ K_RET,20
        .equ K_BUILD,24
        .equ K_MOD,28
        .equ STATE_SIZE,468
        .equ RING_WORDS,304     | 256 + a mirror of its first 48
