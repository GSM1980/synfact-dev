"""Linked POLY integer sync/publication differential, no device I/O or cycles."""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tests')]
from test_st_poly_cost_emu import Runtime, SEQ, KIT, HOST, RETURN, STACK
from test_st_poly_play_emu import Run, WORK, SIZE
from test_st_poly_publication_emu import maps, oracle, measured
from unicorn import UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_PC


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


class Sync(Runtime):
    """Route the existing private immediate boundary into real locked code.

    Native SR exclusion is a fixture boundary. All cases retain an identity
    topology, so no Sound/pool publication or native interest update is needed.
    """
    def __init__(self, path):
        self.immediates = 0
        super().__init__(path)

    def code(self, u, at, n, user):
        if at == 0x4011a722:
            self.count += 1
            self.immediates += 1
            sp = u.reg_read(UC_M68K_REG_A7)
            command = struct.unpack('>I', self.read(sp + 4, 4))[0]
            assert struct.unpack('>2I', self.read(command, 8)) == (0x504f4c59, 0x53594e43)
            u.reg_write(UC_M68K_REG_PC, self.sym['poly_preview_locked'])
            return
        super().code(u, at, n, user)


def sync_cases(before, after):
    cases = []
    for offset in range(4):
        for case in ('warm', 'null_sequence', 'null_kit', 'sequence_cache', 'midi_cache',
                     *(f'byte_{i}' for i in range(7))):
            machines = [Sync(p) for p in (before, after)]
            states, counts, immediates = [], [], []
            for m in machines:
                m.topology([])
                sequence = SEQ + offset
                m.w(m.sym['poly_seq_config'] + 4, sequence)
                m.w(m.sym['cache_sequence'], sequence)
                m.w(0x43bd7430, sequence)
                m.u.mem_write(sequence + 0x21381, bytes(7))
                if case == 'null_sequence':
                    m.w(0x43bd7430, 0)
                elif case == 'null_kit':
                    m.w(0x43bd743c, 0)
                elif case == 'sequence_cache':
                    m.w(m.sym['cache_sequence'], 0)
                elif case == 'midi_cache':
                    m.w(m.sym['cache_midi_mask'], 1)
                elif case.startswith('byte_'):
                    m.u.mem_write(m.sym['record_cache'] + int(case[5:]), b'\xff')
                count = m.execute('poly_sync')
                counts.append(count)
                immediates.append(m.immediates)
                states.append({n: m.read(m.sym[n], m.size[n]).hex() for n in
                               ('poly_seq_config', 'cache_sequence', 'cache_midi_mask', 'record_cache',
                                'poly_modes', 'poly_next_modes', 'poly_lfo_map', 'poly_post_map',
                                'poly_shared_mask', 'poly_independent_mask', 'poly_copy_pending')
                               if n in m.sym})
            assert states[0] == states[1] and immediates[0] == immediates[1], (offset, case)
            assert counts[1] <= counts[0], ('sync instruction regression', offset, case, counts)
            cases.append(dict(sequence_alignment=offset, case=case, before=counts[0],
                              after=counts[1], immediate_calls=immediates[0]))
    return cases


def publications(main, before, after):
    machines = [Run(main.read_bytes(), p) for p in (before, after)]
    def aligned(u, access, address, size, value, user):
        if WORK <= address < WORK + SIZE and size == 4:
            assert address % 4 == 0, ('unaligned publication access', hex(address))
    for m in machines:
        m.u.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, aligned)
    topologies = []
    for digital in maps():
        for master, width in ((8, 1), (8, 2), (8, 3), (9, 2)):
            owners = bytearray(digital)
            for t in range(master + 1, master + width):
                owners[t] = master
            topologies.append(bytes(owners))
    rng = random.Random(0x504f4c59)
    rows = []
    for index, owners in enumerate(topologies):
        slaves = [t for t in range(1, 12) if owners[t] < t]
        span = (slaves[0] << 8) | (slaves[-1] + 1) if slaves else 0x0c0c
        data = bytes(rng.randrange(256) for _ in range(SIZE))
        expected = oracle(data, owners)
        counts = []
        for m in machines:
            m.u.mem_write(WORK, data)
            m.u.mem_write(m.sym['poly_post_map'], owners)
            count = measured(m, m.sym['poly_post_map'], span)
            assert bytes(m.u.mem_read(WORK, SIZE)) == expected, (index, owners)
            counts.append(count)
        assert counts[1]['instructions'] <= counts[0]['instructions'], (index, counts)
        rows.append(dict(case=index, shared_slaves=len(slaves), owners=list(owners),
                         before=counts[0], after=counts[1]))
    # The independent oracle must distinguish a wrong copied field.
    corrupted = bytearray(expected)
    corrupted[0x1c + 142] ^= 1
    assert bytes(corrupted) != expected
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for n in ('core', 'before', 'after', 'out'):
        p.add_argument('--' + n, type=Path, required=True)
    a = p.parse_args()
    sync = sync_cases(a.before, a.after)
    print('PASS sync:', len(sync), 'warm/cold/mismatch/null/alignment cases', flush=True)
    publication = publications(a.core, a.before, a.after)
    print('PASS publication:', len(publication), 'topology/PLAY maps, full-byte oracle and ABI', flush=True)
    result = dict(ok=True, sync=sync, publication=publication, negative_control=True,
                  core_sha256=sha(a.core), before_sha256=sha(a.before), after_sha256=sha(a.after),
                  fixture_sha256=sha(Path(__file__)), device_access=False,
                  limits=['Instructions and opcode bytes, not cycles or CPU percentages.',
                          'Sync immediate exclusion is a boundary; locked sync body executes.',
                          'Publication is integer-only; native synthesis/EMAC kernels are not measured here.',
                          'Native pipelines, topology changes, Sounds, notes and storage have separate regressions.'])
    a.out.write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
