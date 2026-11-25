"""kitload-st LOAD with the real native Kit codecs and module metadata.

Builds on the COMP/Chorus coexistence fixture (real COMP, Chorus, LFO3,
POLY and Playhead providers on a built core and bank). A source Kit gets
distinct native bytes, COMP and Chorus values and Playhead modes, and is
encoded into a bank record. The destination Kit gets different ones. KITLOAD's
kl_restore_kit (what LOAD runs after reading the record) must then leave the
destination equal, byte for byte, to a reference decode of the same record,
with the source's COMP, Chorus and Playhead settings, while the project
memory before the Kits (the patterns, with their trigs, notes, locks,
microtiming, conditions and lengths, as the fixture holds them) and the
other Kits stay unchanged. Not run by the author (no Digitakt OS for COMP,
no SGDK): it needs a reviewer's build.

    python tests/test_st_kitload_restore_emu.py --core out/<core> --bank out/<bank>.smz4

The bank must contain kitload_module, comp_module, chorus_module and
playhead_module. No device access.
"""
import argparse
from pathlib import Path
import random
import struct
from test_st_lfo3_activity_emu import Activity
from test_st_module_startup_emu import DESCRIPTORS
from test_st_comp_emu import ID, KIT

KIT_RAM, SLOT = 0x1618, 0x43c0dfc0 + 0x2e0200
CHORUS = 0x403
KITS_RAM_OFS = 17417756
PATTERNS = (KIT - KITS_RAM_OFS, KIT)          # the project before its Kits: patterns and sequencer data


def verify(core, bank):
    m = Activity(core, bank, full_catalog=True)
    for need in ('kl_restore_kit', 'comp_ui_init', 'chorus_ui_init', 'playhead_modes'):
        assert need in m.bs, 'the bank lacks ' + need
    api = DESCRIPTORS + 0xf000
    m.w(api, 3); m.w(api + 4, m.sym['core:core_machine_ui_api'])
    m.sr = 0x2700
    assert m.call('chorus_ui_init', api) == 0
    m.call('comp_params_init'); assert m.call('comp_ui_init', api) == 0
    for name, at in [('kitnew', 0x400b3384), ('kitenc', 0x400a78f4), ('kitdec', 0x400a78a0),
                     ('getglobal', 0x4000d94a), ('setglobal', 0x4000da32)]:
        m.bs[name] = at
    m.sr = 0x2000
    src, dst, ref = KIT, KIT + KIT_RAM, KIT + 2 * KIT_RAM
    modes = m.bs['playhead_modes']

    def current(kit):
        m.w(0x800030b8, kit); m.w(0x800030bc, kit + 0xdce)

    def settings(kit, k, comp, chorus, phead, rng):
        m.call('kitnew', kit)
        for t in range(12):                       # native track bytes: each Sound record, past its header
            at = kit + 42 + 291 * t + 32
            m.u.mem_write(at, bytes(rng.randrange(256) for _ in range(64)))
        current(kit)
        for i, v in enumerate(comp): assert m.call('setglobal', api, ID + i, v, 0, 0, 1) == 0
        for i, v in enumerate(chorus): assert m.call('setglobal', api, CHORUS + i, v, 0, 0, 1) == 0
        m.u.mem_write(modes + 16 * k, bytes(phead) + b'\0' * (16 - len(phead)))

    comp_a = [0x1234, 0x2345, 0x3456, 0x4567, 0x500, 0x5678, 0x200]
    chorus_a = [0x100, 0x2000, 0x7f00, 0, 0x3000, 0x4000, 0]
    phead_a = [1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1]
    comp_b = [0x0100, 0x0200, 0x0300, 0x0400, 0x100, 0x0600, 0x100]
    chorus_b = [0x7000, 0x100, 0x200, 0x10, 0x100, 0x200, 0]
    phead_b = [0] * 13

    settings(src, 0, comp_a, chorus_a, phead_a, random.Random(1))
    assert m.call('kitenc', SLOT, src, 0) & 255
    record = bytes(m.u.mem_read(SLOT, 0x1600))
    assert record[0x15d0:0x15d2] == b'CQ'          # COMP's record in the tail

    m.call('kitnew', ref)                         # the reference: the native decode of that record
    m.u.mem_write(SLOT, record)
    assert m.call('kitdec', ref, SLOT) & 255
    reference = bytes(m.u.mem_read(ref, KIT_RAM))

    settings(dst, 1, comp_b, chorus_b, phead_b, random.Random(2))
    before = bytes(m.u.mem_read(dst, KIT_RAM))
    assert before != reference
    around = bytes(m.u.mem_read(PATTERNS[0], PATTERNS[1] - PATTERNS[0]))
    others = bytes(m.u.mem_read(src, KIT_RAM)) + bytes(m.u.mem_read(ref, KIT_RAM)) + \
        bytes(m.u.mem_read(KIT + 3 * KIT_RAM, 125 * KIT_RAM))

    m.u.mem_write(SLOT, record)
    assert m.call('kl_restore_kit', dst, SLOT) == 1

    after = bytes(m.u.mem_read(dst, KIT_RAM))
    diff = [hex(i) for i in range(KIT_RAM) if after[i] != reference[i]]
    assert not diff, ('destination differs from the native decode at', diff[:16])
    current(dst)
    assert [m.call('getglobal', api, ID + i) for i in range(7)] == comp_a, 'COMP not restored'
    assert [m.call('getglobal', api, CHORUS + i) for i in range(7)] == chorus_a, 'Chorus not restored'
    assert list(m.u.mem_read(modes + 16, 13)) == phead_a, 'Playhead not restored'
    now = bytes(m.u.mem_read(PATTERNS[0], PATTERNS[1] - PATTERNS[0]))
    if now != around:
        changed = [hex(PATTERNS[0] + i) for i in range(len(now)) if now[i] != around[i]]
        raise AssertionError(('project memory before the Kits changed', len(changed), changed[:16]))
    assert bytes(m.u.mem_read(src, KIT_RAM)) + bytes(m.u.mem_read(ref, KIT_RAM)) + \
        bytes(m.u.mem_read(KIT + 3 * KIT_RAM, 125 * KIT_RAM)) == others, 'another Kit changed'
    print('PASS kitload restore: the whole Kit as the native decode, COMP/Chorus/Playhead restored, '
          'sequencer data and other Kits unchanged')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core', type=Path, required=True); p.add_argument('--bank', type=Path, required=True)
    a = p.parse_args(); verify(a.core, a.bank)
