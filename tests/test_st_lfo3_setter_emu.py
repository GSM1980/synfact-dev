"""Differential: compiled LFO3 setter before/after lazy native-lane census.

Run with the Unicorn environment in docs/SYNTAKT-BUILD.md. Both banks execute
their real wrapper and the real stock/core native setter. Model access, mutex,
ownership lookup and sparse backend are identical explicit boundary doubles.
Compare results, matrix/store bytes, busy state, callback order/arguments,
callee-saved registers and SP. Counts are instructions, not hardware cycles.
"""
import argparse
import hashlib
import json
import random
import struct
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT/'tools'))
from st_ui_cost_study import Machine, BASE, STACK, MODEL, VTABLE, DATA, COMPRESSED, RETURN, ACCESS, NOTIFY, STRIDE
from elekloader import syx
from elekloader.sdk import elf
from unicorn.m68k_const import *

BANK_BASE, STORE, CONTEXT = 0x47000000, MODEL+0x2000, MODEL+0x3000
SAVED = [UC_M68K_REG_D2, UC_M68K_REG_D3, UC_M68K_REG_D4, UC_M68K_REG_D5,
         UC_M68K_REG_D6, UC_M68K_REG_D7, UC_M68K_REG_A2, UC_M68K_REG_A3,
         UC_M68K_REG_A4, UC_M68K_REG_A5, UC_M68K_REG_A6]


class Setter(Machine):
    def __init__(self, native, bank):
        super().__init__(native)
        self.raw = bank.read_bytes()
        self.report = json.loads(Path(str(bank)+'.json').read_text())
        e = elf.Elf.load(bank.parent/(bank.stem+'.work')/'main.elf')
        self.sym = {s.name: BANK_BASE+s.value for s in e.symbols if s.name and s.bind == 1}
        size, nrel = struct.unpack_from('>2I', self.raw, 52)
        image = bytearray(self.raw[80+4*nrel:80+4*nrel+size])
        for off in struct.unpack_from('>%dI' % nrel, self.raw, 80):
            struct.pack_into('>I', image, off, struct.unpack_from('>I', image, off)[0]+BANK_BASE)
        self.uc.mem_map(BANK_BASE, (size+0xffff)&~0xffff)
        self.uc.mem_write(BANK_BASE, bytes(image))
        self.stub_names = {ACCESS: 'model', NOTIFY: 'notify'}
        for name in ('writer_enter', 'writer_leave', 'owned_lookup', 'owned_set'):
            self.stub_names[self.sym['lfo3_'+name]] = name
        self.observe = {self.sym['lfo3_pattern_native_set']: ('native_set', 5),
                        self.sym['lfo3_publication_begin']: ('begin', 0),
                        self.sym['lfo3_publication_end']: ('end', 4)}

    def words(self, n):
        sp = self.uc.reg_read(UC_M68K_REG_A7)
        return struct.unpack('>%dI' % (n+1), bytes(self.uc.mem_read(sp, 4*(n+1))))

    def code(self, u, at, size, user):
        self.count += 1
        if at in self.observe:
            name, n = self.observe[at]
            self.trace.append((name, *self.words(n)[1:]))
        name = self.stub_names.get(at)
        if not name:
            return
        n = {'model': 1, 'notify': 2, 'writer_enter': 0, 'writer_leave': 0,
             'owned_lookup': 1, 'owned_set': 6}[name]
        w = self.words(n)
        args = w[1:]
        result = 0
        if name == 'model':
            result = DATA
        elif name == 'owned_lookup':
            assert args == (DATA,)
            result = STORE if self.case['recognized'] else 0
        elif name == 'owned_set':
            assert args[0] == STORE
            result = self.case['own_ok']
            if result:
                u.mem_write(STORE+16, struct.pack('>6I', *args))
        elif name == 'notify':
            assert args[0] == MODEL
            args = (args[0], *struct.unpack('>3I', bytes(u.mem_read(args[1], 12))))
        self.trace.append((name, *args))
        self.stub_counts[name] += 1
        u.reg_write(UC_M68K_REG_D0, result)
        u.reg_write(UC_M68K_REG_A7, u.reg_read(UC_M68K_REG_A7)+4)
        u.reg_write(UC_M68K_REG_PC, w[0])

    def execute(self, case):
        self.case = case
        self.pattern(case['native_lanes'], steps=case['steps'])
        self.uc.mem_write(STORE, struct.pack('>IHH', 0, case['own_lanes'], 0)+bytes(48))
        self.uc.mem_write(CONTEXT, struct.pack('>2I', 0, case['busy'])+bytes(128))
        self.uc.mem_write(self.sym['lfo3_pattern_context'], struct.pack('>I', CONTEXT))
        self.count, self.flag_reads = 0, 0
        self.trace, self.stub_counts = [], Counter()
        for i, reg in enumerate(SAVED):
            self.uc.reg_write(reg, 0x12340000+i)
        args = [MODEL, case['track'], case['step'], case['slot'], case['value'] & 0xffffffff]
        sp = STACK-4*(len(args)+1)
        self.uc.mem_write(sp, struct.pack('>6I', RETURN, *args))
        self.uc.reg_write(UC_M68K_REG_A7, sp)
        self.uc.emu_start(self.sym['lfo3_pattern_set'], RETURN, count=200000)
        assert self.uc.reg_read(UC_M68K_REG_PC) == RETURN
        assert self.uc.reg_read(UC_M68K_REG_A7) == sp+4
        assert [self.uc.reg_read(r) for r in SAVED] == [0x12340000+i for i in range(len(SAVED))]
        assert self.stub_counts['writer_enter'] == self.stub_counts['writer_leave'] == 1
        assert self.stub_counts['owned_lookup'] == 1
        assert struct.unpack('>I', bytes(self.uc.mem_read(CONTEXT+4, 4)))[0] == case['busy']
        state = (self.uc.reg_read(UC_M68K_REG_D0), bytes(self.uc.mem_read(DATA, 13*STRIDE)),
                 bytes(self.uc.mem_read(STORE, 56)), tuple(self.trace))
        return state, {'instructions': self.count, 'flag_reads': self.flag_reads}


