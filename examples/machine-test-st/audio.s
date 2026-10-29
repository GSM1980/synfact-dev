| SPDX-License-Identifier: GPL-2.0-or-later
| A deliberately silent machine. Native scheduler clears its output buffer;
| these callbacks preserve the native ABI and all voice state.
        .text
        .globl machine_test_update,machine_test_render
machine_test_update:
        rts
machine_test_render:
        rts
