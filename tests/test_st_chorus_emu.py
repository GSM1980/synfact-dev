"""The chorus in Unicorn: core 1.2's mixer-event stubs, and chorus-st's mono path.

A. mods/core-st/fx-send.s, assembled and linked alone, at each of its three
   sites: with no subscriber and with two, the registers, stack and EMAC
   state reaching the stock continuation equal the stock instructions' own.
   The track site fills the send table row by row and calls each subscriber
   once a block, at track 11, with the table (and the native mute bits).
B. chorus-st's real bank kernels against numeric oracles: the mono send mix
   (ramps, write-back), the decimator, the HPF and the two-tap delay.
C. The whole mono chorus against Synfact's V56 (compiled from its pinned
   sources in syntakt-work) fed the same signal on both sides: its modulation
   (now once a block) must follow V56's delay trajectory, and the wet must
   keep V56's level and shape. With WDTH 0 both sides are equal. Instruction
   counts are reported. Not bit-exact: the send is mono (chorus 0.2).

Run with the Unicorn build and toolchain from docs/SYNTAKT-BUILD.md:
  SYNTAKT_WORK=... "$PY" -B tests/test_st_chorus_emu.py \\
      --bank <bank with chorus-st> --digitone <Digitone OS 1.44 .syx> [--blocks 300]
SYNTAKT_WORK is optional: without it test C is reported as SKIPPED.
"""
import argparse
import re
import json
import math
import hashlib
import importlib.util
import os
import random
import struct
import subprocess
import sys
import tempfile
import types
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT))
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE   # noqa: E402
from unicorn.m68k_const import *                                         # noqa: E402
from elekloader import formats                                           # noqa: E402
from elekloader.sdk import elf                                           # noqa: E402
from elekloader.devices.syntakt import DEVICE                            # noqa: E402

# V56 is a build of Synfact's chorus (test C). It is an input the repository does not include;
# without that variable test C is skipped and says so (see docs/SETUP.md).
_SYNTAKT_WORK = os.environ.get('SYNTAKT_WORK')
V56 = Path(_SYNTAKT_WORK) / 'loader-v14r56-chorus-fast' if _SYNTAKT_WORK else None
PINNED = {
    'chorus_dsp.c': '01ee4296d042a16183e65881bd362559c39e459e228a752312c404165a26dd54',
    'chorus_mix.c': 'bc7fcc68ab102990acd5e9fe51fc757dcdc322215db7643b5b882436d91b8bee',
    'chorus_params.c': '82e5437fa719d494029227e795132f7c5a0d847fcf1be5150f693c9c45eadd08',
    'chorus24.h': '8dbd56689d2b9fb412979851e0d4abfdd889cfc0e351a50f0802042916f17eda',
    'chorus24_kernels.py': 'faa7bc23bd35456d9b5f8ba0ca0090ff62c9db92f9998c94c320f92f2f71d5f8',
    'chorus24_design.json': 'bf4d3cd8de472c789ebb56bca93228b8c154474797a04e904b10ef7498f692cf'}
BANK_BASE, V56_BASE, STUB_BASE = 0x46000000, 0x4a400000, 0x46000000
SRAM, STACK, RETURN, DATA = 0x80000000, 0x48010000, 0x4f000000, 0x49000000
FX_POINTER, KIT_POINTER, SEND_BUS, TRACK_BUS = 0x800030bc, 0x800030b8, 0x8000dbd0, 0x8000dad0
FX_RECORD, MIX_RECORDS = DATA, DATA + 0x1000
PREFIX = os.environ.get('ELEKLOADER_CROSS', DEVICE.toolchain['prefix'])
CFLAGS = DEVICE.toolchain['cflags']


def run(cmd):
    subprocess.run(cmd, check=True)