def cases():
    result = []
    default = dict(recognized=True, own_lanes=0, native_lanes=1, steps=64,
                   busy=0, track=0, step=7, slot=0, value=0x3400, own_ok=1)
    def add(**changes):
        result.append(dict(default, **changes))
    # Every capacity boundary, existing/new native lane, no sparse lanes,
    # and the original rejection rule even for malformed native edits.
    for n in (0, 1, 76, 77, 78, 79, 80):
        for own in (0, 1, 2, 79, 80):
            for slot in (0, 20):
                add(native_lanes=n, own_lanes=own, slot=slot)
    # Sparse controls retain their full census/backend/publication protocol.
    for slot in range(0x58, 0x60):
        for busy, ok in ((0, 0), (0, 1), (1, 1)):
            add(slot=slot, busy=busy, own_ok=ok, own_lanes=1, native_lanes=78)
    for recognized in (False, True):
        for track, step, slot in ((12, 63, 70), (13, 7, 0), (0xffffffff, 7, 0),
                                 (0, 64, 0), (0, 0xffffffff, 0), (0, 7, 71),
                                 (0, 7, 87), (0, 7, 96), (0, 7, 0xffffffff)):
            add(recognized=recognized, track=track, step=step, slot=slot, own_lanes=1)
    rng = random.Random(20261028)
    for _ in range(180):
        add(recognized=rng.choice((False, True)), own_lanes=rng.choice((0, 1, 2, 79, 80)),
            native_lanes=rng.choice((0, 1, 16, 64, 77, 78, 79, 80)), steps=rng.choice((1, 16, 64)),
            busy=rng.choice((0, 1)), track=rng.choice((0, 1, 12, 13, 0xffffffff)),
            step=rng.choice((0, 7, 63, 64, 0xffffffff)), slot=rng.choice((0, 1, 54, 70, 71, 87, 88, 91, 95, 96)),
            value=rng.choice((-1, 0, 0x1234, 0x7f00)), own_ok=rng.choice((0, 1)))
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('before', 'after', 'stock', 'core', 'out'):
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    rows, sample = [], cases()
    images = [('stock', syx.Syx(a.stock.read_bytes()).section(3)), ('core', a.core.read_bytes())]
    for label, native in images:
        before, after = Setter(native, a.before), Setter(native, a.after)
        assert before.report['source_sha256'].keys() == after.report['source_sha256'].keys()
        changed_sources = [k for k, v in before.report['source_sha256'].items() if after.report['source_sha256'].get(k) != v]
        assert [Path(k).name for k in changed_sources] == ['pattern.c'], changed_sources
        old_a, old_n = struct.unpack_from('>2I', before.raw, 72)
        new_a, new_n = struct.unpack_from('>2I', after.raw, 72)
        assert before.raw[48+old_a:48+old_a+old_n] == after.raw[48+new_a:48+new_a+new_n]
        for i, case in enumerate(sample):
            old, old_cost = before.execute(case)
            new, new_cost = after.execute(case)
            assert old == new, (label, i, case, old[0], new[0], old[3], new[3])
            if not case['recognized']:
                expected_reads = 0
            elif 0x58 <= case['slot'] <= 0x5f:
                expected_reads = 923
            elif case['track'] < 13 and case['slot'] < 71 and case['own_lanes']:
                used = case['native_lanes'] > 13*case['slot']+case['track']
                expected_reads = 1 if used else 924
            else:
                expected_reads = 0
            assert new_cost['flag_reads'] == expected_reads, (label, i, case, new_cost)
            if not i:
                assert old_cost['flag_reads'] == 923 and new_cost['flag_reads'] == 0
                assert old_cost['instructions']-new_cost['instructions'] > 4000
            rows.append(dict(image=label, case=case, before=old_cost, after=new_cost))
            if i % 100 == 0:
                print(f'{label}: {i}/{len(sample)} cases match', flush=True)
        print(f'PASS {label}: {len(sample)} cases, output/state/callbacks/registers/SP identical', flush=True)
    report = dict(ok=True, cases=len(rows), hardware=False,
                  before_sha256=hashlib.sha256(a.before.read_bytes()).hexdigest(),
                  after_sha256=hashlib.sha256(a.after.read_bytes()).hexdigest(),
                  limitations='Mutex, ownership lookup, sparse backend and model notifications are boundary doubles. No boot, IRQ, cache or physical timing.',
                  samples=rows)
    a.out.write_text(json.dumps(report, indent=2)+'\n')
    print('PASS: only generated pattern.c changed among bank inputs; AUDIO bank identical; no device access')


if __name__ == '__main__':
    main()
