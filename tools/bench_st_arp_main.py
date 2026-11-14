"""Compare real linked ARP callbacks, clocks and event lifetimes; no device I/O.

Uses the existing core/native-producer fixture with its explicit synthesis,
mutex and publication boundaries. Counts are instructions, not physical cycles.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tests')]
from test_st_arp_analog_emu import AnalogMachine
from test_st_arp_generator_emu import BANK, W, KIT, SEQ, OWNERMAP, LIST, OWNER


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def normalized(m, raw):
    # Bank-relative API and captured live-Sound pointers can move when linking.
    # Native pool/event pointers have the same addresses in both executions.
    result = []
    for at in range(0, len(raw), 4):
        part = raw[at:at + 4]
        if len(part) == 4:
            value = int.from_bytes(part, 'big')
            if BANK <= value < BANK + 0x200000:
                voice, size = m.sym['arp_voice'], m.symbol_sizes['arp_voice']
                if voice <= value < voice + size:
                    result.append(('arp_voice', value - voice))
                    continue
                names = sorted(n for n, v in m.sym.items() if v == value)
                assert names, ('unknown bank pointer', hex(value))
                result.append(('symbol', names[0]))
                continue
        result.append(part.hex())
    return result


def snapshot(m):
    names = ('arp_parameters', 'arp_voice', 'arp_generation', 'arp_sequence',
             'arp_kit', 'arp_live', 'arp_bound_sound', 'arp_running', 'arp_root_lock')
    data = {n: normalized(m, m.read(m.sym[n], m.symbol_sizes[n])) for n in names}
    sources = []
    stride = m.symbol_sizes['arp_voice'] // 12
    assert stride == 628, 'runtime layout changed; update pointer-aware snapshot'
    for t in range(12):
        source = m.word(m.sym['arp_voice'] + t * stride + 200)
        sources.append(normalized(m, m.read(source, 80)) if source else None)
    return dict(data=data, templates=sources, pools=m.pool_counts(), notes=m.log,
                analog_effects=m.native_effects, lock_calls=m.locks)


def configure(m, count, kind, speed):
    pools = m.pool_counts()
    for t in range(count):
        assert m.configure(track=t, mode=1 + t % 4, range_=t % 7)
        m.u.mem_write(W + 0x101, bytes([speed]))
        assert m.call('arp_experiment_configure', t, W + 0x100)
        if kind == 'live':
            for note in (48 + t, 52 + t, 55 + t):
                m.key(note, track=t)
        else:
            m.event(note=48 + t, length=0, track=t)
    return pools


def frames(core, before, after, count, kind, speed, changing=False, changing_keys=False):
    machines = [AnalogMachine(core, p) for p in (before, after)]
    pools = [configure(m, count, kind, speed) for m in machines]
    period = 86400000 // (1, 2, 3, 4, 5, 6, 8, 10, 12, 16, 20, 24, 32, 40, 48, 64, 80, 96)[speed]
    times = [0, 28800, period, period + 28800, period * 2]
    if changing:
        times = [k * 900000 for k in range(12)]
    rows = []
    for k, begin in enumerate(times):
        counts = []
        tempo = (3600, 14400, 36000)[k % 3] if changing else 14400
        for m in machines:
            if changing_keys:
                # A changed native held snapshot at callback entry. Other
                # scenarios exercise the actual UI producer lifecycle.
                for t in range(count):
                    notes = (48 + t, 55 + t, 67 + t) if k & 1 else (48 + t, 52 + t)
                    data = bytearray(0x420)
                    mask = [0] * 4
                    base = 0x4553f6e0 + t * 0x2e18
                    for i, note in enumerate(notes):
                        mask[note >> 5] |= 1 << (note & 31)
                        struct.pack_into('>II', data, 16 + note * 8, 0,
                                         base + 16 + notes[i - 1] * 8 if i else 0)
                    struct.pack_into('>4I', data, 0, *mask)
                    struct.pack_into('>I', data, 0x410, base + 16 + notes[-1] * 8)
                    m.u.mem_write(base, bytes(data))
            if changing:
                for t in range(count):
                    at = m.track_address('arp_root_lock', t)
                    # Prepared root locks are explicit fixture inputs. Storage
                    # and capture remain checked by the integration regression.
                    mask = (0, 1, 2, 4, 8, 15, 255, 16, 0x80000000, 15, 15, 15)[k]
                    values = (1 + (k + t) % 4, (0, 9, 17)[(k + t) % 3],
                              (k + t) % 7, (k * 17 + t) % 128, 0, 0, 0, 0)
                    if k == 11:
                        values = (5, 255, 255, 255, 0, 0, 0, 0)  # ignored invalid lock values
                    m.u.mem_write(at, struct.pack('>I8H', mask, *values))
            m.w(0x402d7010, tempo)
            m.w(0x800046ec, begin)
            m.count = 0
            m.call('arp_frame')
            counts.append(m.count)
            m.drain(begin, begin + 2 * tempo)
        assert snapshot(machines[0]) == snapshot(machines[1]), (count, kind, speed, changing, k)
        assert counts[1] <= counts[0], ('instruction regression', count, kind, speed, changing, k, counts)
        rows.append(dict(block=k, begin=begin, before=counts[0], after=counts[1]))
    # Stop must return all template/queue ownership to the native pools.
    for m, initial in zip(machines, pools):
        m.u.mem_write(W + 0x140, struct.pack('>6I', 0x41525031, 0x53544f50, 0, 0, 0, 0))
        m.call(0x4011a722, W + 0x140)
        assert m.pool_counts() == initial
    assert snapshot(machines[0]) == snapshot(machines[1])
    return dict(active_tracks=count, kind=kind, speed=speed, changing_locks_tempo=changing,
                changing_held_snapshot=changing_keys, blocks=rows)


def clocks(core, before, after):
    machines = [AnalogMachine(core, p) for p in (before, after)]
    count = 0
    for speed in (*range(19), 255, 0xffffffff):
        for generation in (0, 1, 0xffffffff):
            for when in (0, 0xfffffff0):
                outputs = []
                for m in machines:
                    m.u.mem_write(W + 0x1800, b'\xa5' * 24)
                    result = m.call('arp_clock_arm', W + 0x1800, when, speed, generation)
                    outputs.append((result, m.read(W + 0x1800, 24)))
                assert outputs[0] == outputs[1], (speed, generation, when)
                count += 1
                if outputs[0][0]:
                    for begin, end, gen in ((when, (when + 28800) & 0xffffffff, generation),
                                            (when, when, generation),
                                            (0x70000000, 0x70011940, generation),
                                            (0, 28800, generation ^ 1)):
                        outputs = []
                        for m in machines:
                            result = m.call('arp_clock_poll', W + 0x1800, begin, end, gen, W + 0x1814)
                            outputs.append((result, m.read(W + 0x1800, 24)))
                        assert outputs[0] == outputs[1]
                        count += 1
    return count


def transitions(core, before, after):
    rows = []
    for count in (1, 8, 12):
        for case in ('tempo_low', 'tempo_high', 'pattern', 'kit', 'midi',
                     'owners', 'empty_keys', 'transport'):
            machines = [AnalogMachine(core, p) for p in (before, after)]
            for m in machines:
                configure(m, count, 'sequence' if case == 'transport' else 'live', 9)
                m.frame(0)
                if case == 'tempo_low':
                    m.w(0x402d7010, 3599)
                elif case == 'tempo_high':
                    m.w(0x402d7010, 36001)
                elif case == 'pattern':
                    m.w(0x43bd7430, SEQ + 0x22000)
                elif case == 'kit':
                    m.w(0x43bd743c, KIT + 0x22000)
                elif case == 'midi':
                    m.u.mem_write(KIT + 0x1550, b'\x0f\xff')
                elif case == 'owners':
                    m.w(LIST + 4, OWNER)
                    m.call(m.cs['core:core_native_interest_set'], OWNER, 1 << 15)
                    m.call(m.cs['core:core_native_initialize'])
                    m.u.mem_write(OWNERMAP, bytes([1] * 12))
                elif case == 'empty_keys':
                    for t in range(count):
                        m.u.mem_write(0x4553f6e0 + t * 0x2e18, bytes(0x420))
                elif case == 'transport':
                    m.w(0x43bd73bc, 2)
            costs = []
            for m in machines:
                m.w(0x800046ec, 5400000)
                m.count = 0
                m.call('arp_frame')
                costs.append(m.count)
            assert snapshot(machines[0]) == snapshot(machines[1]), (count, case)
            row = dict(active_tracks=count, case=case, before=costs[0], after=costs[1])
            rows.append(row)
            assert costs[1] <= costs[0], ('transition instruction regression', row)
            print('transition', count, case, costs[0], '->', costs[1], flush=True)
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for n in ('core', 'before', 'after', 'out'):
        p.add_argument('--' + n, type=Path, required=True)
    p.add_argument('--changing-keys', action='store_true')
    p.add_argument('--transitions', action='store_true')
    a = p.parse_args()
    clock_cases = clocks(a.core, a.before, a.after)
    print('PASS clock states, invalid inputs, generation, wrap:', clock_cases, flush=True)
    rows = []
    for count in (0, 1, 4, 8, 12):
        for kind in ('live', 'sequence'):
            for speed in (0, 9, 17):
                row = frames(a.core, a.before, a.after, count, kind, speed)
                rows.append(row)
                print(count, kind, speed, [(b['before'], b['after']) for b in row['blocks']], flush=True)
            row = frames(a.core, a.before, a.after, count, kind, 9, changing=True)
            rows.append(row)
            print(count, kind, 'changing locks/tempo PASS', flush=True)
        if a.changing_keys:
            row = frames(a.core, a.before, a.after, count, 'live', 9,
                         changing=True, changing_keys=True)
            rows.append(row)
            print(count, 'changing held snapshots/locks/tempo PASS', flush=True)
    transition_rows = transitions(a.core, a.before, a.after) if a.transitions else []
    result = dict(ok=True, core_sha256=sha(a.core), before_sha256=sha(a.before),
                  after_sha256=sha(a.after), fixture_sha256=sha(Path(__file__)),
                  clock_cases=clock_cases, scenarios=rows, transitions=transition_rows, device_access=False,
                  limits=['Instructions, not cycles or MAIN CPU percentages.',
                          'Real linked ARP, core dispatch, native UI producers, pools and queue.',
                          'Native publication, smoothing, mutex and lock application are fixture boundaries.',
                          'No full scheduler, synthesis, DMA or cache latency.',
                          'LFO3/POLY active composition is not executed by this fixture.'])
    a.out.write_text(json.dumps(result, indent=2) + '\n')
    print('PASS: complete callbacks, states, event outputs and balanced pools; no count increases')


if __name__ == '__main__':
    main()
