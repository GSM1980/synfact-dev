| SPDX-License-Identifier: GPL-2.0-or-later
| The explicit companion builder installs the corresponding AUDIO runtime.
| Do not use this capability declaration through the MAIN-only patch CLI.
        .section .run,"a"
        .balign 4
        .globl core_machine_audio_abi
core_machine_audio_abi:
        .long 0x454c0001
