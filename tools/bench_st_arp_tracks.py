"""Linked ARP callback differential for noncontiguous running-track masks."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tests')]
from test_st_arp_analog_emu import AnalogMachine
from test_st_arp_generator_emu import W
from bench_st_arp_main import snapshot


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for n in ('core', 'before', 'after', 'out'):
        p.add_argument('--' + n, type=Path, required=True)
    a = p.parse_args()
    rows = []
    for tracks in ((7,), (11,), (0, 7), (8, 11), (1, 4, 6, 10), (0, 2, 5, 7, 9, 11)):
        for kind in ('live', 'sequence'):
            machines = [AnalogMachine(a.core, bank) for bank in (a.before, a.after)]
            pools = [m.pool_counts() for m in machines]
            for m in machines:
                for t in tracks:
                    assert m.configure(track=t, mode=1 + t % 4, range_=t % 7)
                    if kind == 'live':
                        for note in (48 + t, 52 + t, 55 + t):
                            m.key(note, track=t)
                    else:
                        m.event(note=48 + t, length=0, track=t)
            costs = []
            for when in (0, 28800, 5400000, 5428800, 10800000):
                counts = []
                for m in machines:
                    m.w(0x800046ec, when)
                    m.count = 0
                    m.call('arp_frame')
                    counts.append(m.count)
                    m.drain(when, when + 28800)
                assert snapshot(machines[0]) == snapshot(machines[1]), (tracks, kind, when)
                assert counts[1] <= counts[0], (tracks, kind, when, counts)
                costs.append(dict(begin=when, before=counts[0], after=counts[1]))
            for m, initial in zip(machines, pools):
                m.u.mem_write(W + 0x140, bytes.fromhex('4152503153544f50') + bytes(16))
                m.call(0x4011a722, W + 0x140)
                assert m.pool_counts() == initial
            assert snapshot(machines[0]) == snapshot(machines[1])
            rows.append(dict(tracks=list(tracks), kind=kind, blocks=costs))
            print('PASS sparse tracks', tracks, kind, flush=True)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    result = dict(ok=True, scenarios=rows, callbacks=sum(len(r['blocks']) for r in rows),
                  core_sha256=sha(a.core), before_sha256=sha(a.before), after_sha256=sha(a.after),
                  fixture_sha256=sha(Path(__file__)), device_access=False,
                  limits=['Instructions, not cycles or CPU percentages.',
                          'Same explicit synthesis boundaries as the main callback fixture.'])
    a.out.write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
