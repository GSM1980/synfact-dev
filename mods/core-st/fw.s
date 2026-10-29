| SPDX-License-Identifier: GPL-2.0-or-later
| OS 1.41 firmware services for UI mods. Nontrivial C++ return values use A0.
        .macro FW name,value
        .globl \name
        .set \name,\value
        .endm
        FW fw_string_ctor,0x401bf082
        FW fw_invalidate,0x400ffc5e
        FW fw_fillrect,0x400f7df2
        FW fw_textf,0x400f8a0c
        FW fw_font5,0x443da210
        FW fw_bitmap_ctor,0x400f70fa
        FW fw_bitmap_dtor,0x400f702c
        FW fw_blit,0x400f8dac
| Native Data RPC/descriptor and eMMC services. IO is TASK context only;
| the native block primitives take the controller's own RTOS mutex.
        FW fw_mmc_read,0x4010f44e
        FW fw_mmc_write,0x4010f634
        FW fw_mmc_ready,0x444b96fc
        FW fw_mmc_sectors,0x444b9720
        FW fw_empty_string,0x444e14b8
        FW fw_info_copy,0x4019c31a
        FW fw_string_assign,0x401bde32
| Startup/task-only services and native SYN layout template.
        FW fw_malloc,0x4010350c
        FW fw_free,0x40103712
        FW fw_lcd_front,0x402d6dc0
        FW fw_lcd_back,0x402d6dc4
        FW fw_lcd_swap,0x40113756
        FW fw_source_layout,0x41b9f39c
        FW fw_view_machine,0x40030622
        FW fw_parameter_selecting,0x400c3294
        FW fw_reset_increments,0x400f6730
        FW fw_lane_ids,0x402340a4
