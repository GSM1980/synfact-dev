"""modulation-skip-st's real bank code against the stock modulation, in Unicorn.

Two identical sp-one-shot fixtures (stock MAIN) run the P-region order block
after block: conversion 40097952, modulation 40097afc, LFO 40119266. In one,
40097b04 is patched exactly as the module's install does (`jmp abs.l` to the
relocated bank's modulation_skip_entry) and the module's mode is set (OFF,
VERIFY or ON). Random events on both: relative edits, set+publish, velocity,
source tables and amounts, machine-data entries ({amount, index} longs in the
unsmoothed mirror) and direct writes to destination words of the mirror (their
smoothed values change the modulation's inputs). After every call SRAM and the
LFO heap must be identical. Counts how often the stock body did not run.

Run with the Unicorn build from docs/SYNTAKT-BUILD.md:
  LIBUNICORN_PATH=... sp-one-shot/.venv/Scripts/python.exe -B tests/test_st_modulation_skip_emu.py \
      --bank <bank with modulation-skip-st> [--blocks 400]
"""
import argparse
import random
import struct
import sys
from pathlib import Path

WORK = Path('C:/Desarrollo/syntakt-dev/syntakt-work')
sys.dont_write_bytecode = True
sys.path[:0] = [str(WORK / 'sp-one-shot'), str(Path(__file__).resolve().parents[1])]
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED   # noqa: E402
from elekloader.sdk import elf                            # noqa: E402
from lfo_fixture import LfoRegistry                       # noqa: E402
from frame_fixture import WORKING                         # noqa: E402
from sound_fixture import MIRROR                          # noqa: E402

BASE = 0x46000000
SRAM, HEAP = (0x80000000, 0x20000), (0x444d0000, 0x20000)
OFF, VERIFY, ON = 0, 1, 2
TABLE_SETTERS = (0x400a3564, 0x400a35a2, 0x400a35d4, 0x400a3612)
VELOCITY, AMOUNT = 0x400a36a4, 0x400a3650
ENGINES = (0x51, 0x4e, 0x4f, 0x6a, 0x6d, 0x75, 0x7a, 0x56, 0x57)
RAW_MIRROR = MIRROR + 0x754                               # copied to working + 0x754 by the conversion
BODY = (0x40097b0a, 0x40097c1a)


def load(bank):
    raw = bank.read_bytes()
    size, nrel = struct.unpack_from('>2I', raw, 52)
    image = bytearray(raw[80 + 4 * nrel:80 + 4 * nrel + size])
    for off in struct.unpack_from('>%dI' % nrel, raw, 80):
        struct.pack_into('>I', image, off, struct.unpack_from('>I', image, off)[0] + BASE)
    e = elf.Elf.load(bank.parent / (bank.stem + '.work') / 'main.elf')
    return bytes(image), {s.name: s.value + BASE for s in e.symbols if s.name}


def fixture(rnd_seed, image=None, symbols=None, mode=OFF):
    rnd = random.Random(rnd_seed)
    m = LfoRegistry(False)
    m.reset_lfos()
    for t in range(12):
        m.set_value(0x59, 0x1400, t); m.set_value(0x5e, 0x4100, t); m.publish(t)
    m.modulation_trigs = 0xfff
    m.uc.hook_add(UC_HOOK_MEM_UNMAPPED, lambda uc, a, addr, s, v, u: (uc.mem_map(addr & ~0xffff, 0x10000), True)[1])
    for t in range(13):
        for s in TABLE_SETTERS: m.call(s, t, rnd.randrange(0, 0x47))
        if t < 12: m.call(VELOCITY, t, rnd.randrange(0x2000, 0x7f00)); m.call(AMOUNT, t, rnd.randrange(0, 0x7fff))
        for k in range(24):                               # every entry of the six groups
            m.write32(RAW_MIRROR + 0x7c * t + 4 * k, rnd.randrange(0, 0x8000) << 16 | rnd.randrange(0, 0x47))
    if image:
        m.uc.mem_map(BASE, (len(image) + 0x2ffff) & ~0xffff)
        m.uc.mem_write(BASE, image)
        entry = symbols['modulation_skip_entry']
        m.uc.mem_write(0x40097b04, struct.pack('>HI', 0x4ef9, entry))
        m.uc.mem_write(0x400a30c4, struct.pack('>HI', 0x4ef9, symbols['modulation_skip_machine_entry']))
        m.write32(symbols['modulation_skip_mode'], mode)
    m.stock_body = [0]
    m.uc.hook_add(UC_HOOK_CODE, lambda uc, at, size, u: m.stock_body.__setitem__(0, m.stock_body[0] + 1),
                  begin=BODY[0], end=BODY[0])
    return m


def counter(m, symbols, name):
    return struct.unpack('>I', bytes(m.uc.mem_read(symbols['modulation_skip_' + name], 4)))[0]


