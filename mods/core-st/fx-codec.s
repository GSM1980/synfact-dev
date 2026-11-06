| SPDX-License-Identifier: GPL-2.0-or-later
| The Kit's FX record codec (host API 13, core 1.2). Native encode
| 400a6d96(record, fx, flag) and decode 400a6d2e(fx, record) run unchanged;
| after each, ok or not, core_fx_codec_notify tells the parameter providers
| (query 28 encode {record, fx, ok}, 29 decode {fx, record, ok}). Task
| context. Before core 1.2 LFO3 hooked these two entries itself.
        .text
        .globl core_fx_encode,core_fx_decode
core_fx_encode:
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        move.l 12(%sp),-(%sp)
        jsr core_stock_fx_encode
        lea 12(%sp),%sp
        move.l %d0,-(%sp)
        andi.l #255,%d0
        move.l %d0,-(%sp)
        move.l 16(%sp),-(%sp)                   | fx
        move.l 16(%sp),-(%sp)                   | record
        pea 28
        jsr core_fx_codec_notify
        lea 16(%sp),%sp
        move.l (%sp)+,%d0
        rts
core_stock_fx_encode:
        .byte 0x2f,0x0b,0x2f,0x0a,0x24,0x6f,0x00,0x0c
        jmp 0x400a6d9e
core_fx_decode:
        move.l 8(%sp),-(%sp)
        move.l 8(%sp),-(%sp)
        jsr core_stock_fx_decode
        addq.l #8,%sp
        move.l %d0,-(%sp)
        andi.l #255,%d0
        move.l %d0,-(%sp)
        move.l 16(%sp),-(%sp)                   | record
        move.l 16(%sp),-(%sp)                   | fx
        pea 29
        jsr core_fx_codec_notify
        lea 16(%sp),%sp
        move.l (%sp)+,%d0
        rts
core_stock_fx_decode:
        .byte 0x4f,0xef,0xff,0xf4,0x48,0xd7,0x1c,0x00
        jmp 0x400a6d36