class Cpu:
    def __init__(self):
        self.uc = u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        for at, size in ((SRAM, 0x20000), (STACK - 0x10000, 0x20000), (RETURN, 0x1000), (DATA, 0x10000)):
            u.mem_map(at, size)
        u.mem_write(RETURN, b'\x4e\x71' * 8)
        self.count = 0
        u.hook_add(UC_HOOK_CODE, self._count)

    def _count(self, u, at, size, user):
        self.count += 1

    def map(self, at, data):
        self.uc.mem_map(at, (len(data) + 0x1ffff) & ~0xffff)
        self.uc.mem_write(at, data)

    def call(self, fn, *args):
        sp = STACK - 4 * len(args) - 4
        self.uc.mem_write(sp, struct.pack('>%dI' % (len(args) + 1), RETURN, *[a & 0xffffffff for a in args]))
        self.uc.reg_write(UC_M68K_REG_A7, sp)
        self.uc.emu_start(fn, RETURN, count=2000000)
        if self.uc.reg_read(UC_M68K_REG_PC) != RETURN:
            raise AssertionError('call to %x did not return' % fn)
        return self.uc.reg_read(UC_M68K_REG_D0)

    def write(self, at, words, fmt='i'):
        self.uc.mem_write(at, struct.pack('>%d%s' % (len(words), fmt), *words))

    def read(self, at, n, fmt='i'):
        return struct.unpack('>%d%s' % (n, fmt), bytes(self.uc.mem_read(at, n * struct.calcsize(fmt))))


# ---- A: the core's stubs -------------------------------------------------------------
STUB_LIST, SUB_A, SUB_B, TABLE = DATA + 0x8000, DATA + 0x8100, DATA + 0x8200, DATA + 0x9000
STOCK = {'track': (0x4008f3a0, 'ae000900ae810900', 0x4008f3a8),
         'delay': (0x4008f828, '4eb940096890', 0x40096890),
         'reverb': (0x4008f952, '4eb940096e3c', 0x40096e3c)}


def link_stubs(folder):
    obj, out = folder / 'fx-send.o', folder / 'fx-send.elf'
    run([PREFIX + 'as', *DEVICE.toolchain['asflags'], '-o', str(obj), str(ROOT / 'mods/core-st/fx-send.s')])
    run([PREFIX + 'ld', '--section-start=.run=0x%x' % STUB_BASE, '--section-start=.bss=0x%x' % TABLE,
         '-e', 'core_track_send',
         *['--defsym=ev_%s=0x%x' % (n, STUB_LIST) for n in ('track_send', 'delay_in', 'reverb_in')],
         '-o', str(out), str(obj)])
    e = elf.Elf.load(out)
    return bytes(e.section('.run').data), {s.name: s.value for s in e.symbols if s.name}


def stub_machine(code, kind):
    c = Cpu()
    c.map(STUB_BASE, code)
    c.uc.mem_map(0x40000000, 0x200000)
    at, stock, cont = STOCK[kind]
    c.uc.mem_write(at, bytes.fromhex(stock))
    for a in (cont, SUB_A, SUB_B):
        c.uc.mem_write(a, b'\x4e\x75')                      # rts
    return c


EMAC_SET = bytes.fromhex('a93c000000a0a1c4a3c4')     # MACSR=0xa0, clear ACC0/ACC1 (D4 scratch)
EMAC_GET = bytes.fromhex('a180a381a982')                 # D0=ACC0, D1=ACC1, D2=MACSR
SNIPPET = RETURN + 0x100


def snippet(c, code):
    c.uc.mem_write(SNIPPET, code)
    c.uc.emu_start(SNIPPET, SNIPPET + len(code), count=16)


def emac(c):
    """(ACC0, ACC1, MACSR), read without disturbing them."""
    snippet(c, EMAC_GET)
    return tuple(c.uc.reg_read(r) for r in (UC_M68K_REG_D0, UC_M68K_REG_D1, UC_M68K_REG_D2))


def regs(c):
    names = [getattr(sys.modules['unicorn.m68k_const'], 'UC_M68K_REG_%s%d' % (r, i)) for r in 'DA' for i in range(8)]
    return [c.uc.reg_read(n) for n in names]


