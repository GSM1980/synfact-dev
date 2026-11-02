"""Rebuild FM TONE's Digitone parts from your own Digitone OS file.

    python mods/fm-tone-st/tools/import_digitone.py <Digitone OS 1.44 .syx or .zip>

FM TONE's DSP, tables and drawings are the Digitone's. This repository holds
only the recipe (digitone.json): which parts of the OS file to read, and our
own bytes. This reads them from your file, checks every result against its
sha256, and writes imported/: the bytes dsp.s and tables.s include, and
ui-data.h. Run it once before building a bank with FM TONE. Your file stays
on your machine; nothing is downloaded or sent.
"""
import argparse
import json
import re
import struct
import sys
from pathlib import Path

FOLDER = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(FOLDER.parents[1]))
from elekloader.sdk.syntakt import donor

# outputs written through a template: the drawings' bytes -> ui-data.h
TEMPLATES = {'imported/ui-data.bin': ('ui-data.h.in', 'imported/ui-data.h')}


def fill(template, data):
    """A template's @u32:rows,cols@ / @u8:n@ places <- the drawings' bytes, in order."""
    at = 0

    def value(m):
        nonlocal at
        kind, shape = m[1], [int(x) for x in m[2].split(',')]
        size, code = (4, 'I') if kind == 'u32' else (1, 'B')
        rows, cols = (shape[0], shape[1]) if len(shape) == 2 else (1, shape[0])
        out = []
        for _ in range(rows):
            out.append('{' + ','.join(str(v) for v in struct.unpack_from('>%d%s' % (cols, code), data, at)) + '}')
            at += cols * size
        return '{' + ','.join(out) + '}' if len(shape) == 2 else out[0]
    text = re.sub(r'@(u32|u8):([\d,]+)@', value, template)
    if at != len(data):
        raise donor.DonorError('the drawings do not fill their template')
    return text


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('os_file', type=Path, help='your Digitone OS file')
    a = p.parse_args()
    recipe = json.loads((FOLDER / 'digitone.json').read_text())
    try:
        outputs = donor.rebuild(recipe, a.os_file.read_bytes())
    except donor.DonorError as e:
        sys.exit('import_digitone: %s' % e)
    (FOLDER / 'imported').mkdir(exist_ok=True)
    for name, data in outputs.items():
        if name in TEMPLATES:
            template, path = TEMPLATES[name]
            (FOLDER / path).write_text(fill((FOLDER / template).read_text(), data), newline='\n')
        else:
            path = name
            (FOLDER / path).write_bytes(data)
        print('wrote', path)


if __name__ == '__main__':
    main()
