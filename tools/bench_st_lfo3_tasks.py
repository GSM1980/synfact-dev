"""Measure real LFO3 publication and lock setters with explicit native doubles.

These are task/event operations, not work performed on every audio block.
Reuses the existing differential fixtures without changing their historical
acceptance checks. No USB/MIDI and no native allocation or mutex execution.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tests')]
from test_st_lfo3_publication_emu import Runtime, LIVE, OBJECT, SCRATCH
from test_st_lfo3_setter_emu import Setter


def measure_publication(native, path):
    m = Runtime(native, path)
    rows = []
    values = [0x7000, 0x300, 0x4000, 0x100, 0, 0, 0, 0x4000]

    def record(label, fn='lfo3_republish', args=()):
        result, cost = m.call(fn, list(args))
        rows.append({'case': label, 'result': result, **cost})

    record('empty')
    for t in range(13):
        m.record(m.sym['kit_shadow'] + 16 * t, values)
        src = LIVE + (0xdce if t == 12 else 0x2a + t * 0x123)
        if t != 12:
            m.u.mem_write(src + 0xa2, bytes([t]))
        m.publish(src, t)
    record('live_13_sources')
    record('live_unchanged_repeat')
    owned = m.owner(OBJECT)
    for t in range(13):
        m.record(owned + 16 * t, values)
        src = OBJECT + (0xdce if t == 12 else 0x2a + t * 0x123)
        if t != 12:
            m.u.mem_write(src + 0xa2, bytes([t + 1]))
        m.publish(src, t)
    record('owned_cold')
    record('owned_warm')
    m.record(owned, [0x7100, *values[1:]])
    record('owned_one_value_changed')
    m.u.mem_write(SCRATCH + 0x1000, bytes(range(144)))
    record('unrelated_row_copy', 'lfo3_memcpy_entry',
           (SCRATCH + 0x2000, SCRATCH + 0x1000, 144))
    stack = OBJECT + 0x18000
    m.call('lfo3_memcpy_entry', [stack, LIVE, 0x1618])
    for t in range(13):
        m.publish(stack + (0xdce if t == 12 else 0x2a + t * 0x123), t)
    record('side_kit_hashes')
    record('side_kit_hashes_repeat')
    m.u.mem_write(stack, b'\xa5')
    record('side_kit_hash_stale')
    return rows, m.snapshot()


def measure_setters(native, path):
    m = Setter(native, path)
    base = dict(recognized=1, own_lanes=0, native_lanes=0, steps=64,
                busy=0, track=0, step=0, slot=54, value=0x1234, own_ok=1)
    cases = [('native_no_sparse', {}),
             ('native_existing_lane_with_sparse', {'own_lanes': 1, 'native_lanes': 1, 'slot': 0}),
             ('native_new_lane_capacity_check', {'own_lanes': 1, 'native_lanes': 78}),
             ('sparse_LFO3_write', {'slot': 0x58, 'own_lanes': 1, 'native_lanes': 1}),
             ('sparse_ARP_write', {'slot': 0x60, 'own_lanes': 1, 'native_lanes': 1})]
    rows, states = [], []
    for name, changes in cases:
        state, cost = m.execute(dict(base, **changes))
        rows.append({'case': name, **cost})
        states.append(state)
    return rows, states


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core', type=Path, required=True)
    p.add_argument('--before', type=Path, required=True)
    p.add_argument('--after', type=Path)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    native = (a.core / 'main.bin').read_bytes()
    after = a.after or a.before
    publication, state0 = measure_publication(native, a.before)
    candidate, state1 = measure_publication(native, after)
    assert state0 == state1, 'publication/lifetime/state difference'
    setters, states0 = measure_setters(native, a.before)
    candidate_setters, states1 = measure_setters(native, after)
    assert states0 == states1, 'setter output/state/ABI difference'
    for label, before_rows, after_rows in [('publication', publication, candidate),
                                           ('setters', setters, candidate_setters)]:
        for old, new in zip(before_rows, after_rows):
            print(label, old['case'], old['instructions'], '->', new['instructions'], flush=True)
    output = {'ok': True, 'before_sha256': hashlib.sha256(a.before.read_bytes()).hexdigest(),
              'after_sha256': hashlib.sha256(after.read_bytes()).hexdigest(),
              'publication': {'before': publication, 'after': candidate},
              'setters': {'before': setters, 'after': candidate_setters},
              'limits': ['Task/event costs, not per-block costs; do not add to the engine count.',
                         'Real bank publication, owner, hash and setter code.',
                         'Native allocation, copy, mutex, descriptors and model services are doubles.',
                         'No complete scheduler, hardware timing or device access.']}
    a.out.parent.mkdir(exist_ok=True, parents=True)
    a.out.write_text(json.dumps(output, indent=2) + '\n')


if __name__ == '__main__':
    main()
