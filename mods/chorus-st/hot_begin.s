| SPDX-License-Identifier: GPL-2.0-or-later
| Start of the per-block code: mod.json links hot.c's code right after this
| file's, and hot_end.s right after it; chorus_place copies the span to SRAM.
        .text
        .globl chorus_hot_c
chorus_hot_c:
