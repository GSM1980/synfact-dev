| SPDX-License-Identifier: GPL-2.0-or-later
| OS 1.42 firmware services for UI mods. Nontrivial C++ return values use A0.
        .macro FW name,value
        .globl \name
        .set \name,\value
        .endm
        FW fw_string_ctor,0x401bf31e
        FW fw_invalidate,0x400ffe22
        FW fw_fillrect,0x400f7fb6
        FW fw_textf,0x400f8bd0
        FW fw_font5,0x443db210
        FW fw_bitmap_ctor,0x400f72be
        FW fw_bitmap_dtor,0x400f71f0
        FW fw_blit,0x400f8f70
| Native Data RPC/descriptor and eMMC services. IO is TASK context only;
| the native block primitives take the controller's own RTOS mutex.
        FW fw_mmc_read,0x4010f612
        FW fw_mmc_write,0x4010f7f8
        FW fw_mmc_ready,0x444ba6fc
        FW fw_mmc_sectors,0x444ba720
        FW fw_empty_string,0x444e24b8
        FW fw_info_copy,0x4019c4de
        FW fw_string_assign,0x401be0ce
| Startup/task-only services and native SYN layout template.
        FW fw_malloc,0x401036d0
        FW fw_free,0x401038d6
        FW fw_memcpy,0x4011613c
        FW fw_memset,0x40116184
        FW fw_lcd_front,0x402d7110
        FW fw_lcd_back,0x402d7114
        FW fw_lcd_swap,0x4011391a
        FW fw_source_layout,0x41ba039c
        FW fw_view_machine,0x40030622
        FW fw_parameter_selecting,0x400c3340
        FW fw_reset_increments,0x400f68f4
        FW fw_lane_ids,0x40234344