def bump(m, symbols, n):
    m.write32(symbols['modulation_skip_machine_writes'], counter(m, symbols, 'machine_writes') + n)


def unknown_writer(bank, seed):
    """Negative control: machine data changed without the writer's count is
    found by the periodic full comparison within 64 blocks and turns ON off."""
    image, symbols = load(bank)
    dut = fixture(seed, image, symbols, ON)
    for b in range(8):
        dut.call(0x40097952, MIRROR); dut.call(0x40097afc, WORKING)
    dut.write32(RAW_MIRROR + 0x7c * 4 + 0x24, 0x12340011)
    for b in range(70):
        dut.call(0x40097952, MIRROR); dut.call(0x40097afc, WORKING)
        if counter(dut, symbols, 'mode') == OFF:
            break
    mode = counter(dut, symbols, 'mode')
    assert mode == OFF and counter(dut, symbols, 'errors') == 1, (mode, b)
    return dict(found_after_blocks=b + 1)


def run(bank, mode, blocks, seed):
    image, symbols = load(bank)
    ref, dut = fixture(seed), fixture(seed, image, symbols, mode)
    rnd = random.Random(seed + 1)
    stats = dict(mode=mode, blocks=blocks, events=0, stock_runs=0, skipped=0)
    for b in range(blocks):
        r = rnd.random()
        if b > 3 and r < 0.05:
            t = rnd.randrange(12); stats['events'] += 1
            kind = rnd.choice((0, 1, 2, 2, 3, 4, 5, 6))      # velocity most often, as trigs bring it
            for m in (ref, dut):
                rr = random.Random(b)                     # identical event in both fixtures
                if kind == 0: m.nudge(rr.choice(ENGINES), rr.choice((-2, 1, 3)), t)
                elif kind == 1: m.set_value(rr.choice(ENGINES), rr.randrange(0, 0x7f00), t); m.publish(t)
                elif kind == 2: m.call(VELOCITY, t, rr.randrange(0x2000, 0x7f00))
                elif kind == 3: m.call(rr.choice(TABLE_SETTERS), t, rr.randrange(0, 0x47))
                elif kind == 4: m.call(AMOUNT, t, rr.randrange(0, 0x7fff))
                elif kind == 5:                           # as the hooked writer 400a30c4 does
                    m.write32(RAW_MIRROR + 0x7c * t + 4 * rr.randrange(24),
                              rr.randrange(0, 0x8000) << 16 | rr.randrange(0, 0x47))
                    if m is dut: bump(dut, symbols, 2)
                else:                                     # a destination's smoothed source changes
                    m.write32(MIRROR + 0x1c + 0x8e * t + 4 * rr.randrange(0x23) & ~3, rr.getrandbits(32) & 0x7fff7fff)
        for name, call in (('conversion', lambda m: m.call(0x40097952, MIRROR)),
                           ('modulation', lambda m: m.call(0x40097afc, WORKING)),
                           ('lfo', lambda m: m.call(0x40119266, WORKING, m.modulation_tempo, m.modulation_trigs))):
            before = dut.stock_body[0]
            call(ref); call(dut)
            if name == 'modulation':
                if dut.stock_body[0] != before: stats['stock_runs'] += 1
                else: stats['skipped'] += 1
            assert bytes(ref.uc.mem_read(*SRAM)) == bytes(dut.uc.mem_read(*SRAM)), (mode, b, name, 'SRAM')
            assert bytes(ref.uc.mem_read(*HEAP)) == bytes(dut.uc.mem_read(*HEAP)), (mode, b, name, 'heap')
    for name in ('blocks', 'hits', 'skips', 'errors'):
        stats[name] = counter(dut, symbols, name)
    assert stats['errors'] == 0, stats
    if mode == OFF: assert stats['skipped'] == 0 and stats['blocks'] == 0
    if mode == VERIFY: assert stats['skipped'] == 0 and stats['hits'] > 0
    # ON: every 64th matching block runs stock and compares (the VERIFY path).
    if mode == ON: assert stats['skipped'] == stats['skips'] > 0 and stats['hits'] == stats['skips'] + stats['hits'] // 64, stats
    return stats


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bank', type=Path, required=True)
    p.add_argument('--blocks', type=int, default=400)
    p.add_argument('--seed', type=int, default=5)
    a = p.parse_args()
    for mode in (OFF, VERIFY, ON):
        print(run(a.bank, mode, a.blocks if mode == ON else a.blocks // 4, a.seed))
    print(unknown_writer(a.bank, a.seed))
    print('PASS: OFF, VERIFY and ON leave SRAM and the LFO heap identical to stock after every '
          'conversion, modulation and LFO call; VERIFY found no difference; ON skipped the stock body')


if __name__ == '__main__':
    main()
