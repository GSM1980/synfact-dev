"""Linked keyboard fast/changed-path differential, including malformed lists.

Uses original producer fixture boundaries; no device I/O or timing claims.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tests')]
from test_st_arp_generator_emu import Machine, W


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def native(notes):
    data = bytearray(0x420)
    mask = [0] * 4
    for i, note in enumerate(notes):
        mask[note >> 5] |= 1 << (note & 31)
        previous = W + 0x2000 + 16 + notes[i - 1] * 8 if i else 0
        struct.pack_into('>II', data, 16 + note * 8, 0, previous)
    struct.pack_into('>4I', data, 0, *mask)
    struct.pack_into('>I', data, 0x410, W + 0x2010 + notes[-1] * 8 if notes else 0)
    return data


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for n in ('core', 'before', 'after', 'out'):
        p.add_argument('--' + n, type=Path, required=True)
    a = p.parse_args()
    machines = [Machine(a.core, bank) for bank in (a.before, a.after)]
    rng = random.Random(0x415250)
    sets = [[], [0], [31], [32], [63], [64], [95], [96], [127],
            [67, 60, 64], list(range(128))]
    cases = [(old, new, None) for old in sets for new in sets]
    for i in range(64):
        old = rng.sample(range(128), rng.randrange(129))
        new = old[:] if i % 3 == 0 else rng.sample(range(128), rng.randrange(129))
        cases.append((old, new, None))
    # Changed masks must reject malformed lists without altering the selector.
    for defect in ('low_head', 'high_head', 'misaligned_head', 'cycle', 'missing_mask', 'unlisted_mask'):
        cases.append(([60], [60, 64, 67], defect))
    # With identical masks the existing contract ignores list/order changes.
    cases += [([60, 64], [64, 60], None), ([60], [60], 'low_head')]
    rows = []
    for index, (old, new, defect) in enumerate(cases):
        results, counts, states = [], [], []
        data = native(new)
        if defect in ('low_head', 'high_head', 'misaligned_head'):
            head = {'low_head': W + 0x200c, 'high_head': W + 0x2410,
                    'misaligned_head': W + 0x2011}[defect]
            struct.pack_into('>I', data, 0x410, head)
        elif defect == 'cycle':
            struct.pack_into('>I', data, 16 + new[-1] * 8 + 4, W + 0x2010 + new[-1] * 8)
        elif defect == 'missing_mask':
            # Remove note 64 but retain 67: the mask must still differ from
            # the old [60] set, otherwise the documented fast exit applies.
            struct.pack_into('>I', data, 8, struct.unpack_from('>I', data, 8)[0] & ~1)
        elif defect == 'unlisted_mask':
            struct.pack_into('>I', data, 0, 1)
        for m in machines:
            m.call('arp_reset', W + 0x1800, 1)
            for note in old:
                m.call('arp_hold', W + 0x1800, note)
            # Preserve nontrivial selection history across held-set changes.
            m.u.mem_write(W + 0x100, struct.pack('>6BH16b', 1, 9, 3, 30, 15, 0, 0xffff, *([0] * 16)))
            for _ in range(3):
                m.call('arp_next', W + 0x1800, W + 0x100, 1)
            before = m.read(W + 0x1800, 180)
            m.u.mem_write(W + 0x2000, bytes(data))
            m.count = 0
            result = m.call('arp_keyboard_sync', W + 0x1800, W + 0x2000)
            results.append(result)
            counts.append(m.count)
            states.append(m.read(W + 0x1800, 180))
            assert m.read(W + 0x2000, len(data)) == bytes(data), 'native list modified'
            if result == 0xffffffff:
                assert states[-1] == before, 'invalid list mutated selector'
        assert results[0] == results[1] and states[0] == states[1], (index, old, new, defect)
        assert counts[1] <= counts[0], ('keyboard instruction regression', index, counts)
        if defect and set(old) != set(new):
            assert results[0] == 0xffffffff, (defect, results)
        rows.append(dict(case=index, old_count=len(old), new_count=len(new),
                         unchanged_mask=set(old) == set(new), defect=defect,
                         result=results[0], before=counts[0], after=counts[1]))
    # A deliberately inconsistent mask/list must be caught (negative control).
    assert any(r['defect'] == 'unlisted_mask' and r['result'] == 0xffffffff for r in rows)
    output = dict(ok=True, cases=rows, before_sha256=sha(a.before), after_sha256=sha(a.after),
                  fixture_sha256=sha(Path(__file__)), device_access=False,
                  limits=['Instructions, not cycles; native exclusion keeps the snapshot stable.',
                          'Selector state and ABI checked with real linked keyboard implementations.'])
    a.out.write_text(json.dumps(output, indent=2) + '\n')
    print('PASS:', len(rows), 'keyboard comparisons; all selector bytes/ABI and invalid-list rejection')
    for group in (True, False):
        selected = [r for r in rows if r['unchanged_mask'] == group]
        print('unchanged' if group else 'changed', 'count ranges',
              (min(r['before'] for r in selected), max(r['before'] for r in selected)), '->',
              (min(r['after'] for r in selected), max(r['after'] for r in selected)),
              'increases', sum(r['after'] > r['before'] for r in selected))


if __name__ == '__main__':
    main()
