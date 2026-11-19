"""Compare every linked selector-state byte across histories and mode changes.

The native Digitone oracle is a separate regression. No device I/O or cycles.
"""
import argparse
import collections
import hashlib
import itertools
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


def settings(mode, rg, mask, length, offsets):
    return struct.pack('>6BH16b', mode, 9, rg, 30, length, 0, mask, *offsets)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for n in ('core', 'before', 'after', 'out'):
        p.add_argument('--' + n, type=Path, required=True)
    a = p.parse_args()
    machines = [Machine(a.core, bank) for bank in (a.before, a.after)]
    rng = random.Random(0x41525012)
    sets = [[], [60], [60, 64, 67], [67, 60, 64],
            [0, 31, 32, 63, 64, 95, 96, 127], list(range(128))]
    rows, increases, calls = [], [], 0

    def reset(notes):
        for m in machines:
            m.call('arp_reset', W + 0x1800, 1)
            for n in notes:
                m.call('arp_hold', W + 0x1800, n)

    def select(payload, generation, label):
        nonlocal calls
        results, states, costs = [], [], []
        for m in machines:
            m.u.mem_write(W + 0x100, payload)
            m.count = 0
            results.append(m.call('arp_next', W + 0x1800, W + 0x100, generation))
            costs.append(m.count)
            states.append(m.read(W + 0x1800, 180))
            assert m.read(W + 0x100, 24) == payload, 'settings modified'
        assert results[0] == results[1] and states[0] == states[1], (label, results, states)
        if costs[1] > costs[0]:
            increases.append(dict(case=label, before=costs[0], after=costs[1]))
        calls += 1
        return costs

    for case, (mode, notes, rg, mask, length) in enumerate(itertools.product(
            range(5), sets, range(7), (0, 0xffff, 0xa55a), (0, 15))):
        reset(notes)
        payload = settings(mode, rg, mask, length, [rng.randrange(-24, 25) for _ in range(16)])
        costs = [select(payload, 1, [case, step]) for step in range(32)]
        select(payload, 2, [case, 'stale'])
        rows.append(dict(case=case, mode=mode, notes=len(notes), range=rg,
                         mask=mask, length=length, before=sum(c[0] for c in costs),
                         after=sum(c[1] for c in costs)))
    print('PASS:', len(rows), 'configurations, full state and stale generation', flush=True)

    # Preserve history while switching mode/range, adding/removing notes,
    # changing rest/length/offset patterns and hitting all bitmap boundaries.
    reset([0, 31, 32, 63, 64, 95, 96, 127])
    for step in range(1024):
        if step % 7 == 0:
            note = rng.randrange(128)
            operation = 'arp_hold' if step & 1 else 'arp_release'
            results = [m.call(operation, W + 0x1800, note) for m in machines]
            assert results[0] == results[1]
        payload = settings(step % 5, (step // 5) % 7,
                           (0, 0xffff, 0xa55a, 0x8001)[(step // 3) % 4],
                           (0, 1, 7, 15, 16, 255)[(step // 9) % 6],
                           [rng.randrange(-128, 128) for _ in range(16)])
        select(payload, 2 if step % 13 == 0 else 1, ['history', step])
    # Deliberate state corruption must be visible to the same byte comparison.
    before = machines[0].read(W + 0x1800, 180)
    corrupted = bytearray(machines[1].read(W + 0x1800, 180))
    assert before == bytes(corrupted)
    corrupted[176] ^= 1
    machines[1].u.mem_write(W + 0x1800, bytes(corrupted))
    assert before != machines[1].read(W + 0x1800, 180)
    report = dict(ok=not increases, configurations=len(rows), calls=calls, histories=1024,
                  negative_control=True, rows=rows, instruction_increases=increases,
                  core_sha256=sha(a.core), before_sha256=sha(a.before), after_sha256=sha(a.after),
                  fixture_sha256=sha(Path(__file__)), device_access=False,
                  limits=['Instructions, not cycles or MAIN percentages.',
                          'Private selector state/ABI; no scheduler or synthesis.',
                          'Native oracle must be run separately.'])
    a.out.write_text(json.dumps(report, indent=2) + '\n')
    print('Calls:', calls, 'instruction increases:', len(increases), flush=True)
    assert not increases, increases[:12]
    for mode in range(5):
        selected = [r for r in rows if r['mode'] == mode]
        print('mode', mode, sum(r['before'] for r in selected), '->', sum(r['after'] for r in selected))


if __name__ == '__main__':
    main()
