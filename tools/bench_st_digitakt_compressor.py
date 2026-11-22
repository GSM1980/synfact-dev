"""Count Digitakt 1.54's unmodified compressor on synthetic 32-frame blocks.

Read-only research: no firmware output, relocation, device access or cycle model.
Requires the owner's OS and a ColdFire/EMAC-capable Unicorn build.
"""
import argparse
import collections
import hashlib
import json
import math
import random
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from elekloader.syx import Syx
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.m68k_const import UC_CPU_M68K_CFV4E, UC_M68K_REG_A7, UC_M68K_REG_PC

OS_SHA = 'f78ba80fa7b1da5fb0e1ff61ad61e9e71aafe79f4364fc49679f3651353e3cf6'
BASE, ENTRY, END, INIT = 0x40000400, 0x4007425e, 0x4007472a, 0x400741d8
STACK, RETURN = 0x48010000, 0x4f000000
SCRATCH, PCM, PRESET, TRACKS = 0x49000000, 0x49001000, 0x49002000, 0x49003000
STAGES = [
    (ENTRY, 'entry_and_sidechain_filter'),
    (0x400743d8, 'stereo_peak'),
    (0x40074412, 'log_threshold_ratio'),
    (0x400744f2, 'release_peak_hold'),
    (0x40074576, 'attack_makeup_exp'),
    (0x40074604, 'gain_smoothing'),
    (0x40074644, 'apply_gain'),
    (0x4007466e, 'scale_x16'),
    (0x4007469c, 'dry_wet_and_exit'),
]


