"""Assemble and count a supplied Synthack master-comp source tree, offline.

Uses only its formula-generated data; no stock OS, patch builder or device IO.
Pass --source, --toolchain, --work and --out. Requires ColdFire EMAC Unicorn.
"""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import random
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.m68k_const import UC_CPU_M68K_CFV4E, UC_M68K_REG_A3, UC_M68K_REG_A7, UC_M68K_REG_D1, UC_M68K_REG_D3, UC_M68K_REG_PC

BASE, RET, STACK, HW, KIT = 0x46000000, 0x50008000, 0x50010000, 0x50002000, 0x50004000


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ['source', 'toolchain', 'work', 'out']:
        p.add_argument('--' + key, type=Path, required=True)
    a = p.parse_args()
    source = a.source.resolve()
    folder = source / 'mods/master-comp'
    a.work.mkdir(parents=True, exist_ok=True)
    build = a.work.resolve()
    sys.path[:0] = [str(folder), str(source / 'tools/unpack'), str(source / 'tools/build')]
    spec = importlib.util.spec_from_file_location('compact_recipe', folder / 'make_patch.py')
    recipe = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(recipe)
    defs = dict(C_OCT=recipe.C_OCT, D_THR=0x4000, D_ATK=0x1800, D_REL=0x2000, D_MUP=0x2000, GFX_ON=1)
    defs.update({'ID_' + n: i for n, _, i, _ in recipe.PARAMS})
    defs.update({'TY_' + n: recipe.TYPES[n] for n, _, _, _ in recipe.PARAMS})
    defs.update({'LOG_T%d' % i: v for i, v in enumerate(recipe.LOG_T)})
    defs.update({'EXP_T%d' % i: v for i, v in enumerate(recipe.EXP_T)})
    (build / 'fmt_tables.inc').write_text(recipe.fmt_include())
    (build / 'dt_tables.inc').write_text(''.join('.long %d\n' % v for tab in
                                                [recipe.ATTACK, recipe.RELEASE, recipe.RATIO] for v in tab))
    (build / 'names.inc').write_text(''.join('n_%s_s: .asciz "%s"\nn_%s_l: .asciz "%s"\n' %
                                                (n.lower(), n, n.lower(), label) for n, label, _, _ in recipe.PARAMS))
    def run(tool, *args):
        return subprocess.check_output([str(a.toolchain / (tool + '.exe')), *map(str, args)], text=True)
    obj, elf, binary = [build / ('compact.' + ext) for ext in ['o', 'elf', 'bin']]
    run('as', '-mcpu=54415', *['--defsym=%s=%d' % (k, v & 0xffffffff) for k, v in defs.items()],
        '-I', build, '-o', obj, folder / 'comp.S')
    run('ld', '-N', '-Ttext=0x46000000', '-e', 'comp_hook', '-o', elf, obj)
    run('objcopy', '-O', 'binary', elf, binary)
    symbols = {}
    for line in run('nm', elf).splitlines():
        bits = line.split()
        if len(bits) == 3:
            symbols[bits[2]] = int(bits[0], 16)
    image = binary.read_bytes()
    report = dict(source_sha256=hashlib.sha256((folder / 'comp.S').read_bytes()).hexdigest(),
                  blob_bytes=len(image), text_bytes=symbols['k_const'] - BASE,
                  units='ColdFire guest instructions per 32 stereo frames; not cycles or CPU percent',
                  includes='comp_hook, change detection/conversion when needed, GR meter and VCA scale/clamp',
                  excludes='caller JSR/branch, UI setters, full core dispatch, cache timing and actual hardware', cases={})
    for kind in ['silence', 'dc', 'sine', 'noise', 'impulse', 'full_scale']:
        for mode in ['off', 'on', 'changing_controls']:
            u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
            u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
            for at, length in [(BASE, 0x10000), (0x80000000, 0x10000), (0x50000000, 0x20000),
                               (0x41b9d000, 0x10000), (0x43bde000, 0x1000), (0x444e1000, 0x1000)]:
                u.mem_map(at, length)
            u.mem_write(BASE, image)
            u.mem_write(0x800030bc, struct.pack('>I', KIT))
            u.mem_write(0x444e1334, struct.pack('>I', 0x5000a000))
            u.mem_write(RET + 0x100, bytes.fromhex('a93c000000a0a1c4a3c4a5c4a7c44e75'))
            armed, counter, code, data = [False], [0], set(), set()
            def code_hook(uc, at, size, _):
                if armed[0]:
                    assert BASE <= at < BASE + len(image), 'unexpected native call'
                    counter[0] += 1
                    code.update(range(at // 16, (at + size - 1) // 16 + 1))
            def data_hook(uc, kind, at, size, value, _):
                if armed[0] and BASE <= at < BASE + len(image):
                    data.update(range(at // 16, (at + size - 1) // 16 + 1))
            u.hook_add(UC_HOOK_CODE, code_hook)
            u.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, data_hook)
            def call(fn):
                u.mem_write(STACK - 4, struct.pack('>I', RET))
                u.reg_write(UC_M68K_REG_A7, STACK - 4)
                u.emu_start(fn, RET, count=100000)
                assert u.reg_read(UC_M68K_REG_PC) == RET
                assert u.reg_read(UC_M68K_REG_A7) == STACK
            call(RET + 0x100)
            measured, cold = [], None
            for block in range(64):
                values = [0x4000, 0x1800, 0x2000, 0x2000]
                if mode == 'changing_controls':
                    values = [0x1000 + (block % 20) * 256, (block % 128) * 256,
                              ((3 * block) % 128) * 256, 0x2000]
                ratio = 0 if mode == 'off' else 4
                for i, offset in enumerate([12, 14, 2, 8]):
                    word = ((values[i] ^ [0x4000, 0x1800, 0x2000, 0x2000][i]) & 0x7fff) | (((ratio >> i) & 1) << 15)
                    u.mem_write(KIT + 70 + offset, struct.pack('>H', word))
                rng = random.Random(600 + block)
                left, right = [], []
                for i in range(32):
                    t = block * 32 + i
                    pair = dict(silence=(0, 0), dc=(2**28, -(2**27)), full_scale=(-(2**31), 2**31 - 1),
                                sine=(int(2**29 * math.sin(t * 2 * math.pi * 997 / 48000)),
                                      int(2**28 * math.sin(t * 2 * math.pi * 431 / 48000))),
                                noise=(rng.randrange(-2**30, 2**30), rng.randrange(-2**30, 2**30)),
                                impulse=(2**30 if t % 97 == 0 else 0, 0))[kind]
                    left.append(pair[0]); right.append(pair[1])
                u.mem_write(0x80003c50, struct.pack('>32i', *left))
                u.mem_write(0x80003cd0, struct.pack('>32i', *right))
                u.reg_write(UC_M68K_REG_D1, 40000)
                u.reg_write(UC_M68K_REG_D3, 50000)
                u.reg_write(UC_M68K_REG_A3, HW)
                counter[0] = 0; code.clear(); data.clear(); armed[0] = True
                call(symbols['comp_hook'])
                armed[0] = False
                if block == 0:
                    cold = counter[0]
                if block >= 16:
                    measured.append(dict(instructions=counter[0], code_lines=len(code), data_lines=len(data)))
            result = dict(cold_first_block=cold,
                          minimum=min(x['instructions'] for x in measured), maximum=max(x['instructions'] for x in measured),
                          mean=sum(x['instructions'] for x in measured) / len(measured),
                          max_code_bytes=16 * max(x['code_lines'] for x in measured),
                          max_data_bytes=16 * max(x['data_lines'] for x in measured))
            report['cases'][kind + '/' + mode] = result
            print(kind, mode, result['minimum'], result['maximum'], 'first', cold, flush=True)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: 18 cases, 864 measured blocks; comp_hook includes VCA and meter, excludes caller')


if __name__ == '__main__':
    main()
