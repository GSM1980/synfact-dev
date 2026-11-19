# SPDX-License-Identifier: GPL-2.0-or-later
"""Syntakt OS 1.42: CPU1 core and an explicitly requested CPU2 companion.

Section 3 loads at 0x40000400. Bootstrap and updater stage its packed
stream at 0x40200000. There is no HMAC trailer in this pinned release.
Transport retains the full-zero-packet convention when preamble + container
is 101-aligned (1.41); official 1.42 ends with 94 padding bytes instead.

The boot copier uses 64 KiB at 0x46000000, following SyntHack-OS's
documented modarea (see docs/DEVICES.md).
The stock BSS clear ends at 0x45566490; its boot stack is 0x48000000.
No copy/BSS routine is patched and NO_DTIM0 preserves the native timer.

The flash budget below deliberately permits no container larger than
official 1.42. This is an experimental packaging policy, NOT a measured
flash or receiver limit. The current Python compressor produced a larger
file that failed during a 1.41 transfer; a compact EFT repack loaded on a unit.
Until compact compression is integrated, build and lint the core with
the SDK, then use the documented external packaging step. No compiler
backend, diagnostic mod or emulator is introduced by this profile.
"""
from . import Device, Release

DEVICE = Device(
    key='syntakt', name='Syntakt', sysex_id=0x16,
    releases={
        '1.41': Release(
            version='1.41',
            syx_sha256='8e2488f462c4a5656396a895f113bcd415e9900fa8709340dccf45d4cb9ed19e',
            main_sha256='8cf2b4b4ea80d58953da46ca453328454fb3fca5cb5f844a2f7bad4b72241f53',
            main_len=3438480),
        '1.42': Release(
            version='1.42',
            syx_sha256='ded3c59fdb909eb9e2c3307b30a486452c8fa6947b12c14e0ce35a187b7f37c1',
            main_sha256='1a9723b19fb54d0566c1f690379d2e6dfe43227f77e170ad731dd029430eb3e7',
            main_len=3442576),
    },
    main_section=3, main_load=0x40000400, stage=0x40200000,
    flash_at=0x80000,
    flash_limit=0x80000 + 2029392,  # official 1.42 container: same conservative policy
    trailer=None, isa='coldfire', version_optional=True,
    terminal_zero_packet=True,
    secondary={7: ('daf6451cf9587c0b628e901b7bb6b25f4e2633d448c534c0c181dd35ec783bc2',
                   383760, 0x40000400,
                   ((0x4000094a, 0x40000950),
                    # chain end: after the AMP stage, before the track DMA
                    (0x40000ac6, 0x40000acc),
                    (0x40000b04, 0x40000b0a),
                    (0x40001016, 0x40001020),
                    # base-width filter: the two low-pass pole lookups
                    (0x40002364, 0x4000236c), (0x400023e8, 0x400023f0),
                    (0x400043b2, 0x400043b6),
                    (0x400043fe, 0x40004400), (0x40004404, 0x40004406),
                    (0x40004408, 0x4000440c), (0x40004460, 0x40004462),
                    (0x400044ac, 0x400044b0),
                    (0x40040860, 0x40041050)))},
    areas={'ddr': (0x46000000, 0x46010000)},
    ddr=(0x46000000, 0x46010000),
    recovery='Restore the official Syntakt OS using Elektron\'s documented OS upgrade procedure.',
    toolchain={
        'prefix': 'm68k-elf-',
        'asflags': ['-mcpu=54415'],
        # Bound complete loop expansion: larger known-count loops otherwise
        # become long chains of absolute loads/branches on ColdFire V4.
        'cflags': ['-mcpu=54415', '-mtune=cfv4', '-O3', '--param=max-completely-peel-times=4',
                   '-ffreestanding', '-fno-builtin', '-nostdlib',
                   '-fno-pic', '-fno-pie', '-fomit-frame-pointer', '-Wall'],
    },
)
