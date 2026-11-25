# SPDX-License-Identifier: GPL-2.0-or-later
"""SP WAVETBL on cfbench (analog-emu's ColdFire V4e): the instructions of one
track's 32-frame block, both oscillators on a 64-wave table at the heaviest
setting, per note. Built with the bank's flags. Instructions only: no cache
stalls, not a hardware CPU percentage. No device access.

    python tests/test_wavetable_st_cfbench.py --cfbench <cfbench.exe> --out out/<new folder>
"""
import argparse
import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader.devices.syntakt import DEVICE  # noqa: E402
from elekloader.sdk.elf import Elf  # noqa: E402


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cfbench', type=Path, required=True)
    p.add_argument('--sgdk-bin', type=Path, default=os.environ.get('SGDK_BIN'))
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    assert a.sgdk_bin, 'SGDK_BIN'
    flags = DEVICE.toolchain['cflags'] + ['-fno-jump-tables', '-I', 'elekloader/sdk/include']

    def run(args):
        return subprocess.run([str(x) for x in args], check=True, capture_output=True, text=True, cwd=ROOT)
    objects = []
    for name, src in (('engine', 'mods/wavetable-st/engine.c'), ('driver', 'tests/st_wavetable_cfbench.c')):
        obj = (a.out / (name + '.o')).resolve()
        run([a.sgdk_bin / 'gcc.exe', *flags, '-c', src, '-o', obj])
        objects.append(obj)
    script = a.out / 'image.ld'
    script.write_text('SECTIONS { .image 0x40001000 : { *(.text*) *(.rodata*) *(.data*) BYTE(0) . = ALIGN(4); '
                      '*(.bss*) *(COMMON) . = ALIGN(4); } /DISCARD/ : { *(.comment) *(.note*) *(.gnu.attributes) } }\n')
    exe, binary = (a.out / 'image.elf').resolve(), (a.out / 'image.bin').resolve()
    run([a.sgdk_bin / 'ld.exe', '-T', script.resolve(), *objects, '-o', exe])
    run([a.sgdk_bin / 'objcopy.exe', '-O', 'binary', '-j', '.image', exe, binary])
    syms = {s.name: s.value for s in Elf.load(exe).symbols if s.name}
    costs = {}
    for case, bit in (('anim', 0), ('fixed', 1 << 16), ('sample', 3 << 16)):
        for note in (24, 48, 60, 72, 96, 120):
            arg = f'{note * 256 | bit:x}'  # cfbench reads hexadecimal
            steps = [f'{syms["setup_wt"]:x}:{arg}'] + [f'{syms["render_wt"]:x}:{arg}'] * 16
            out = run([a.cfbench, binary, '40001000', *steps]).stdout
            counts = [int(n) for _, n in re.findall(r'-> d0 ([0-9a-f]{8}): (\d+) instructions', out)][1:]
            assert len(counts) == 16, out
            costs.setdefault(case, {})[note] = dict(max=max(counts), mean=round(sum(counts) / 16))
        print(f'one track, two oscillators, {case}: instructions per block by note', {n: c['max'] for n, c in costs[case].items()})
    (a.out / 'results.json').write_text(json.dumps(dict(ok=True, block_instructions=costs, hardware_cycles=False), indent=2) + '\n')
    print('PASS: instruction counts written; no device access')


if __name__ == '__main__':
    main()
