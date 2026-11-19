"""POLY configuration, native key events and sequencer batches before/after.

Real core dispatch, native pools/queue and note effects use the established
producer fixture. Sound publication/synthesis and parent UI return-PC are
explicit boundaries; this does not emulate hardware sound or scheduling.
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
from test_st_arp_generator_emu import W, SP, LIST, KIT, SEQ
from bench_st_arp_main import normalized


class Poly(AnalogMachine):
    def __init__(self, core, bank):
        super().__init__(core, bank)
        self.w(LIST, self.sym['poly_native'])
        self.w(LIST + 4, 0)
        assert self.call('poly_initialize', W) == 0
        self.call(self.cs['core:core_native_initialize'])
        for t in range(12):
            self.u.mem_write(KIT + 0x2a + t * 0x123 + 290, bytes([t * 7]))
            # Inherit chord defaults +4/+7/+12; the other step lanes are FF.
            self.u.mem_write(SEQ + t * 0x499 + 0x2c0, b'\xff' * (4 * 64))
            self.u.mem_write(SEQ + t * 0x499 + 0x3c5, bytes([60, 68, 71, 76]))

    def config(self, track, voices, independent, enabled=1):
        command = W + 0x180
        self.u.mem_write(command, struct.pack('>6I', 0x504f4c59, 0x43464731,
                                            track, enabled, voices, 0))
        self.count = 0
        self.call(0x4011a722, command)
        assert self.word(command + 20) == 1
        if enabled and track < 8 and independent:
            self.u.mem_write(command, struct.pack('>6I', 0x504f4c59, 0x43464732,
                                                track, 1, 0, 0))
            self.call(0x4011a722, command)
            assert self.word(command + 20) == 1
        return self.count

    def key_event(self, note, on, track):
        # Same explicit parent-origin fixture as the established POLY keyboard
        # regression; the real native producer and immediate dispatch still run.
        self.w(SP + 0x100, 0x40105408 if on == 1 else 0x401052c4)
        self.count = 0
        self.key(note, on, track=track)
        cost = self.count
        self.drain(0, 28800)
        return cost

    def sequence_event(self, track, note, step, when):
        event = self.call(0x4011a4c2)
        assert event
        lock = self.call(0x4011a472)
        values = [0] * 20
        values[1:4] = [1, track, 1]
        values[6], values[9], values[10], values[12], values[17] = note, 0x10601, KIT + 0x2a + track * 0x123, 14400, lock
        self.u.mem_write(event, struct.pack('>20I', *values))
        generation = self.word(self.sym['poly_seq_config'])
        pending = self.sym['poly_seq_config'] + 32 + 16 * track
        self.u.mem_write(pending, struct.pack('>4I', event, SEQ, step, generation))
        # ENQUEUE is a call-site core adapter at 4008cfc4, not a hook at the
        # general native queue entry. Execute its real dispatcher with the
        # accepted-root input above, then retain real native queue consumers.
        args = W + 0x200
        self.u.mem_write(args, struct.pack('>2I', event, when))
        self.count = 0
        self.call(self.cs['core:core_native'], 1, args)
        cost = self.count
        assert self.word(pending) == 0, 'sequencer batch was not intercepted'
        previous = len(self.log)
        self.drain(when, when + 28800)
        assert len(self.log) > previous + 1, 'fixture did not expand the chord'
        return cost


def state(m):
    names = ('poly_seq_config', 'cache_sequence', 'cache_midi_mask', 'record_cache',
             'poly_modes', 'poly_next_modes', 'poly_lfo_map', 'poly_post_map',
             'poly_shared_mask', 'poly_independent_mask', 'poly_copy_pending',
             'poly_has_groups', 'poly_analog_active', 'poly_analog_type',
             'poly_publish_first', 'poly_publish_end', 'poly_publish_span', 'poly_publish_entry',
             'live_notes', 'live_age', 'live_clock', 'live_next', 'live_generation',
             'prep_step', 'prep_generation', 'preview_notes', 'preview_generation',
             'chord_root', 'chord_notes', 'chord_generation')
    return dict(data={n: normalized(m, m.read(m.sym[n], m.symbol_sizes[n])) for n in names if n in m.sym},
                kit=m.read(KIT, 0x1618).hex(), pools=m.pool_counts(), notes=m.log,
                effects=m.native_effects, locks=m.locks)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for n in ('core', 'before', 'after', 'out'):
        p.add_argument('--' + n, type=Path, required=True)
    a = p.parse_args()
    rows = []
    for track, voices in ((0, 2), (0, 3), (0, 4), (5, 3), (8, 2), (8, 3), (9, 2)):
        for independent in ((0, 1) if track < 8 else (0,)):
            machines = [Poly(a.core, bank) for bank in (a.before, a.after)]
            pools = [m.pool_counts() for m in machines]
            costs = [m.config(track, voices, independent) for m in machines]
            assert state(machines[0]) == state(machines[1]), ('configuration', track, voices, independent)
            assert costs[1] <= costs[0], ('configuration count', track, voices, costs)
            operations = [dict(case='configure', before=costs[0], after=costs[1])]
            for on, notes in ((1, (60, 64, 67, 72, 60, 55)),
                              (2, (60, 55, 72, 67, 64))):
                for note in notes:
                    costs = [m.key_event(note, on, track) for m in machines]
                    assert state(machines[0]) == state(machines[1]), ('key', track, voices, on, note)
                    assert costs[1] <= costs[0], ('key count', track, voices, on, note, costs)
                    operations.append(dict(case='press' if on == 1 else 'release', note=note,
                                           before=costs[0], after=costs[1]))
            for step, note in enumerate((60, 62, 65, 72)):
                costs = [m.sequence_event(track, note, step, 28800 * (step + 1)) for m in machines]
                assert state(machines[0]) == state(machines[1]), ('sequence', track, voices, step)
                assert costs[1] <= costs[0], ('sequence count', track, voices, step, costs)
                operations.append(dict(case='sequence', note=note, before=costs[0], after=costs[1]))
            for m, initial in zip(machines, pools):
                m.config(track, voices, independent, enabled=0)
                m.drain(200000, 228800)
                assert m.pool_counts() == initial, ('unbalanced pools', track, voices, m.pool_counts(), initial)
            assert state(machines[0]) == state(machines[1])
            rows.append(dict(track=track, voices=voices, independent=independent, operations=operations))
            print('PASS events:', track, voices, independent, 'configure', operations[0], flush=True)
    sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    result = dict(ok=True, scenarios=rows, before_sha256=sha(a.before), after_sha256=sha(a.after),
                  core_sha256=sha(a.core), fixture_sha256=sha(Path(__file__)), device_access=False,
                  limits=['Instructions, not cycles or CPU percentages.',
                          'Core, producers, native pools/queue and note effects execute.',
                          'Parent UI origin, Sound publication, smoothing and native lock application are boundaries.',
                          'No complete scheduler, DSP or analog electronics.'])
    a.out.write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
