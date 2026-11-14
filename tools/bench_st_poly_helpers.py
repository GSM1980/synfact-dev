"""Linked POLY copy, stack-origin and queue edge cases; no device access.

Native enqueue/immediate are read-only recording boundaries in this fixture.
The event differential separately executes the real native pools and queue.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tests')]
from test_st_poly_cost_emu import Runtime, WORK
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_PC, UC_M68K_REG_D0


class Queue(Runtime):
    def __init__(self, bank):
        self.submitted = []
        super().__init__(bank)

    def code(self, u, at, size, user):
        if at in (0x4011a648, 0x4011a722):
            self.count += 1
            sp = u.reg_read(UC_M68K_REG_A7)
            event = struct.unpack('>I', self.read(sp + 4, 4))[0]
            time = struct.unpack('>I', self.read(sp + 8, 4))[0] if at == 0x4011a648 else None
            self.submitted.append((event, time))
            u.reg_write(UC_M68K_REG_PC, struct.unpack('>I', self.read(sp, 4))[0])
            u.reg_write(UC_M68K_REG_A7, sp + 4)
            return
        super().code(u, at, size, user)


def copies(bank):
    m = Runtime(bank)
    rows = []
    for length, symbol, alignments in ((291, 'poly_copy_record', range(4)),
                                        (80, 'poly_copy_static_event', (0,))):
        for sa in alignments:
            for da in alignments:
                src, dst = WORK + 0x104 + sa, WORK + 0x804 + da
                data = bytes((i * 37 + i // 7) % 256 for i in range(length))
                m.u.mem_write(src - 4, b'\xa5' * (length + 8))
                m.u.mem_write(src, data)
                m.u.mem_write(dst - 4, b'\x5a' * (length + 8))
                count = m.execute(symbol, (dst, src))
                assert m.read(src - 4, length + 8) == b'\xa5' * 4 + data + b'\xa5' * 4
                assert m.read(dst - 4, length + 8) == b'\x5a' * 4 + data + b'\x5a' * 4
                rows.append(dict(symbol=symbol, source_alignment=sa,
                                 destination_alignment=da, instructions=count))
    return rows


def origins(bank):
    m = Runtime(bank)
    reads, writes = [], []
    cursor = WORK + 0x1000
    # Unicorn constants keep write/read classification independent of the code.
    from unicorn import UC_MEM_WRITE
    def memory(u, kind, at, size, value, unused):
        if cursor <= at < cursor + 2054:
            (writes if kind == UC_MEM_WRITE else reads).append((at, size))
    m.u.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, memory)
    pcs = (0x4002ed0a, 0x40105408, 0x4002d602, 0x401052c4)
    fixtures = [[]]
    for index in (0, 1, 2, 511, 512, 1022, 1023):
        fixtures.extend([[(index, pc)] for pc in pcs])
    fixtures.extend([[(1, pcs[2]), (20, pcs[0])], [(2, pcs[0]), (100, pcs[3])]])
    rows = []
    for entries in fixtures:
        data = bytearray(b'\xa5' * 2054)
        for index, pc in entries:
            struct.pack_into('>I', data, 2 * index, pc)
        expected, stop = 0, 1023
        for i in range(1024):
            word = struct.unpack_from('>I', data, 2 * i)[0]
            if word in pcs:
                expected, stop = (2 if word in pcs[:2] else 1), i
                break
        m.u.mem_write(cursor, bytes(data))
        reads.clear(); writes.clear()
        count = m.execute('poly_scan_key_origin', (cursor,))
        assert m.u.reg_read(UC_M68K_REG_D0) == expected
        assert not writes and m.read(cursor, len(data)) == bytes(data)
        assert reads == [(cursor + 2 * i + j, 2) for i in range(stop + 1) for j in (0, 2)]
        rows.append(dict(entries=entries, result=expected, instructions=count))
    return rows


def queues(before, after):
    rows = []
    for free in (0, 1):
        for immediate in (0, 1):
            for shape in ('empty', 'immediate', 'other', 'match', 'late_match'):
                for count in (0, 1, 2, 3, 4, 5):
                    states, counts = [], []
                    for bank in (before, after):
                        m = Queue(bank)
                        bucket, events = WORK + 0x2000, WORK + 0x3000
                        entries = {'empty': [], 'immediate': [(1, 0)],
                                   'other': [(0, 12)], 'match': [(0, 77)],
                                   'late_match': [(0, 12), (0, 21), (0, 77)]}[shape]
                        m.w(0x444de434, bucket if entries else 0)
                        m.w(0x444de43c, WORK + 0x4000 if free else 0)
                        for i, (mode, time) in enumerate(entries):
                            m.u.mem_write(bucket + i * 24, struct.pack('>6I', mode, time, 0, 0,
                                bucket + (i + 1) * 24 if i + 1 < len(entries) else 0, 0))
                        pointers = [WORK + 0x5000 + i * 80 for i in range(5)]
                        m.u.mem_write(events, struct.pack('>5I', *pointers))
                        memory = m.read(WORK + 0x2000, 0x4000)
                        cost = m.execute('poly_submit', (events, count, 77, immediate))
                        assert m.read(WORK + 0x2000, 0x4000) == memory
                        states.append((m.u.reg_read(UC_M68K_REG_D0), m.submitted))
                        counts.append(cost)
                    assert states[0] == states[1], (free, immediate, shape, count, states)
                    assert counts[1] <= counts[0], ('queue instruction regression', free, immediate, shape, count, counts)
                    # Invalid counts and exhausted-pool paths must also be measured.
                    rows.append(dict(free=free, immediate=immediate, shape=shape, count=count,
                                     before=counts[0], after=counts[1], accepted=bool(states[0][0])))
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('before', 'after', 'out'):
        p.add_argument('--' + name, required=True, type=Path)
    a = p.parse_args()
    result = dict(copies=copies(a.after), origins=origins(a.after), queues=queues(a.before, a.after),
                  before_sha256=hashlib.sha256(a.before.read_bytes()).hexdigest(),
                  after_sha256=hashlib.sha256(a.after.read_bytes()).hexdigest(),
                  fixture_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                  device_access=False, units='instructions, not cycles or CPU percentages')
    a.out.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS copies/origins/queue semantics:', *(len(result[k]) for k in ('copies', 'origins', 'queues')))
    print('Queue instruction increases:', [r for r in result['queues'] if r['after'] > r['before']])


if __name__ == '__main__':
    main()
