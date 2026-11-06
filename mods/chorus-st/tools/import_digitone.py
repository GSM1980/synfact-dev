# SPDX-License-Identifier: GPL-2.0-or-later
"""Rebuild the chorus's Digitone tables from your own Digitone OS file.

    python mods/chorus-st/tools/import_digitone.py <Digitone OS 1.44 .syx or .zip>

The chorus's modulation shape (a 32-entry LUT) and its HPF coefficients are
the Digitone's. This repository holds only the recipe (digitone.json): which
bytes of the OS file to read, each pinned by its sha256. This reads them from
your file and writes imported/: lut.bin, hpf.bin and hpf24.bin, the HPF
retuned from 48 to 24 kHz. Run it once before building a bank with the
chorus. Your file stays on your machine; nothing is downloaded or sent.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

FOLDER = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(FOLDER.parents[1]))
from elekloader.sdk.syntakt import donor

# The 24 kHz HPF: the bilinear coefficient a of the 48 kHz one-pole HPF,
# remapped to the same corner at half the rate, a' = (3a - 1) / (3 - a).
HPF24_SHA256 = '838c75fced74777dad9fd498fa290777eb0c67cc8b6fa4b1e42cdcd7848b4660'


def hpf24(hpf):
    values = struct.unpack('>128i', hpf)
    return struct.pack('>128i', *(round((3 * (x / 2**31) - 1) / (3 - x / 2**31) * 2**31) for x in values))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('os_file', type=Path, help='your Digitone OS file')
    a = p.parse_args()
    recipe = json.loads((FOLDER / 'digitone.json').read_text())
    try:
        outputs = donor.rebuild(recipe, a.os_file.read_bytes())
    except donor.DonorError as e:
        sys.exit('import_digitone: %s' % e)
    outputs['imported/hpf24.bin'] = hpf24(outputs['imported/hpf.bin'])
    if hashlib.sha256(outputs['imported/hpf24.bin']).hexdigest() != HPF24_SHA256:
        sys.exit('import_digitone: the 24 kHz HPF differs from the one the chorus was made with')
    (FOLDER / 'imported').mkdir(exist_ok=True)
    for name, data in outputs.items():
        (FOLDER / name).write_bytes(data)
        print('wrote', name)


if __name__ == '__main__':
    main()
