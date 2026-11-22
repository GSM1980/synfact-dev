#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Write an SMZ4 bank into a dtcard-formatted card image so analog-emu's core
loads it at boot, as if installed on the +Drive.

  dtcard CARD.img format                 # once, makes the +Drive (analog-emu tool)
  make_card.py BANK.smz4 CARD.img [slot] # then place the bank in a GOOD slot

The core's v2 slot layout (mods/core-st/storage.h): slot base is
CORE_MODS_BASE + slot*CORE_MODS_SLOT_BYTES; the slot's final sector holds a
512-byte 'EMB2' metadata record (state GOOD=1). This writes the bank at the
slot base and a GOOD record with the real byte size, so core_mods_select_io
picks it. No firmware is committed; the card image is a scratch file.
"""
import struct
import sys
import zlib

CORE_MODS_BASE = 0x0240_0000
SLOT_BYTES = 0x0080_0000
SLOT_CAP = SLOT_BYTES - 1024          # core_mods_record_io rejects larger
GOOD = 1


def main(bank_path, card_path, slot=0):
    bank = open(bank_path, 'rb').read()
    if len(bank) > SLOT_CAP:
        sys.exit(f"bank {len(bank)} bytes exceeds slot capacity {SLOT_CAP}")
    base = CORE_MODS_BASE + slot * SLOT_BYTES
    meta = CORE_MODS_BASE + (slot + 1) * SLOT_BYTES - 512
    rec = bytearray(512)
    # EMB2 v2: magic, version, sequence, state, bytes, ~bytes, stamp, then crc32(:32)
    struct.pack_into('>7I', rec, 0, 0x454D4232, 2, 1, GOOD,
                     len(bank), (~len(bank)) & 0xFFFFFFFF, 0x5CA1AB1E)
    struct.pack_into('>I', rec, 32, zlib.crc32(bytes(rec[:32])))
    with open(card_path, 'r+b') as f:
        f.seek(base)
        f.write(bank)
        f.seek(meta)
        f.write(rec)
    print(f"placed {len(bank)}-byte bank in slot {slot} "
          f"(base {base:#x}, meta {meta:#x}) of {card_path}")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 0)
