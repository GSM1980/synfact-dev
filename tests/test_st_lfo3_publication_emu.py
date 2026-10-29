"""Compiled LFO3 publication differential, including real owner lifetimes.

Executes both bank implementations with real shadow/owner/publication code.
Only native memcpy, allocator, recursive mutex and descriptor
helpers are boundary doubles. Checks live/pool/owned/side sources, changes of
values/type, copies, stale hashes, cell reuse, free, relocation and IRQ bypass.
Freed metadata is poisoned: retaining an invalid cached record must fail.
"""
import argparse
import hashlib
import json
import struct
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader import syx
from elekloader.sdk import elf
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE, UC_HOOK_MEM_READ
from unicorn.m68k_const import *

BASE, BANK, STACK, RETURN = 0x40000400, 0x47000000, 0x48010000, 0x4f000000
LIVE, POOL, OBJECT, META = 0x4b000000, 0x4b100000, 0x4c000000, 0x4e000000
BUDGET, ENGINE, SCRATCH = 0x4b1f0000, 0x4b1f1000, 0x4c180000
SAVED = [UC_M68K_REG_D2, UC_M68K_REG_D3, UC_M68K_REG_D4, UC_M68K_REG_D5,
         UC_M68K_REG_D6, UC_M68K_REG_D7, UC_M68K_REG_A2, UC_M68K_REG_A3,
         UC_M68K_REG_A4, UC_M68K_REG_A5, UC_M68K_REG_A6]