class Bench:
    def __init__(self, image, gain_only=False):
        self.u = u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        for at, size in [(0x40000000, 0x300000), (0x80000000, 0x20000),
                         (0x43990000, 0x10000), (STACK - 0x10000, 0x20000),
                         (RETURN, 0x1000), (SCRATCH, 0x10000)]:
            u.mem_map(at, size)
        u.mem_write(BASE, image)
        if gain_only:
            # Laboratory-only recipe: after producing the 32 gain values,
            # skip digital PCM multiplication/scaling/blend and use its epilogue.
            # No saved image, relocated code, hardware CV or firmware patch.
            u.mem_write(0x40074644, bytes.fromhex('4ef940074714'))
        # Saturating fractional EMAC, four cleared accumulators. Outside counts.
        u.mem_write(RETURN + 0x100, bytes.fromhex('a93c000000a0a1c4a3c4a5c4a7c44e75'))
        self.armed = False
        self.counts = collections.Counter()
        self.code, self.tables, self.reads, self.writes = set(), set(), set(), set()
        self.pcs = set()
        u.hook_add(UC_HOOK_CODE, self.code_hook)
        u.hook_add(UC_HOOK_MEM_READ, self.read_hook)
        u.hook_add(UC_HOOK_MEM_WRITE, self.write_hook)
        self.call(RETURN + 0x100)
        self.call(INIT)

    def call(self, fn, *args):
        sp = STACK - 4 * (len(args) + 1)
        self.u.mem_write(sp, struct.pack('>%dI' % (len(args) + 1), RETURN, *args))
        self.u.reg_write(UC_M68K_REG_A7, sp)
        self.u.emu_start(fn, RETURN, count=100000)
        assert self.u.reg_read(UC_M68K_REG_PC) == RETURN, 'native function did not return'

    def code_hook(self, u, at, size, _):
        if not self.armed:
            return
        assert ENTRY <= at < END, 'unexpected call out of native compressor: %x' % at
        self.pcs.add(at)
        self.code.update(range(at // 16, (at + size - 1) // 16 + 1))
        name = next(name for lo, name in reversed(STAGES) if at >= lo)
        self.counts[name] += 1

    def read_hook(self, u, kind, at, size, value, _):
        if self.armed:
            lines = set(range(at // 16, (at + size - 1) // 16 + 1))
            self.reads.update(lines)
            if BASE <= at < BASE + 0x260000:
                self.tables.update(lines)

    def write_hook(self, u, kind, at, size, value, _):
        if self.armed:
            self.writes.update(range(at // 16, (at + size - 1) // 16 + 1))

    def block(self, samples, preset, measure):
        self.u.mem_write(PCM, struct.pack('>64i', *samples))
        self.u.mem_write(PRESET, struct.pack('>8H', *preset))
        for k in range(8):
            self.u.mem_write(TRACKS + 128 * k, struct.pack('>32i',
                             *[samples[(2 * i + k) % 64] for i in range(32)]))
        self.counts.clear()
        self.code.clear(); self.tables.clear(); self.reads.clear(); self.writes.clear()
        self.armed = measure
        self.call(ENTRY, SCRATCH, PCM, PRESET, TRACKS)
        self.armed = False
        out = struct.unpack('>64i', bytes(self.u.mem_read(SCRATCH, 256)))
        return dict(instructions=sum(self.counts.values()), stages=dict(self.counts),
                    code_bytes_16b_lines=16 * len(self.code),
                    table_bytes_16b_lines=16 * len(self.tables),
                    data_read_bytes_16b_lines=16 * len(self.reads),
                    data_written_bytes_16b_lines=16 * len(self.writes),
                    output_peak=max(map(abs, out)),
                    last_gain=struct.unpack('>i', bytes(self.u.mem_read(0x8000ce4c + 31 * 4, 4)))[0])


def samples(kind, block):
    rng = random.Random(800 + block)
    out = []
    for i in range(32):
        t = 32 * block + i
        if kind == 'silence':
            pair = [0, 0]
        elif kind == 'dc':
            pair = [2**28, -(2**27)]
        elif kind == 'full_scale':
            pair = [-(2**31), 2**31 - 1]
        elif kind == 'noise':
            pair = [rng.randrange(-2**30, 2**30) for _ in range(2)]
        elif kind == 'impulse':
            pair = [2**30 if t % 97 == 0 else 0, -(2**29) if t % 97 == 0 else 0]
        else:
            pair = [int(2**29 * math.sin(t * 2 * math.pi * 997 / 48000)),
                    int(2**28 * math.sin(t * 2 * math.pi * 431 / 48000))]
        out += pair
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--digitakt', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    raw = a.digitakt.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == OS_SHA, 'requires original Digitakt 1.54'
    image = Syx(raw).section(3)
    report = dict(units='guest instructions per 32 stereo frames, not cycles or CPU percent',
                  stock_sha256=OS_SHA, main_sha256=hashlib.sha256(image).hexdigest(),
                  function_sha256=hashlib.sha256(image[ENTRY - BASE:END - BASE]).hexdigest(),
                  function_start=hex(ENTRY), function_end_exclusive=hex(END),
                  function_bytes=END - ENTRY,
                  limitations=['Synthetic buffers and initialized native state; no full OS, hardware or cache timing.',
                               'Unmodified Digitakt digital compressor; not an FX-bus VCA port.',
                               'Input selector/filter and dry/wet are included; integration/UI/core dispatch excluded.',
                               'ColdFire instruction paths, not a numerical certification of Unicorn EMAC.'], cases={})
    gain_cases = {}
    for kind in ['silence', 'dc', 'sine', 'noise', 'impulse', 'full_scale']:
        for mode in ['normal', 'ratio_1_mix_0', 'fast_max_ratio', 'sidechain_1', 'sidechain_8', 'filter_127']:
            preset = [0x4000, 0x1800, 0x2000, 0x2000, 0x300, 0, 0, 0x7f00]
            if mode == 'ratio_1_mix_0':
                preset[4], preset[7] = 0, 0
            elif mode == 'fast_max_ratio':
                preset[0:3], preset[4] = [0, 0, 0], 0x700
            elif mode.startswith('sidechain_'):
                preset[5] = int(mode.split('_')[1]) << 8
            elif mode == 'filter_127':
                preset[6] = 0x7f00
            b = Bench(image)
            g = Bench(image, gain_only=True)
            for block in range(8):
                b.block(samples(kind, block), preset, False)
                g.block(samples(kind, block), preset, False)
            measured, gain_measured = [], []
            for block in range(8, 16):
                data = samples(kind, block)
                normal, gain = b.block(data, preset, True), g.block(data, preset, True)
                assert normal['last_gain'] == gain['last_gain'], 'gain-only lab changed the gain'
                measured.append(normal)
                gain_measured.append(gain)
            report['cases'][kind + '/' + mode] = dict(
                minimum=min(x['instructions'] for x in measured),
                maximum=max(x['instructions'] for x in measured),
                mean=sum(x['instructions'] for x in measured) / len(measured),
                stages_mean={name: sum(x['stages'].get(name, 0) for x in measured) / len(measured)
                             for _, name in STAGES},
                max_code_bytes=max(x['code_bytes_16b_lines'] for x in measured),
                max_table_bytes=max(x['table_bytes_16b_lines'] for x in measured),
                output_peak=max(x['output_peak'] for x in measured))
            gain_cases[kind + '/' + mode] = dict(
                minimum=min(x['instructions'] for x in gain_measured),
                maximum=max(x['instructions'] for x in gain_measured),
                mean=sum(x['instructions'] for x in gain_measured) / len(gain_measured),
                stages_mean={name: sum(x['stages'].get(name, 0) for x in gain_measured) / len(gain_measured)
                             for _, name in STAGES})
    report['fx_gain_only_lab'] = dict(
        description='Native detector and gain trajectory, PCM application skipped in emulated RAM only.',
        same_last_gain_as_original=True,
        excluded='ADC planar/interleaved adapter, VCA scaling/clamping, parameter/UI dispatch and hook.',
        cases=gain_cases)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS: %d cases, %d measured blocks; native function %d bytes' %
          (len(report['cases']), len(report['cases']) * 8, END - ENTRY))
    print('Range: %d..%d instructions/32 frames' %
          (min(x['minimum'] for x in report['cases'].values()), max(x['maximum'] for x in report['cases'].values())))
    print('Gain-only lab: %d..%d; final gain equals the original in every measured block' %
          (min(x['minimum'] for x in gain_cases.values()), max(x['maximum'] for x in gain_cases.values())))


if __name__ == '__main__':
    main()