def stub_case(code, symbols, kind, subscribers, rnd):
    """Stock bytes vs the stub (entered as the patched site would) -> same state at the continuation."""
    at, stock, cont = STOCK[kind]
    calls = []
    states = []
    values = [rnd.getrandbits(32) for _ in range(16)]
    values[3] = rnd.choice((11, rnd.randrange(12)))         # D3 track
    values[2] = 4 * values[3]                               # D2 = 4 x track
    values[11] = MIX_RECORDS + 142 * values[3]              # A3 mixer record
    values[14] = STACK - 0x400                              # A6 frame
    values[15] = STACK - 0x800                              # A7
    mute = rnd.getrandbits(32)
    args = [rnd.getrandbits(32) for _ in range(3)]
    for patched in (False, True):
        c = stub_machine(code, kind)
        c.write(STUB_LIST, subscribers + [0], 'I')
        if patched:
            for s in subscribers:
                c.uc.hook_add(UC_HOOK_CODE, lambda u, a, size, x: calls.append(
                    struct.unpack('>6I', bytes(u.mem_read(u.reg_read(UC_M68K_REG_A7), 24)))), begin=s, end=s)
        snippet(c, EMAC_SET)
        c.write(values[14] - 508, [mute], 'I')
        for i, v in enumerate(values):
            c.uc.reg_write(getattr(sys.modules['unicorn.m68k_const'], 'UC_M68K_REG_%s%d' % ('DA'[i // 8], i % 8)), v)
        c.write(values[15], args, 'I')
        if kind == 'track':
            entry = symbols['core_track_send'] if patched else at
        else:                                               # the site's JSR has pushed its return
            sp = values[15] - 4
            c.write(sp, [at + 6], 'I')
            c.uc.reg_write(UC_M68K_REG_A7, sp)
            entry = symbols['core_%s_in' % kind] if patched else int(stock[4:], 16)
            if not patched:
                c.uc.mem_write(entry, b'\x4e\x75')
        stop = cont if kind == 'track' else int(stock[4:], 16)
        c.uc.emu_start(entry, stop, count=10000)
        assert c.uc.reg_read(UC_M68K_REG_PC) == stop, (kind, patched)
        stack = bytes(c.uc.mem_read(values[15] - (0 if kind == 'track' else 4), 16))   # live words only
        state = regs(c) + [stack, emac(c)]
        states.append(state)
    a, b = states
    # D0/A0 are scratch at the delay/reverb calls (C calls follow); D1/A1 at all three
    # sites are dead or rewritten by the stock code before use, except D0/D1 at track.
    keep = [i for i in range(16) if (kind == 'track' and i not in (8, 9)) or i not in (0, 1, 8, 9)]
    for i in keep:
        assert a[i] == b[i], (kind, 'register %s%d' % ('DA'[i // 8], i % 8), hex(a[i]), hex(b[i]))
    assert a[16:] == b[16:], (kind, subscribers, a[16:], b[16:])
    if kind == 'track':
        if subscribers:                                     # the track's row, then the block's call
            row = struct.unpack('>4I', bytes(c.uc.mem_read(TABLE + 16 * values[3], 16)))
            assert row == (values[0], values[1], values[11], 0), row
        assert len(calls) == (len(subscribers) if values[3] == 11 else 0), calls
        for call in calls:
            assert call[1] == TABLE and struct.unpack('>I', bytes(c.uc.mem_read(TABLE + 192, 4)))[0] == mute
    else:
        assert len(calls) == len(subscribers)


def test_stubs(folder, rnd):
    code, symbols = link_stubs(folder)
    n = 0
    for kind in STOCK:
        for subscribers in ([], [SUB_A], [SUB_A, SUB_B]):
            for _ in range(40):
                stub_case(code, symbols, kind, subscribers, rnd); n += 1
    print('A: fx-send.s, %d cases at 3 sites, 0/1/2 subscribers: PASS' % n)


# ---- B: chorus-st against V56 ---------------------------------------------------------
def load_bank(bank):
    raw = bank.read_bytes()
    size, nrel = struct.unpack_from('>2I', raw, 52)
    image = bytearray(raw[80 + 4 * nrel:80 + 4 * nrel + size])
    for off in struct.unpack_from('>%dI' % nrel, raw, 80):
        struct.pack_into('>I', image, off, struct.unpack_from('>I', image, off)[0] + BANK_BASE)
    e = elf.Elf.load(bank.parent / (bank.stem + '.work') / 'main.elf')
    return bytes(image), {s.name: s.value + BANK_BASE for s in e.symbols if s.name}


def build_v56(folder, digitone):
    for name, digest in PINNED.items():
        if hashlib.sha256((V56 / name).read_bytes()).hexdigest() != digest:
            raise SystemExit('V56 source changed: %s' % name)
    parsed, dev, rel = formats.load(digitone)
    if dev.key != 'digitone-mk1' or rel.version != '1.44':
        raise SystemExit('--digitone must be the Digitone OS 1.44')
    native = types.ModuleType('chorus_native')
    native.BASE, native.STATE_BEGIN, native.HPF = 0x40000400, 0x8000cbc8, 0x40234204
    native.source = lambda: parsed.section(3)
    sys.modules['chorus_native'] = native
    spec = importlib.util.spec_from_file_location('chorus24_kernels_v56', V56 / 'chorus24_kernels.py')
    kernels = importlib.util.module_from_spec(spec); spec.loader.exec_module(kernels)
    work = folder / 'v56'; work.mkdir()
    for name in PINNED:
        (work / name).write_bytes((V56 / name).read_bytes())
    (work / 'native.s').write_text(kernels.assembly())
    objs = []
    for name in ('chorus_dsp.c', 'chorus_mix.c', 'chorus_params.c', 'native.s'):
        obj = work / (name + '.o'); objs.append(str(obj))
        if name.endswith('.c'):
            run([PREFIX + 'gcc', *CFLAGS, '-fno-jump-tables', '-fno-tree-switch-conversion', '-DCHORUS_SRAM=1',
                 '-I', str(work), '-c', str(work / name), '-o', str(obj)])
        else:
            run([PREFIX + 'as', *DEVICE.toolchain['asflags'], '-o', str(obj), str(work / name)])
    out = work / 'v56.elf'
    run([PREFIX + 'ld', '-Ttext=0x%x' % V56_BASE, '-e', 'chorus_dsp_render', '-o', str(out), *objs])
    run([PREFIX + 'objcopy', '-O', 'binary', str(out), str(work / 'v56.bin')])
    e = elf.Elf.load(out)
    return (work / 'v56.bin').read_bytes(), {s.name: s.value for s in e.symbols if s.name}


def settings_record(values):
    words = [0] * 71
    words[0] = 0x4332
    for slot, v in zip((0x1a, 0x24, 0x27, 0x29, 0x2a, 0x37, 0x46), values):
        words[slot] = 0x8000 | v
    return words


def clamp(x):
    return max(-2**31, min(2**31 - 1, x))


def wrap(x):
    return (x + 2**31) % 2**32 - 2**31


def test_kernels(bank, rnd):
    image, s = load_bank(bank)
    c = Cpu(); c.map(BANK_BASE, image)
    buf, out, aux = DATA + 0x2000, DATA + 0x3000, DATA + 0x4000
    worst = {}
    # mono send mix: dst = sat(-sum gain*x) per sample, gains stepping before each sample
    for case in range(60):
        n, ramp = rnd.randrange(1, 13), rnd.random() < 0.5
        srcs = [[rnd.randrange(-2**31, 2**31) if case % 7 == 0 else rnd.randrange(-2**28, 2**28) for _ in range(32)]
                for _ in range(n)]
        gains = [rnd.randrange(-2**31, 2**31) for _ in range(n)]
        steps = [rnd.randrange(-2**25, 2**25) if ramp else 0 for _ in range(n)]
        for k in range(n):
            c.write(buf + 128 * k, srcs[k])
        c.write(aux, [buf + 128 * k for k in range(n)], 'I'); c.write(aux + 64, gains); c.write(aux + 128, steps)
        c.call(s['chorus24_mix'], out, aux, aux + 64, aux + 128, n, int(ramp))
        got, g, expect = c.read(out, 32), list(gains), []
        for i in range(32):
            acc = 0
            for k in range(n):
                if ramp:
                    g[k] = wrap(g[k] + steps[k])
                acc -= g[k] * srcs[k][i]
            expect.append(clamp(acc >> 31))
        worst['mix'] = max(worst.get('mix', 0), max(abs(x - y) for x, y in zip(got, expect)))
        assert worst['mix'] <= n + 1, (case, worst)
        if ramp:
            assert list(c.read(aux + 64, n)) == g, 'ramped gains not written back'
    # decimator: y[j] = sum c_k x[2j - p_k], coefficients Q30
    positions = json.loads((ROOT / 'mods/chorus-st/design.json').read_text())['decimator']['positions']
    coeff = c.read(s['chorus24_decimator_coeffs'], 16)
    for case in range(40):
        x = ([rnd.randrange(-2**28, 2**28) for _ in range(56)] if case < 32 else
             [(-2**31 if (i + case) % 3 else 2**31 - 1) for i in range(56)])
        c.write(buf, x)
        c.call(s['chorus24_decimate'], buf + 96, out, s['chorus24_decimator_coeffs'], aux)
        expect = [clamp(round(sum(coeff[k] * x[24 + 2 * j - positions[k]] for k in range(16)) / 2**30))
                  for j in range(16)]
        worst['decimate'] = max(worst.get('decimate', 0), max(abs(a - b) for a, b in zip(c.read(aux, 16), expect)))
    assert worst['decimate'] <= 10, worst
    # HPF: y = d2 (x - x') + d1 y', d2 = d1/2 + 1/2
    for case in range(40):
        d1 = c.read(s['chorus24_hpf24'] + 4 * rnd.randrange(128), 1)[0]
        state = [rnd.randrange(-2**27, 2**27), rnd.randrange(-2**27, 2**27)]
        x = [rnd.randrange(-2**28, 2**28) for _ in range(16)]
        c.write(buf, x); c.write(aux, state)
        c.call(s['chorus24_hpf'], buf, aux, d1)
        d2 = (d1 >> 1) + 2**30
        xp, y, expect = state[0], state[1], []
        for v in x:
            y = clamp((d2 * (v - xp) + d1 * y) >> 31); xp = v; expect.append(y)
        got = c.read(buf, 16)
        worst['hpf'] = max(worst.get('hpf', 0), max(abs(a - b) for a, b in zip(got, expect)))
        assert c.read(aux, 2) == (x[-1], got[-1])
    assert worst['hpf'] <= 4, worst
    # two-tap delay: each tap moves linearly from its node to the next over 16 frames
    for case in range(40):
        ring = [rnd.randrange(-2**29, 2**29) for _ in range(257)]
        ring[256] = ring[0]
        w = 16 * rnd.randrange(16)
        nodes = [rnd.randrange(100 << 22, 200 << 22) for _ in range(4)]
        c.write(buf, ring); c.write(aux, nodes)
        c.call(s['chorus24_delay'], out, buf, aux, w)
        got = c.read(out, 32)
        for ch in (0, 1):
            pos, step = nodes[2 * ch], (nodes[2 * ch + 1] - nodes[2 * ch]) >> 4
            for f in range(16):
                pos += step
                idx, frac = (w + f - (pos >> 22)) & 255, (pos & 0x3fffff) / 2**22
                # V56's weighting: (1 - frac) on ring[idx + 1], frac on ring[idx]
                e = clamp(round(2 * ((1 - frac) * ring[idx + 1] + frac * ring[idx])))
                worst['delay'] = max(worst.get('delay', 0), abs(got[2 * f + ch] - e))
    assert worst['delay'] <= 8, worst
    print('B: kernels against oracles, max error in Q31 units: %s: PASS' % worst)


def hot(c, s, fields):
    """Chorus hot-state words by chorus24_layout index (the position index - 2 words: previous delay)."""
    layout = c.read(s['chorus24_layout'], 11, 'I')
    base = c.read(s['chorus_ch'], 1, 'I')[0] if 'chorus_ch' in s else 0x80007340
    return {name: c.read(base + layout[index] + delta, count) for name, (index, delta, count) in fields.items()}


def test_chain(bank, digitone, folder, rnd, blocks):
    image, port = load_bank(bank)
    v56_image, v56 = build_v56(folder, digitone)
    fields = {'delay': (8, -8, 2)}
    report, cost = [], [0, 0]
    for values in ((0x7f00, 0x2000, 0, 0x7f00, 0, 0, 0x6400), (0x4000, 0x6000, 0x3000, 0x3000, 0, 0, 0x7f00),
                   (0x7f00, 0x7f00, 0x1000, 0, 0, 0, 0x6400), (0x2000, 0x1000, 0, 0x7f00, 0, 0, 0x6400)):
        a, b = Cpu(), Cpu()
        a.map(BANK_BASE, image); b.map(V56_BASE, v56_image)
        a.uc.mem_write(port['chorus_sync'], b'\x4e\x75')      # the emulator has no CACR
        assert a.call(port['chorus_place']) == 7              # state, kernels and per-block code in SRAM
        a.call(port['chorus_dsp_reset']); b.call(v56['chorus_dsp_reset'])
        record = settings_record(values)
        tracks = {t: (-rnd.randrange(2**29, 2**31), 0x7f00 if t < 4 else 0x3000) for t in range(6)}
        for c in (a, b):
            c.write(FX_RECORD, record, 'H'); c.write(FX_POINTER, [FX_RECORD], 'I'); c.write(KIT_POINTER, [0], 'I')
        wet, drift, phase = [[], []], 0.0, rnd.random()
        for block in range(blocks):
            pcm = [[int(2**27 * math.sin(2 * math.pi * (440 + 110 * t) * (32 * block + i) / 48000 + phase))
                    for i in range(32)] for t in range(12)]
            for c, s, is_port in ((a, port, True), (b, v56, False)):
                for t in range(12):
                    c.write(0x80003d50 + 128 * t if t < 8 else 0x80003950 + 128 * (t - 8), pcm[t])
                c.write(TRACK_BUS, [0] * 64)
                start = c.count
                if is_port:
                    rows = []
                    for t in range(12):
                        g, send = tracks.get(t, (0, 0))
                        c.write(MIX_RECORDS + 142 * t + 106, [send], 'H')
                        rows += [g & 0xffffffff, g & 0xffffffff, MIX_RECORDS + 142 * t, 0]
                    c.write(TABLE, rows + [0], 'I')
                    c.call(s['chorus_sends'], TABLE)
                else:
                    for t in range(12):
                        g, send = tracks.get(t, (0, 0))
                        c.call(s['chorus_track'], t, g, g, send, 0)
                c.call(s['chorus_before_delay']); c.call(s['chorus_before_reverb']); c.call(s['chorus_master_return'])
                cost[is_port] += c.count - start
                if block >= 60:
                    wet[is_port].append(c.read(TRACK_BUS, 64))
            if block >= 60:
                da, db = hot(a, port, fields)['delay'], hot(b, v56, fields)['delay']
                drift = max(drift, max(abs(x - y) for x, y in zip(da, db)) / 2**22)
        mono, ref = [v for blk in wet[1] for v in blk], [v for blk in wet[0] for v in blk]
        rms = lambda v: math.sqrt(sum(x * x for x in v) / len(v))
        ratio = 20 * math.log10(rms(mono) / rms(ref))
        corr = sum(x * y for x, y in zip(mono, ref)) / math.sqrt(sum(x * x for x in mono) * sum(y * y for y in ref))
        same = all(blk[0::2] == blk[1::2] for blk in wet[1]) if values[3] == 0 else None
        report.append((values, ratio, corr, drift, same))
    print('C: mono chorus against V56 fed L = R, %d blocks per setting:' % blocks)
    for values, ratio, corr, drift, same in report:
        print('   DPTH %3d SPD %3d HPF %3d WDTH %3d: level %+.2f dB, correlation %.3f, delay drift %.3f samples%s'
              % (values[0] >> 8, values[1] >> 8, values[2] >> 8, values[3] >> 8, ratio, corr, drift,
                 ', L = R' if same else ''))
    print('   instructions per block (6 sends, VOL): chorus-st %.0f, V56 %.0f'
          % (cost[1] / blocks / len(report), cost[0] / blocks / len(report)))
    for values, ratio, corr, drift, same in report:
        assert abs(ratio) < 1.0 and corr > 0.9 and drift < 0.5 and same in (None, True), (values, ratio, corr, drift)
    print('   PASS')


def test_span(bank):
    """hot.c's code, copied to SRAM, keeps no PC-relative reference outside itself."""
    e = elf.Elf.load(bank.parent / (bank.stem + '.work') / 'main.elf')
    s = {x.name: x.value for x in e.symbols if x.name}
    lo, hi = s['chorus_hot_c'], s['chorus_hot_c_end']
    for name in ('chorus_hot_sends', 'chorus_hot_before_delay', 'chorus_hot_before_reverb', 'chorus_hot_master_return'):
        assert lo <= s[name] < hi, name
    dump = subprocess.run([PREFIX + 'objdump', '-d', '--start-address=%d' % lo, '--stop-address=%d' % hi,
                           str(bank.parent / (bank.stem + '.work') / 'main.elf')],
                          capture_output=True, text=True, check=True).stdout
    relative = 0
    for line in dump.splitlines():
        m = re.search(r'\t(j\w+|b\w+|lea|pea|move\w*)\s.*%pc@\(([0-9a-f]+)', line) or \
            re.search(r'\t(b\w+|bra\w*|jbsr)\s+([0-9a-f]+) <', line)
        if m:
            target = int(m.group(2), 16)
            relative += 1
            assert lo <= target < hi, line
    lfo3 = ''
    if 'lfo3_render' in s:                                  # a full bank: LFO3's kernel still fits before the chorus
        n = s['lfo3_render_end'] - s['lfo3_render']
        assert n <= 0x800077c0 - 0x80007340, 'LFO3 kernel %d bytes outgrew its SRAM window' % n
        lfo3 = ', LFO3 kernel %d of 1152 bytes' % n
    print('D: hot.c span %d bytes, %d PC-relative references, all inside%s: PASS' % (hi - lo, relative, lfo3))


def test_reference(bank, reference, rnd, blocks):
    """The same arithmetic as a reference bank's chorus: bit-identical buses."""
    image, port = load_bank(bank)
    rimage, ref = load_bank(reference)
    a, b = Cpu(), Cpu()
    a.map(BANK_BASE, image); b.map(BANK_BASE, rimage)
    a.uc.mem_write(port['chorus_sync'], b'\x4e\x75'); b.uc.mem_write(ref['chorus_sync'], b'\x4e\x75')
    assert a.call(port['chorus_place']) == 7 and b.call(ref['chorus_place']) in (3, 7)   # test d: code in DDR; test e: in SRAM
    a.call(port['chorus_dsp_reset']); b.call(ref['chorus_dsp_reset'])
    sends, gains, record, silent = [0] * 12, [0] * 12, None, 0
    for block in range(blocks):
        if block % 23 == 0:                                 # settings move: returns ramp
            record = settings_record([rnd.choice((0, 0x2000, 0x4000, 0x7f00, rnd.randrange(0x7f01))) for _ in range(7)])
        if block % 5 == 0:
            for k in range(12):
                sends[k] = rnd.choice((0, 0, 0x3000, 0x7f00, rnd.randrange(0x7f01)))
                gains[k] = rnd.randrange(-2**31, 0)
        if block % 97 == 0:
            silent = rnd.randrange(0, 90)                   # long silences put the chorus to sleep
        outputs, muted = [], rnd.getrandbits(12) if block % 41 == 0 else 0   # one draw for both banks
        for c, s in ((a, port), (b, ref)):
            c.write(FX_RECORD, record, 'H'); c.write(FX_POINTER, [FX_RECORD], 'I'); c.write(KIT_POINTER, [0], 'I')
            pcm = random.Random(block)
            for k in range(12):
                c.write(0x80003d50 + 128 * k if k < 8 else 0x80003950 + 128 * (k - 8),
                        [0 if silent else pcm.randrange(-2**29, 2**29) for _ in range(32)])
            rows = []
            for k in range(12):
                c.write(MIX_RECORDS + 142 * k + 106, [sends[k]], 'H')
                rows += [gains[k] & 0xffffffff, (gains[k] >> 1) & 0xffffffff, MIX_RECORDS + 142 * k, 0]
            c.write(TABLE, rows + [muted], 'I')
            bus = random.Random(-block)
            c.write(SEND_BUS, [bus.randrange(-2**30, 2**30) for _ in range(64)])
            c.write(TRACK_BUS, [bus.randrange(-2**30, 2**30) for _ in range(64)])
            c.call(s['chorus_sends'], TABLE); c.call(s['chorus_before_delay'])
            d = c.read(SEND_BUS, 64); c.write(SEND_BUS, [bus.randrange(-2**30, 2**30) for _ in range(64)])
            c.call(s['chorus_before_reverb']); r = c.read(SEND_BUS, 64); c.call(s['chorus_master_return'])
            outputs.append((d, r, c.read(TRACK_BUS, 64)))
        if outputs[0] != outputs[1]:
            for k, name in enumerate(('delay bus', 'reverb bus', 'FX-track bus')):
                diff = [(i, x, y) for i, (x, y) in enumerate(zip(outputs[0][k], outputs[1][k])) if x != y]
                if diff:
                    print('  block %d %s: %d samples differ, first %s, max %d' % (block, name, len(diff), diff[0],
                          max(abs(x - y) for i, x, y in diff)))
        assert outputs[0] == outputs[1], 'block %d differs from the reference' % block
        silent = max(0, silent - 1)
    print('E: per-block code in SRAM = reference bank, bit for bit, over %d blocks: PASS' % blocks)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--bank', type=Path, required=True)
    p.add_argument('--digitone', type=Path, required=True)
    p.add_argument('--blocks', type=int, default=300)
    p.add_argument('--reference', type=Path, help='a bank with the same arithmetic (chorus test d)')
    a = p.parse_args()
    rnd = random.Random(56)
    with tempfile.TemporaryDirectory() as tmp:
        test_stubs(Path(tmp), rnd)
        test_kernels(a.bank, rnd)
        chain_ran = V56 is not None and V56.is_dir()
        if chain_ran:
            test_chain(a.bank, a.digitone, Path(tmp), rnd, a.blocks)
        test_span(a.bank)
        if a.reference:
            test_reference(a.bank, a.reference, rnd, 4 * a.blocks)
    if chain_ran:
        print('ALL PASS; no device access')
    else:
        print('PASS (A, B, D); C SKIPPED: set SYNTAKT_WORK to the folder with loader-v14r56-chorus-fast; no device access')


if __name__ == '__main__':
    main()
