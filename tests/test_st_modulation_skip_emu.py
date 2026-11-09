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
  SYNTAKT_WORK=... "$PY" -B tests/test_st_modulation_skip_emu.py \
      --bank <bank with modulation-skip-st> [--blocks 400]
SYNTAKT_WORK is the folder with the emulator fixtures (sp-one-shot/); without it the test is SKIPPED.
"""
import argparse
import os
import random
import struct
import sys
from pathlib import Path

WORK = Path(os.environ['SYNTAKT_WORK']) if os.environ.get('SYNTAKT_WORK') else None
if WORK is None or not (WORK / 'sp-one-shot').is_dir():
    print('SKIPPED: needs the emulator fixtures: set SYNTAKT_WORK (docs/SETUP.md)')
    sys.exit(0)
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


def fixture(rnd_seed, image=None, symbols=None, mode=OFF, dense=False):
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
            amount=rnd.randrange(0,0x8000) if dense or (t in (2,3,5) and k%4==0) else 0
            index=rnd.randrange(0,0x47)
            m.write32(RAW_MIRROR + 0x7c * t + 4 * k,
                      amount << 16 | ((index if dense else 28+(k//4)%2) if amount else 0))
        # The fixture's unused Sound slots start as a5a5. Real base controls
        # are bounded to 0..7fff; preserve that invariant for sparse skip cases.
        for i in range(0x47):
            at=MIRROR+0x1c+0x8e*t+2*i
            if struct.unpack('>H',bytes(m.uc.mem_read(at,2)))[0]>0x7fff:
                m.uc.mem_write(at,b'\0\0')
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


def bump(m, symbols, n, track=None):
    if track is not None:
        m.uc.mem_write(symbols['modulation_skip_machine_dirty']+track,b'\x01')
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


def zero_depth_filter_edits(bank,seed):
    """Two tracks name FREQ/RES with amount zero; edits must stay on the fast path."""
    image,symbols=load(bank)
    ref,dut=fixture(seed),fixture(seed,image,symbols,ON)
    for m in (ref,dut):
        for t in range(13):
            for k in range(24):m.write32(RAW_MIRROR+0x7c*t+4*k,0)
        for t in (3,5):
            m.write32(RAW_MIRROR+0x7c*t+4,28)
            m.write32(RAW_MIRROR+0x7c*t+8,29)
    values=(0x5000,0x5100,0x7fff,0x8000,0xa5a5,0xbfff,0xc000,0xffff,0x4000,0x6000)
    for b,value in enumerate(values):
        for m in (ref,dut):
            for t in (3,5):m.write32(MIRROR+0x1c+0x8e*t+56,value<<16|0x4000)
            m.call(0x40097952,MIRROR)
            m.call(0x40097afc,WORKING)
        assert bytes(ref.uc.mem_read(*SRAM))==bytes(dut.uc.mem_read(*SRAM)),b
    assert dut.stock_body[0]==1 and counter(dut,symbols,'skips')==9
    return dict(blocks=10,stock_runs=dut.stock_body[0],skips=counter(dut,symbols,'skips'))


def single_group_filter_edits(bank,seed):
    """A lone FREQ assignment runs its one native four-entry group exactly."""
    image,symbols=load(bank)
    ref,dut=fixture(seed),fixture(seed,image,symbols,ON)
    sparse=[0]
    dut.uc.hook_add(UC_HOOK_CODE,lambda uc,at,size,u:sparse.__setitem__(0,sparse[0]+1),
                    begin=symbols['modulation_skip_sparse'],end=symbols['modulation_skip_sparse'])
    source_bases=(0x80004670,0x80004746,0x8000472c,0x80004712,0x800046f8)
    for m in (ref,dut):
        for t in range(13):
            for k in range(24):m.write32(RAW_MIRROR+0x7c*t+4*k,0)
    for g in range(5):
        for m in (ref,dut):
            for k in range(24):m.write32(RAW_MIRROR+0x7c*2+4*k,0)
            m.write32(RAW_MIRROR+0x7c*2+16*g,0x0f00001c)
            m.uc.mem_write(source_bases[g]+4,struct.pack('>H',0x8001 if g&1 else 0x2000))
        bump(dut,symbols,2,2)
        for b in range(4):
            value=0x3000+0x100*g+0x40*b
            for m in (ref,dut):
                m.uc.mem_write(MIRROR+0x1c+0x8e*2+56,struct.pack('>H',value))
                m.call(0x40097952,MIRROR)
                m.call(0x40097afc,WORKING)
            assert bytes(ref.uc.mem_read(*SRAM))==bytes(dut.uc.mem_read(*SRAM)),(g,b)
    assert sparse[0]>=15 and counter(dut,symbols,'errors')==0,sparse
    fast=sparse[0]
    # A zero-depth group before the active group names FREQ too: native order
    # matters for out-of-range values, so this matrix must use the full row.
    for m in (ref,dut):
        m.write32(RAW_MIRROR+0x7c*2,28)
        m.uc.mem_write(MIRROR+0x1c+0x8e*2+56,struct.pack('>H',0x7fff))
        m.call(0x40097952,MIRROR)
    bump(dut,symbols,2,2)
    ref.call(0x40097afc,WORKING);dut.call(0x40097afc,WORKING)
    assert bytes(ref.uc.mem_read(*SRAM))==bytes(dut.uc.mem_read(*SRAM))
    assert sparse[0]==fast and counter(dut,symbols,'errors')==0
    return dict(groups=5,blocks=21,sparse_runs=fast,overlap_fallback=True)


def writer_track_hint(bank,seed):
    """The real Sound writer names one row; only its 24 words are rescanned."""
    image,symbols=load(bank)
    dut=fixture(seed,image,symbols,ON)
    dut.call(0x40097952,MIRROR);dut.call(0x40097afc,WORKING)
    # This writer was executed by fixture setup before the patch was applied.
    dut.uc.ctl_remove_cache(0x400a30c4,0x400a30cc)
    dut.call(0x400a30c4,MIRROR,2)
    flags=bytes(dut.uc.mem_read(symbols['modulation_skip_machine_dirty'],13))
    assert flags==b'\0\0\1'+b'\0'*10 and counter(dut,symbols,'machine_writes')==2,flags
    reads=[0]
    dut.uc.hook_add(UC_HOOK_CODE,lambda uc,at,size,u:reads.__setitem__(0,reads[0]+1),
                    begin=symbols['raw_word'],end=symbols['raw_word'])
    dut.call(0x40097952,MIRROR);dut.call(0x40097afc,WORKING)
    assert reads[0]==24 and counter(dut,symbols,'errors')==0,reads
    return dict(published_track=2,matrix_words_read=reads[0])


def dense_fallback(bank,seed):
    image,symbols=load(bank)
    ref,dut=fixture(seed,dense=True),fixture(seed,image,symbols,ON,dense=True)
    for b in range(3):
        for m in (ref,dut):
            m.call(0x40097952,MIRROR);m.call(0x40097afc,WORKING)
        assert bytes(ref.uc.mem_read(*SRAM))==bytes(dut.uc.mem_read(*SRAM)),b
    assert dut.stock_body[0]==3 and counter(dut,symbols,'skips')==0
    return dict(blocks=3,stock_runs=dut.stock_body[0])


def kernel_mask_sweep(bank,seed):
    """Each subset must calculate exactly its own rows, including the FX row."""
    image,symbols=load(bank)
    masks=[0,0x1fff,0x555,0xaaa]+[1<<t for t in range(13)]
    for mask in masks:
        ref,dut=fixture(seed,dense=True),fixture(seed,image,symbols,ON,dense=True)
        for m in (ref,dut):m.call(0x40097952,MIRROR)
        before=bytes(dut.uc.mem_read(WORKING+0x1c,13*0x8e))
        dut.write32(symbols['modulation_skip_run_mask'],mask)
        ref.call(0x40097afc,WORKING)
        dut.call(symbols['modulation_skip_filtered'],WORKING)
        for t in range(13):
            at=WORKING+0x1c+0x8e*t
            got=bytes(dut.uc.mem_read(at,0x8e))
            expected=(bytes(ref.uc.mem_read(at,0x8e)) if mask&(1<<t)
                      else before[0x8e*t:0x8e*(t+1)])
            assert got==expected,(hex(mask),t)
    return dict(masks=len(masks),rows=len(masks)*13)


def run(bank, mode, blocks, seed):
    image, symbols = load(bank)
    ref, dut = fixture(seed), fixture(seed, image, symbols, mode)
    rnd = random.Random(seed + 1)
    stats = dict(mode=mode, blocks=blocks, events=0, stock_runs=0, skipped=0, partial=0)
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
                    if m is dut: bump(dut, symbols, 2, t)
                else:                                     # a destination's smoothed source changes
                    m.write32(MIRROR + 0x1c + 0x8e * t + 4 * rr.randrange(0x23) & ~3, rr.getrandbits(32) & 0x7fff7fff)
        for name, call in (('conversion', lambda m: m.call(0x40097952, MIRROR)),
                           ('modulation', lambda m: m.call(0x40097afc, WORKING)),
                           ('lfo', lambda m: m.call(0x40119266, WORKING, m.modulation_tempo, m.modulation_trigs))):
            before = dut.stock_body[0]
            before_partial=counter(dut,symbols,'partial_runs')
            call(ref); call(dut)
            if name == 'modulation':
                if dut.stock_body[0] != before: stats['stock_runs'] += 1
                else: stats['skipped'] += 1
                if counter(dut,symbols,'partial_runs')!=before_partial:
                    mask=counter(dut,symbols,'run_mask')
                    assert 0<mask.bit_count()<12,(b,hex(mask))
                    stats['partial']+=1
            expected=bytes(ref.uc.mem_read(*SRAM));actual=bytes(dut.uc.mem_read(*SRAM))
            if expected!=actual:
                first=next(i for i,(x,y) in enumerate(zip(expected,actual)) if x!=y)
                run_mask=counter(dut,symbols,'run_mask')
                raise AssertionError((mode,b,name,'SRAM',hex(SRAM[0]+first),
                                      expected[first:first+8].hex(),actual[first:first+8].hex(),hex(run_mask)))
            assert bytes(ref.uc.mem_read(*HEAP)) == bytes(dut.uc.mem_read(*HEAP)), (mode, b, name, 'heap')
    for name in ('blocks', 'hits', 'skips', 'errors'):
        stats[name] = counter(dut, symbols, name)
    assert stats['errors'] == 0, stats
    if mode == OFF: assert stats['skipped'] == 0 and stats['blocks'] == 0
    if mode == VERIFY: assert stats['skipped'] == 0
    # ON: full stock runs periodically; filtered blocks execute only marked tracks.
    if mode == ON: assert stats['skipped'] == stats['skips'] > 0 and stats['partial'] > 0, stats
    return stats


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bank', type=Path, required=True)
    p.add_argument('--blocks', type=int, default=400)
    p.add_argument('--seed', type=int, default=5)
    a = p.parse_args()
    for mode in (OFF, VERIFY, ON):
        print(run(a.bank, mode, a.blocks if mode == ON else a.blocks // 4, a.seed))
    print(zero_depth_filter_edits(a.bank,a.seed))
    print(single_group_filter_edits(a.bank,a.seed))
    print(writer_track_hint(a.bank,a.seed))
    print(dense_fallback(a.bank,a.seed))
    print(kernel_mask_sweep(a.bank,a.seed))
    print(unknown_writer(a.bank, a.seed))
    print('PASS: OFF, VERIFY and ON leave SRAM and the LFO heap identical to stock after every '
          'conversion, modulation and LFO call; VERIFY found no difference; ON skipped the stock body')


if __name__ == '__main__':
    main()
