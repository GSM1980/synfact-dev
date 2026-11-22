# SPDX-License-Identifier: AGPL-3.0-or-later
"""DigiPRO on cfbench (analog-emu's ColdFire V4e with its EMAC): the EMAC build
equals the split integer build in PCM, voice state and every EMAC register,
and the instructions of 32-frame renders at heavy settings. Instructions only:
no cache stalls, not a hardware CPU percentage. No device access."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader.devices.syntakt import DEVICE  # noqa: E402
from elekloader.sdk.elf import Elf  # noqa: E402

NAMES = {12: 'DPRO-WAVE', 13: 'DPRO-DDRW', 14: 'DPRO-DENS'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cfbench', type=Path, required=True)
    p.add_argument('--sgdk-bin', type=Path, default=os.environ.get('SGDK_BIN'))
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--os-rate', type=int, choices=(1, 2), default=1)
    p.add_argument('--blocks', type=int, default=20)
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    assert a.sgdk_bin, 'SGDK_BIN'
    flags = DEVICE.toolchain['cflags'] + ['-fno-jump-tables', '-I', 'mods/digimono-st', f'-DDM_DPRO_OS={a.os_rate}']

    def run(args):
        return subprocess.run([str(x) for x in args], check=True, capture_output=True, text=True, cwd=ROOT)
    objects = []
    for name, src, extra in (
            ('target', 'mods/digimono-st/dpro.c', []),
            ('portable', 'mods/digimono-st/dpro.c', ['-DDM_DP_SPLIT', '-Ddm_dp_init=portable_init',
                                                     '-Ddm_dp_trig=portable_trig', '-Ddm_dp_half=portable_half',
                                                     '-Ddm_dp_render=portable_render', '-Ddm_dp_word=portable_word',
                                                     '-Ddm_dp_increments=portable_increments']),
            ('driver', 'tests/st_dpro_cfbench.c', [])):
        obj = (a.out / (name + '.o')).resolve()
        run([a.sgdk_bin / 'gcc.exe', *flags, *extra, '-c', src, '-o', obj])
        objects.append(obj)
    script = a.out / 'image.ld'
    script.write_text('SECTIONS { .image 0x40001000 : { *(.text*) *(.rodata*) *(.data*) BYTE(0) . = ALIGN(4); '
                      '*(.bss*) *(COMMON) . = ALIGN(4); } /DISCARD/ : { *(.comment) *(.note*) *(.gnu.attributes) } }\n')
    exe, binary = (a.out / 'image.elf').resolve(), (a.out / 'image.bin').resolve()
    run([a.sgdk_bin / 'ld.exe', '-T', script.resolve(), *objects, '-o', exe])
    run([a.sgdk_bin / 'objcopy.exe', '-O', 'binary', '-j', '.image', exe, binary])
    syms = {s.name: s.value for s in Elf.load(exe).symbols if s.name}
    result = run([a.cfbench, binary, '40001000', f'{syms["check_dp"]:x}:{a.blocks:x},65815630'])
    values = re.findall(r'-> d0 ([0-9a-f]{8}): (\d+) instructions', result.stdout)
    assert len(values) == 1 and int(values[0][0], 16) == 0, result.stdout
    print(f'PASS: {3 * 8 * a.blocks} EMAC/split DigiPRO renders equal in PCM, state and EMAC registers', flush=True)
    costs = {}
    for kind in (12, 13, 14):
        for note in (36, 60, 84, 108):
            arg = f'{kind:x},{note * 64:x}'  # cfbench reads hexadecimal
            steps = [f'{syms["setup_dp"]:x}:{arg}'] + [f'{syms["render_dp"]:x}:{arg}'] * 16
            out = run([a.cfbench, binary, '40001000', *steps]).stdout
            counts = [int(n) for _, n in re.findall(r'-> d0 ([0-9a-f]{8}): (\d+) instructions', out)][1:]
            assert len(counts) == 16, out
            costs.setdefault(NAMES[kind], {})[note] = dict(max=max(counts), mean=round(sum(counts) / 16))
        print(NAMES[kind], {n: c['max'] for n, c in costs[NAMES[kind]].items()}, flush=True)
    (a.out / 'results.json').write_text(json.dumps(dict(
        ok=True, os_rate=a.os_rate, blocks=a.blocks, render_instructions=costs, physical_emac_model=True,
        hardware_cycles=False,
        source_sha256=hashlib.sha256((ROOT / 'mods/digimono-st/dpro.c').read_bytes()).hexdigest()), indent=2) + '\n')
    print('PASS: instruction counts written; no device access')


if __name__ == '__main__':
    main()