class Runtime:
    def __init__(self, native, path, break_invalidation=False):
        self.raw = path.read_bytes()
        self.report = json.loads(Path(str(path)+'.json').read_text())
        e = elf.Elf.load(path.parent/(path.stem+'.work')/'main.elf')
        self.sym = {s.name: BANK+s.value for s in e.symbols if s.name and s.bind == 1}
        self.local = {name: BANK+next(s.value for s in e.symbols if s.name == name)
                      for name in ('frame', 'dest_raw', 'shadow_owners')}
        self.frame_bytes = next(s.size for s in e.symbols if s.name == 'frame')
        assert self.frame_bytes == 282
        u = self.u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        u.mem_map(0x40000000, (len(native)+0x103ff)&~0xffff)
        u.mem_write(BASE, native)
        size, nrel = struct.unpack_from('>2I', self.raw, 52)
        image = bytearray(self.raw[80+4*nrel:80+4*nrel+size])
        for off in struct.unpack_from('>%dI' % nrel, self.raw, 80):
            struct.pack_into('>I', image, off, struct.unpack_from('>I', image, off)[0]+BANK)
        u.mem_map(BANK, (size+0xffff)&~0xffff)
        u.mem_write(BANK, bytes(image))
        for at, n in ((STACK-0x10000, 0x20000), (LIVE, 0x200000), (OBJECT, 0x200000),
                      (META, 0x400000), (RETURN, 0x10000)):
            u.mem_map(at, n)
        self.irq, self.depth, self.next_heap = 0, 0, META
        self.allocations = {}
        self.native = {0x40001980: 'enter', 0x40001aa2: 'leave', 0x400a63ec: 'dest',
                       0x400a6300: 'fxdest', 0x400a666a: 'flags',
                       self.sym['lfo3_memcpy_native']: 'copy',
                       self.sym['lfo3_owned_native_allocate']: 'allocate',
                       self.sym['lfo3_owned_native_release']: 'release'}
        self.query = self.sym['lfo3_shadow_owned_record_body']
        self.forget = self.sym.get('lfo3_publication_forget_owned')
        self.break_invalidation = break_invalidation
        u.hook_add(UC_HOOK_CODE, self.code)
        u.hook_add(UC_HOOK_MEM_READ, self.read_hook)
        self.write(0x402d4fd8, META)
        self.write(0x402d4fe0, META+0x400000)
        for name, value in (('kits_base', LIVE), ('pool_base', POOL), ('lfo3_bank', ENGINE),
                            ('lfo3_budget_context', BUDGET),
                            ('shadow_query_callback', self.query),
                            ('shadow_enter_callback', self.sym['lfo3_writer_enter']),
                            ('shadow_leave_callback', self.sym['lfo3_writer_leave']),
                            ('shadow_relocate_callback', self.sym['lfo3_shadow_owner_relocate_body'])):
            self.write(self.sym[name], value)
        self.call('lfo3_budget_init', [BUDGET, 0x800000])
        self.call('lfo3_records_init', [])

    def write(self, at, value):
        self.u.mem_write(at, struct.pack('>I', value))

    def read(self, at, n):
        return bytes(self.u.mem_read(at, n))

    def read_hook(self, u, access, at, n, value, user):
        if self.forget and self.active == 'lfo3_republish' and META <= at < META+0x400000:
            assert self.depth > 0, 'publication metadata read outside writer mutex'
        if self.forget and self.active == 'lfo3_republish' and OBJECT+0x18000 <= at < OBJECT+0x19618:
            assert self.depth == 0, 'whole-Kit hash held the owner mutex'

    def words(self, n):
        return struct.unpack('>%dI' % (n+1), self.read(self.u.reg_read(UC_M68K_REG_A7), 4*(n+1)))

    def ret(self, value):
        w = self.words(0)
        self.u.reg_write(UC_M68K_REG_D0, value)
        self.u.reg_write(UC_M68K_REG_A7, self.u.reg_read(UC_M68K_REG_A7)+4)
        self.u.reg_write(UC_M68K_REG_PC, w[0])

    def code(self, u, at, size, user):
        self.instructions += 1
        if at == self.sym['lfo3_irq_mask']:
            self.counts['irq'] += 1
        if at == self.query:
            self.counts['owner_queries'] += 1
        if at == self.forget:
            self.counts['invalidations'] += 1
            assert self.depth > 0, 'owner mutation invalidated outside writer mutex'
            if self.break_invalidation:
                self.ret(0)
                return
        name = self.native.get(at)
        if not name:
            return
        self.counts[name] += 1
        value = 0
        if name == 'enter':
            assert not self.irq
            self.depth += 1
        elif name == 'leave':
            assert self.depth > 0
            self.depth -= 1
        elif name == 'copy':
            _, dst, src, n = self.words(3)
            u.mem_write(dst, self.read(src, n))
            value = dst
        elif name == 'allocate':
            n = self.words(1)[1]
            block = 16
            while block < n:
                block *= 2
            value = self.next_heap
            self.next_heap += block
            assert self.next_heap <= META+0x400000
            self.allocations[value] = block
            u.mem_write(value, bytes(block))
        elif name == 'release':
            pointer = self.words(1)[1]
            u.mem_write(pointer, b'\xcd'*self.allocations.pop(pointer))
        elif name in ('dest', 'fxdest'):
            args = self.words(2 if name == 'dest' else 1)[1:]
            self.trace.append((name, *args))
            value = 0x100+args[0]
        elif name == 'flags':
            self.trace.append((name, self.words(1)[1]))
            value = 0x200
        self.ret(value)

    def call(self, name, args):
        self.active = name
        self.instructions, self.counts, self.trace = 0, Counter(), []
        args = [x & 0xffffffff for x in args]
        # Execute the real SR/AND probe, including CCR consumed by assembly.
        # Set the mode before installing this call's active stack pointer.
        self.u.reg_write(UC_M68K_REG_SR, 0x2000|self.irq)
        for i, reg in enumerate(SAVED):
            self.u.reg_write(reg, 0x12340000+i)
        sp = STACK-4*(len(args)+1)
        self.u.mem_write(sp, struct.pack('>%dI' % (len(args)+1), RETURN, *args))
        self.u.reg_write(UC_M68K_REG_A7, sp)
        depth = self.depth
        self.u.emu_start(self.sym[name], RETURN, count=2000000)
        assert self.u.reg_read(UC_M68K_REG_PC) == RETURN, name
        assert self.u.reg_read(UC_M68K_REG_A7) == sp+4, name
        assert [self.u.reg_read(r) for r in SAVED] == [0x12340000+i for i in range(len(SAVED))], name
        expected_depth = depth+(1 if name == 'lfo3_writer_enter' else -1 if name == 'lfo3_writer_leave' else 0)
        assert self.depth == expected_depth, (name, self.depth, expected_depth)
        return self.u.reg_read(UC_M68K_REG_D0), dict(instructions=self.instructions, **self.counts)

    def snapshot(self):
        return (self.read(self.local['frame'], self.frame_bytes), self.read(ENGINE, 944),
                self.read(self.sym['lfo3_params_dirty'], 4), self.read(self.local['dest_raw'], 26),
                self.read(self.sym['kit_shadow'], 128*13*16), self.read(self.sym['pool_shadow'], 128*16),
                self.read(self.sym['side'], 8*228), self.read(self.sym['stats'], 28), tuple(self.trace),
                self.read(OBJECT, 0x20000), self.read(META, self.next_heap-META))

    def record(self, at, values):
        self.u.mem_write(at, struct.pack('>8H', *values))

    def owner(self, at, n=0x1618):
        self.call('lfo3_writer_enter', [])
        assert self.call('lfo3_shadow_owner_allocate', [at, n])[0] == 1
        p = self.call('lfo3_shadow_owned_record', [at, n, SCRATCH, 1])[0]
        assert p
        self.call('lfo3_writer_leave', [])
        return p

    def publish(self, source, track):
        self.call('lfo3_publication_begin', [])
        return self.call('lfo3_publication_end', [source, track, track == 12, 1])


def exercise(old, new):
    rows = []
    def check(label, name, args, compare_return=False):
        a, ac = old.call(name, args)
        b, bc = new.call(name, args)
        if compare_return:
            assert a == b, (label, a, b)
        assert old.snapshot() == new.snapshot(), label
        rows.append(dict(case=label, before=ac, after=bc))
        return ac, bc
    def write(at, data):
        for m in (old, new):
            m.u.mem_write(at, data)
    def rec_pair(at, values):
        for m in (old, new):
            m.record(at, values)
    values = [0x7000, 0x300, 0x4000, 0x100, 0, 0, 0, 0x4000]
    check('empty', 'lfo3_republish', [])
    for m in (old, new):
        for t in range(13):
            m.record(m.sym['kit_shadow']+16*t, values)
            source = LIVE+(0xdce if t == 12 else 0x2a+t*0x123)
            if t != 12:
                m.u.mem_write(source+0xa2, bytes([t]))
            m.publish(source, t)
    check('all-live', 'lfo3_republish', [])
    # Populate a real allocation owner and its Kit association, then publish it.
    records = [m.owner(OBJECT) for m in (old, new)]
    assert records[0] == records[1]
    for m, p in zip((old, new), records):
        for t in range(13):
            m.record(p+16*t, values)
            source = OBJECT+(0xdce if t == 12 else 0x2a+t*0x123)
            if t != 12:
                m.u.mem_write(source+0xa2, bytes([t+1]))
            m.publish(source, t)
    check('owned-cold', 'lfo3_republish', [])
    ac, bc = check('owned-warm', 'lfo3_republish', [])
    assert ac['owner_queries'] == 13 and bc.get('owner_queries', 0) == 0
    assert ac['enter'] == 13 and bc['enter'] == 1
    assert bc['instructions'] < ac['instructions']
    # A new overlapping owner hides the old association before creating cells.
    # This specifically needs invalidation at owner allocation, not just create.
    for m in (old, new):
        m.call('lfo3_writer_enter', [])
        assert m.call('lfo3_shadow_owner_allocate', [OBJECT, 0x1618])[0] == 1
        m.call('lfo3_writer_leave', [])
    check('overlapping-owner-without-cells', 'lfo3_republish', [])
    for m in (old, new):
        m.call('lfo3_writer_enter', [])
        m.call('lfo3_shadow_owner_release', [OBJECT])
        m.call('lfo3_writer_leave', [])
    check('old-owner-revealed', 'lfo3_republish', [])
    check('old-owner-revealed-warm', 'lfo3_republish', [])
    for i in range(10):
        values[0] = 0x7000-i*128
        rec_pair(records[0]+16*(i%13), values)
        if i < 8:
            write(OBJECT+0x2a+i*0x123+0xa2, bytes([20+i]))
        check('owned-value-and-type-'+str(i), 'lfo3_republish', [])
    write(SCRATCH+0x1000, bytes(range(144)))
    ac, bc = check('unrelated-row-copy', 'lfo3_memcpy_entry', [SCRATCH+0x2000, SCRATCH+0x1000, 144], True)
    assert ac['owner_queries'] == 13 and bc.get('owner_queries', 0) == 0
    assert ac['enter'] == 13 and bc['enter'] == 1
    # Copies that create/delete owner associations invalidate before reuse.
    for m in (old, new):
        m.call('lfo3_shadow_owned_record', [OBJECT, 0x1618, SCRATCH, 2])
    check('association-deleted', 'lfo3_republish', [])
    for m in (old, new):
        p = m.call('lfo3_shadow_owned_record', [OBJECT, 0x1618, SCRATCH, 1])[0]
        for t in range(13):
            m.record(p+16*t, values)
    check('association-recreated', 'lfo3_republish', [])
    check('association-recreated-warm', 'lfo3_republish', [])
    # Free metadata, poison it, and republish before allocating at the same key.
    for m in (old, new):
        m.call('lfo3_writer_enter', [])
        m.call('lfo3_shadow_owner_release', [OBJECT])
        m.call('lfo3_writer_leave', [])
    check('owner-freed-poisoned', 'lfo3_republish', [])
    for m in (old, new):
        p = m.owner(OBJECT)
        for t in range(13):
            m.record(p+16*t, values)
    check('owner-address-reused', 'lfo3_republish', [])
    # True native copy of a recognized Sound: execute all real shadow work.
    ac, bc = check('known-sound-copy', 'lfo3_memcpy_entry', [OBJECT+0x2a, LIVE+0x2a, 0x123], True)
    assert ac.get('owner_queries', 0) >= 13 and bc.get('owner_queries', 0) < ac['owner_queries'], (ac, bc)
    check('after-known-sound-copy', 'lfo3_republish', [])
    # Pool source and changed values must also be seen; it is never memoized.
    for m in (old, new):
        m.record(m.sym['pool_shadow'], values)
        m.u.mem_write(POOL+0xa2, b'\x03')
        m.publish(POOL, 0)
    check('pool-source', 'lfo3_republish', [])
    # Relocation uses real owner/cell logic and keeps the mutex balanced.
    for m in (old, new):
        m.owner(OBJECT+0x10000)
        m.u.mem_write(OBJECT+0x10000, m.read(OBJECT, 0x1618))
    check('owner-relocation', 'lfo3_shadow_owner_relocate', [OBJECT+0x10000, OBJECT, 0x1618])
    for m in (old, new):
        for t in range(13):
            m.publish(OBJECT+0x10000+(0xdce if t == 12 else 0x2a+t*0x123), t)
    check('relocated-publish', 'lfo3_republish', [])
    # Foreign stack/clipboard Kit: real side table and whole-Kit hash checks.
    foreign, source = OBJECT+0x18000, LIVE
    write(foreign, old.read(source, 0x1618))
    check('side-kit-copy', 'lfo3_memcpy_entry', [foreign, source, 0x1618], True)
    for m in (old, new):
        for t in range(13):
            m.publish(foreign+(0xdce if t == 12 else 0x2a+t*0x123), t)
    ac, bc = check('side-kit-valid', 'lfo3_republish', [])
    assert ac['enter'] == bc['enter'] == 13
    # A small copy bypasses shadow work but invalidates the Kit's hash.
    write(SCRATCH+0x3000, b'\x91'*16)
    check('small-copy-stales-side-hash', 'lfo3_memcpy_entry', [foreign+4, SCRATCH+0x3000, 16], True)
    # Therefore a later unrelated large copy MUST still revalidate all sources.
    check('unrelated-copy-after-stale-hash', 'lfo3_memcpy_entry', [SCRATCH+0x2000, SCRATCH+0x1000, 144], True)
    assert old.read(old.local['frame']+52, 4) == bytes(4)
    check('side-kit-invalid', 'lfo3_republish', [])
    for m in (old, new):
        m.irq = 0x500
    ac, bc = check('irq-republish-bypass', 'lfo3_republish', [])
    assert not ac.get('enter') and not bc.get('enter')
    check('irq-copy-bypass', 'lfo3_memcpy_entry', [SCRATCH+0x2000, SCRATCH+0x1000, 144], True)
    for m in (old, new):
        m.irq = 0
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('before', 'after', 'stock', 'core', 'out'):
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    rows, setter_cases = [], 0
    for label, image in (('stock', syx.Syx(a.stock.read_bytes()).section(3)), ('core', a.core.read_bytes())):
        old, new = Runtime(image, a.before), Runtime(image, a.after)
        assert old.report['source_sha256'].keys() == new.report['source_sha256'].keys()
        changed = {Path(k).name for k, v in old.report['source_sha256'].items() if new.report['source_sha256'][k] != v}
        assert changed == {'ui.c', 'shadow.c', 'owners.c'}, changed
        result = exercise(old, new)
        rows += [dict(image=label, **r) for r in result]
        print(f'PASS {label}: {len(result)} publication/copy/lifetime comparisons; real owner backend', flush=True)
        # The previous delivery's lazy-census setter must survive this change.
        from test_st_lfo3_setter_emu import Setter, cases as lock_cases
        old_setter, new_setter = Setter(image, a.before), Setter(image, a.after)
        for index, case in enumerate(lock_cases()):
            before_state, before_cost = old_setter.execute(case)
            after_state, after_cost = new_setter.execute(case)
            assert before_state == after_state, (label, 'setter', index, case)
            assert before_cost['flag_reads'] == after_cost['flag_reads'], (label, 'setter census', index)
            setter_cases += 1
        print(f'PASS {label}: lazy-census setter behavior and flag-read counts retained', flush=True)
    # Negative control: the differential must reject a disabled invalidator.
    native = syx.Syx(a.stock.read_bytes()).section(3)
    try:
        exercise(Runtime(native, a.before), Runtime(native, a.after, break_invalidation=True))
    except AssertionError as error:
        assert str(error) in ('overlapping-owner-without-cells', 'association-deleted', 'association-recreated', 'owner-freed-poisoned'), str(error)
        mutant = str(error)
    else:
        raise AssertionError('disabled invalidator escaped the lifetime differential')
    report = dict(ok=True, hardware=False, cases=len(rows), setter_cases=setter_cases, negative_control_detected=mutant,
                  before_sha256=hashlib.sha256(a.before.read_bytes()).hexdigest(),
                  after_sha256=hashlib.sha256(a.after.read_bytes()).hexdigest(),
                  limitations='Native allocator/mutex/copy/descriptor are boundary doubles. No boot, audio render, DMA, cache timing or scheduler contention.',
                  samples=rows)
    a.out.write_text(json.dumps(report, indent=2)+'\n')
    print('PASS: disabled invalidator detected; no device access', flush=True)


if __name__ == '__main__':
    main()
