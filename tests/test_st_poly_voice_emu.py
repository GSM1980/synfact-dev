"""POLY voice routing with the OS 1.42 pedal handler, event queue and gates.

Use a linked bank containing ARP and POLY. The existing ARP fixture sets up
the core and native pools, then only POLY is subscribed for these cases.
Note producers begin at their mapped track entry points. Track Relation and
the Sound Setup/Sound Browser mutations execute their linked native paths.
UI model/synthesis boundaries are stubbed. No device IO.
"""
import argparse
import gc
import itertools
import struct
from pathlib import Path

from test_st_arp_generator_emu import KIT, LIST, SEQ, SP, W
from test_st_arp_keyboard_emu import KeyboardMachine
from unicorn.m68k_const import (UC_M68K_REG_A2, UC_M68K_REG_A4, UC_M68K_REG_A7,
                               UC_M68K_REG_D0, UC_M68K_REG_PC)

MODEL = W+0x3000
MODEL_VT = W+0x3400
RETURN_SOUND, CHANGED, RAW_KIT, ROUTE = (W+0x3600+i*4 for i in range(4))


class Poly(KeyboardMachine):
    def __init__(self, core, bank):
        super().__init__(core, bank)
        self.w(LIST, self.sym['poly_native'])
        self.w(LIST+4, 0)
        self.call(self.cs['core:core_native_initialize'])
        assert self.call('poly_initialize', W) == 0
        self.w(0x444852d0, W+0xc00)
        self.w(0x402d5aac, 0xffffffff)
        self.w(0x800030bc, KIT+0xdce)
        self.w(0x80003120, KIT)
        self.call(0x40109f56)  # Native sustain queues and note-off list pool.
        for track in range(self.tracks):
            self.call(0x400b35ec, 0x40a39b34+track*0x499, 0)
            self.call(0x4010519c, track)
        assert self.call('poly_record_write', 0, 1, 2) == 1
        self.applied = []
        self.sounds = []

    def code(self, uc, pc, size, context):
        if pc in (0x400a431c, 0x400a5592):
            # ColdFire FF1 is absent from this Unicorn build.
            uc.reg_write(UC_M68K_REG_D0, 32-uc.reg_read(UC_M68K_REG_D0).bit_length())
            uc.reg_write(UC_M68K_REG_PC, pc+2)
            return
        if pc in [0x4008b846, 0x4010438c]:
            self.ret()
            return
        if pc == 0x4008dae6:
            self.u.mem_write(self.word(uc.reg_read(UC_M68K_REG_A7)+12), bytes(4))
            self.ret()
            return
        if pc == 0x400a42bc and self.phase == 'accept':
            event = uc.reg_read(UC_M68K_REG_A2)
            self.applied.append((self.word(event+4), self.word(event+8), self.word(event+24)))
            self.sounds.append(uc.reg_read(UC_M68K_REG_A4))
        return super().code(uc, pc, size, context)

    def key(self, pitch, on=True, track=0):
        self.w(SP+0x100, 0x401055cc if on else 0x40105488)
        try:
            if on:
                self.call(0x40104dc6, track, pitch, 100, 64, 0, 0xffffffff, 0xffffffff, 0xffffffff)
            else:
                self.call(0x401051f0, track, pitch, 64)
        finally:
            self.w(SP+0x100, 0)

    def pedal(self, value):
        # CC dispatcher preconditions after mapping input to an audio track.
        self.w(0x444e23f4, W)
        self.u.mem_write(0x4449986c, b'\x01')
        self.call(0x40106190, 0, 64, value, 0, 0)
        self.w(0x444e23f4, 0)

    def play(self):
        self.drain(0, 28800)

    def live(self):
        return self.read(self.sym['live_mask'], 1)[0]

    def relate(self, source, *targets):
        self.u.mem_write(KIT+0x15e4+2*source, struct.pack('>H', sum(1 << t for t in targets)))

    def mode(self, mode, track=0):
        command = W+0x180
        self.u.mem_write(command, struct.pack('>6I', 0x504f4c59, 0x43464732, track, mode, 0, 0))
        self.call(0x4011a8e6, command)
        assert self.word(command+20) == 1

    def step(self, pitch, length, sound=0, track=0):
        event = self.call(0x4011a686)
        words = [0]*20
        words[1], words[3], words[6], words[9], words[12] = 1, 1, pitch, 0x10781, length
        words[2] = track
        words[10] = sound
        self.u.mem_write(event, struct.pack('>20I', *words))
        config = self.sym['poly_seq_config']
        self.u.mem_write(config+32+16*track, struct.pack('>4I', event, SEQ, 0, self.word(config)))
        assert self.call('poly_sequence_enqueue', event, 0) == 1
        self.play()


class Unison(Poly):
    offsets = ()

    def expected_pitch(self, track, pitch, transpose):
        offset = dict(self.offsets).get(track, 0)
        native = super().expected_pitch(track, pitch, transpose)
        return max(0, min(127 << 16, native+offset))

    def spread(self, track, cents):
        command = W+0x180
        self.u.mem_write(command, struct.pack('>6I', 0x504f4c59, 0x554e4953, track, cents, 0, 0))
        self.call(0x4011a8e6, command)
        assert self.word(command+20) == 1


def unison(core, bank):
    for track, voices, offsets in [(0, 4, (-16384, -5461, 5461, 16384)),
                                   (8, 3, (-16384, 0, 16384)),
                                   (9, 2, (-16384, 16384))]:
        machine = Unison(core, bank)
        assert machine.call('poly_record_write', track, 1, voices) == 1
        machine.mode(track % 2, track)
        assert not machine.word(machine.cs['core:core_note_pitch_bias'])
        machine.spread(track, 50)
        machine.offsets = tuple(zip(range(track, track+voices), offsets))
        stack = lambda action, note: [(action, t, note) for t in range(track, track+voices)]
        baseline = machine.pool_counts()
        machine.gate_tick()
        machine.key(60, track=track)
        machine.play()
        assert machine.applied == stack(1, 60)
        machine.applied.clear()
        machine.key(60, False, track=track)
        machine.play()
        assert machine.applied == stack(2, 60)
        assert machine.pool_counts() == baseline
        machine.applied.clear()
        machine.step(62, 14400, track=track)
        assert machine.applied == stack(1, 62)
        assert machine.pool_counts() == baseline
        machine.applied.clear()

        # Native OS 1.42 pattern encoder and decoder, including the core
        # PatternSettings hook. The saved wire header is exactly 40 bytes.
        wire, restored = 0x44200000, 0x44300000
        record = machine.read(SEQ+0x21381, 7)
        machine.call(0x400a8264, wire, SEQ, 1)
        assert machine.read(wire+0x5a8f+0x21, 7) == record
        assert machine.call(0x400a80c0, restored, wire, 0) & 255 == 1
        assert machine.read(restored+0x21381, 7) == record
        machine.w(0x43bd8430, restored)
        assert machine.call('poly_sync') == 1
        machine.key(64, track=track)
        machine.play()
        assert machine.applied == stack(1, 64)
        machine.key(64, False, track=track)
        machine.play()
        machine.spread(track, 0)
        assert not machine.word(machine.cs['core:core_note_pitch_bias'])
        assert machine.read(restored+0x21381, 1) == b'\x50'
        del machine
        gc.collect()

    machine = Unison(core, bank)
    machine.spread(0, 50)
    machine.offsets = ((0,-16384),(1,16384))
    machine.gate_tick()
    machine.key(60)
    machine.key(64)
    machine.play()
    machine.applied.clear()
    machine.key(64, False)
    machine.play()
    assert machine.applied == [(2,0,64),(2,1,64),(1,0,60),(1,1,60)]
    machine.key(60, False)
    machine.play()
    assert machine.live() == 0

    for source, target, order in ((4,0,[4,1,0]),(0,4,[0,4,1])):
        machine.relate(source,target)
        machine.relate(target)
        machine.applied.clear()
        machine.key(62, track=source)
        machine.play()
        assert sorted(machine.applied) == sorted((1,t,62) for t in order)
        machine.applied.clear()
        machine.key(62, False, track=source)
        machine.play()
        assert sorted(machine.applied) == sorted((2,t,62) for t in order)
    print('PASS: UNISON digital and analog notes, Q16 pitch, native priority, Track Relation, pool recycling and pattern save/load')


def pedal_order(core, bank, releases):
    machine = Poly(core, bank)
    pools = machine.pool_counts()
    for pitch in (60, 64, 67, 72):
        machine.key(pitch)
    machine.play()
    assert machine.applied == [(1, 0, 60), (1, 1, 64), (1, 0, 67), (1, 1, 72)]
    machine.applied.clear()
    machine.pedal(127)
    for pitch in releases:
        machine.key(pitch, False)
    machine.play()
    machine.call('poly_tick', 0)
    assert machine.live() == 3 and not machine.applied, releases
    machine.pedal(0)
    machine.play()
    machine.call('poly_tick', 0)
    assert all(action == 2 for action, _, _ in machine.applied), releases
    assert machine.live() == 0 and machine.pool_counts() == pools, releases


def voice_origins(machine):
    cursor = W+0x1400
    callers = {0x4002ed0a: 2, 0x401055cc: 2, 0x4002d602: 1,
               0x40105488: 1, 0x4010a096: 1}
    cases = [([], 0), ([(0, 0x40020100), (3, 0x4010a096), (11, 0x4002ed0a)], 1)]
    cases.extend(([(index, pc)], origin)
                 for index in (0, 1, 2, 511, 512, 1022, 1023)
                 for pc, origin in callers.items())
    for entries, origin in cases:
        data = bytearray(b'\xa5'*2050)
        for index, pc in entries:
            struct.pack_into('>I', data, 2*index, pc)
        machine.u.mem_write(cursor, bytes(data))
        assert machine.call('poly_scan_key_origin', cursor) == origin, entries
        assert machine.read(cursor, len(data)) == data


def verify(core, bank):
    for releases in itertools.permutations((60, 64, 67, 72)):
        pedal_order(core, bank, releases)
        gc.collect()

    machine = Poly(core, bank)
    voice_origins(machine)
    machine.key(60)
    machine.play()
    machine.pedal(127)
    machine.key(60, False)
    machine.play()
    machine.applied.clear()
    machine.key(60)
    machine.play()
    assert machine.applied == [(1, 0, 60)]

    machine = Poly(core, bank)
    for lane in (0x300, 0x340, 0x380):
        machine.u.mem_write(SEQ+lane, b'\x40')
    machine.gate_tick()  # Consume the native release requested by group setup.
    # Transport start enqueues the prepared track event directly in stock.
    machine.event(note=60, length=0, flags=0x10781, locked=False)
    machine.play()
    machine.step(64, 14400)
    assert machine.gate_tick() & 2 and machine.word(0x80003088) == 0
    assert machine.word(0x80003124) & 2  # The native held bit outlives this finite gate.
    machine.step(67, 14400)
    assert machine.applied == [(1, 0, 60), (1, 1, 64), (1, 1, 67)]
    assert machine.word(0x80003084) == 0 and machine.word(0x80003124) & 1

    machine = Poly(core, bank)
    for lane in (0x300, 0x340, 0x380):
        machine.u.mem_write(SEQ+lane, b'\x40')
    machine.gate_tick()
    machine.key(72)
    machine.play()
    for pitch in (60, 64, 67):
        machine.step(pitch, 14400)
    assert machine.applied == [(1, 0, 72), (1, 1, 60), (1, 1, 64), (1, 1, 67)]
    assert machine.live() == 1
    print('PASS: 24 native CC64 release orders, same-pitch retrigger, gate expiry and live/sequence overlap')


def track_relations(core, bank):
    machine = Poly(core, bank)
    machine.relate(0, 3)
    machine.relate(3, 4)  # Linked notes cannot create a second relation.
    machine.key(60)
    machine.key(64)
    machine.play()
    assert machine.applied == [(1, 0, 60), (1, 3, 60), (1, 1, 64), (1, 3, 64)], machine.applied

    machine = Poly(core, bank)
    machine.relate(1, 3)  # The logical source has an empty row.
    machine.key(60)
    machine.key(64)
    machine.play()
    assert machine.applied == [(1, 0, 60), (1, 1, 64)], machine.applied

    for targets in [(0,), (1,), (0, 1)]:
        machine = Poly(core, bank)
        machine.relate(3, *targets)
        for pitch in (60, 64):
            machine.key(pitch, track=3)
            machine.play()
        assert machine.applied == [(1, 3, 60), (1, 0, 60), (1, 3, 64), (1, 1, 64)], machine.applied
        assert machine.sounds[1] == machine.sounds[3] == KIT+0x2a, machine.sounds
        assert machine.read(0x43bdda3c+2*(3*128+64), 2) == b'\x00\x02'
        machine.key(64, False, track=3)
        machine.play()
        assert (2, 1, 64) in machine.applied and (2, 0, 64) not in machine.applied, machine.applied

    machine = Poly(core, bank)
    machine.relate(3, 0)
    machine.key(72)
    machine.play()
    machine.key(60, track=3)
    machine.play()
    assert machine.applied == [(1, 0, 72), (1, 3, 60), (1, 1, 60)], machine.applied

    machine = Poly(core, bank)
    machine.relate(3, 0)
    for on in (True, True, False):
        machine.key(60, on, track=3)
        machine.play()
    assert [e for e in machine.applied if e[1]<2] == [(1, 0, 60), (1, 0, 60), (2, 0, 60)]
    assert not machine.word(0x80003124) & 3

    machine = Poly(core, bank)
    for source in (3, 4, 5):
        machine.relate(source, 0)
        machine.key(60, track=source)
        machine.play()
    assert [e for e in machine.applied if e[1]<2] == [(1, 0, 60), (1, 1, 60), (1, 0, 60)]
    machine.key(60, False, track=3)
    machine.play()
    assert machine.word(0x80003124) & 3 == 3
    machine.key(60, False, track=5)
    machine.play()
    assert machine.word(0x80003124) & 3 == 2
    print('PASS: native Track Relation logical source, legacy targets, allocation, matched release, retrigger and no cascade')


def poly_sound_locks(core, bank):
    m = Poly(core, bank)
    m.mode(1)
    for lane in (0x300, 0x340, 0x380):
        m.u.mem_write(SEQ+lane, b'\x40')
    first, second = 0x41b94ec9, 0x41b94ec9+0x123  # Native sequencer Sound Lock pool.
    m.u.mem_write(first+0xa8, b'\x05')
    m.u.mem_write(second+0xa8, b'\x0c')
    m.step(60, 28800, first)
    m.step(64, 28800, second)
    assert m.applied == [(1, 0, 60), (1, 1, 64)] and m.sounds == [first, second], m.applied
    assert [m.word(0x800046b8+4*t) >> 16 for t in (0,1)] == [65,76]
    print('PASS: separate P.MOD voices keep their sequencer Sound Locks and native pitch')


class TrackOptions(Poly):
    def __init__(self, core, bank):
        super().__init__(core, bank)
        self.w(MODEL, MODEL_VT)
        self.w(MODEL_VT+0x28, RETURN_SOUND)
        self.w(MODEL_VT+0x10, CHANGED)
        self.w(MODEL_VT+0x34, RAW_KIT)
        self.w(MODEL+0x6c, MODEL_VT)
        self.call_sr = 0x2000
        self.published = []
        self.call('poly_tick', 0)

    def code(self, uc, pc, size, context):
        if pc == RETURN_SOUND:
            self.ret(KIT+0x2a)
            return
        if pc == RAW_KIT:
            self.ret(KIT)
            return
        if pc in (CHANGED, 0x400243b2, 0x40024e78, 0x400a657e, 0x4000f5ae, 0x4001fdfe):
            self.ret()
            return
        if pc in (0x400c2494, 0x400a64f2):  # Hardware track/type availability boundary.
            self.ret(1)
            return
        if pc == 0x4016e9c8:
            self.ret(MODEL-0x130)
            return
        if pc == 0x4000f642:
            track = self.word(uc.reg_read(UC_M68K_REG_A7)+8)
            self.ret(MODEL+0x100+4*track)
            return
        if pc == ROUTE:
            track = (self.word(uc.reg_read(UC_M68K_REG_A7)+4)-MODEL-0x100)//4
            self.ret(self.fx[track])
            return
        if pc == 0x400a321a and hasattr(self, 'published'):
            # The native voice update downstream of the audio/Sound cache.
            # Execute the real publication of Sound Setup and its mod slots.
            args = uc.reg_read(UC_M68K_REG_A7)+4
            sound, track = struct.unpack('>2I', self.read(args, 8))
            self.published.append((sound, track))
            self.w(0x800030c0+4*track, sound)
            uc.reg_write(UC_M68K_REG_PC, 0x400a313c)
            return
        return super().code(uc, pc, size, context)


def track_options(core, bank):
    m = TrackOptions(core, bank)
    master, own, loaded = KIT+0x2a, KIT+0x2a+0x123, W+0x3800
    slave = b'\x11'*0x123
    m.u.mem_write(own, slave)
    m.u.mem_write(master+0xa3, b'\x02')
    m.call(0x400a321a, master, 1)
    aux = 0x8000297c+124
    m.published.clear()
    assert m.read(aux, 1) == b'\x02'
    m.call(0x4002450c, MODEL, 0)  # Actual Velocity to Vol setter.
    assert m.read(master+0xa3, 1) == b'\x00' and m.read(aux, 1) == b'\x02'
    m.call('poly_tick', 0)
    assert m.read(aux, 1) == b'\x00' and m.published == [(master, 1)]

    m.published.clear()
    new_sound = bytearray(0x123)
    new_sound[0xa3] = 3
    new_sound[0xaa] = 0x35  # Velocity modulation payload in the stock record.
    m.u.mem_write(loaded, bytes(new_sound))
    # The same KitActive virtual method used by Load Sound to Track.
    # Its embedded TrackModel reuses the fake method table.
    m.w(MODEL+0x6c+0x10, 0)
    assert m.call(0x4000ebe0, MODEL, loaded, 0) == 0
    m.call('poly_tick', 0)
    assert m.read(aux, 1) == b'\x03' and m.read(0x80002904+124, 1) == b'\x35'
    assert m.published == [(master, 1)] and m.read(own, len(slave)) == slave

    m.fx = [0, 1]+[1]*10
    m.w(MODEL_VT+0x28, ROUTE)
    for i in range(12):
        m.w(MODEL+0x100+4*i, MODEL_VT)
    for mode in (0, 1):
        m.mode(mode)
        assert [m.call(0x4004bf86, 0, t)&255 for t in range(3)] == [1, 1, 0]
    m.relate(3, 1)
    assert m.call(0x400100f6, MODEL, 3, 0) & 255 == 1
    assert m.call(0x400100f6, MODEL, 3, 1) & 255 == 0
    m.w(MODEL_VT+0x50, CHANGED)
    m.call(m.cs['core:core_relation_ui_set'], MODEL, 3, 0, 0)
    m.call(m.cs['core:core_relation_ui_set'], MODEL, 3, 1, 1)
    assert m.read(KIT+0x15e4+2*3, 2) == bytes(2)
    print('PASS: stock Sound Setup and Sound load publish to voices, FX screen/LED getter and logical Track Relation editing')


def modulation_modes(core, bank):
    from test_st_poly_play_emu import fixture

    work, args, mac_init = W+0x4000, W+0x4e00, W+0x4f00
    layouts = [((0, 2, 0),), ((0, 3, 0),), ((0, 4, 0),), ((0, 3, 1),),
               ((8, 2, 0),), ((8, 3, 0),), ((9, 2, 0),), ((8, 3, 1),),
               ((0, 3, 0), (8, 3, 1)), ((0, 3, 1), (8, 3, 0)),
               ((0, 3, 0), (8, 3, 0)), ((0, 3, 1), (8, 3, 1))]

    def prepare(layout):
        machine = Poly(core, bank)
        assert machine.call('poly_record_write', 0, 0, 2) == 1
        for track, voices, mode in layout:
            assert machine.call('poly_record_write', track, 1, voices) == 1
            machine.mode(mode, track)
        machine.u.mem_write(mac_init, bytes.fromhex('a93c000000204e75'))
        machine.call(mac_init)  # Native fractional, signed MACSR.
        machine.call(0x40118fe8)
        machine.w(args, work)
        for track in range(13):
            machine.w(0x800046b8+4*track, (48+3*track)<<16)
            machine.u.mem_write(0x80004670+2*track, struct.pack('>H', 0x2000+0x200*track))
        return machine

    def render(machine, stock, source, trigs, shared):
        machine.u.mem_write(work, source)
        before = machine.count
        machine.call('poly_mod_all' if stock else 0x40097b74, work)
        machine.call('poly_native_all' if stock else 0x4011942a, work, 0x3840, trigs)
        if shared:
            machine.call('poly_native', 3, args, args+16)
        return machine.read(work, len(source)), machine.count-before

    for layout in layouts:
        expected, actual = prepare(layout), prepare(layout)
        shared = sum(((1 << (voices-1))-1) << (track+1) for track, voices, mode in layout if not mode)
        assert actual.word(actual.sym['poly_shared_mask']) == shared
        assert actual.sym['poly_slave_mask'] == actual.sym['poly_shared_mask']
        data, params = fixture(1, 1, 0)
        _, lfo2 = fixture(2, 0, 1)
        data = bytearray(data)
        for t in range(12):
            row = 0x1c+142*t
            data[row+2:row+18] = params
            data[row+18:row+34] = lfo2
            # Velocity and key tracking use native controller destinations.
            struct.pack_into('>I', data, 0x754+124*t, 0x1000002f)
        counts, full_counts, distinct = [], [], set()
        for block in range(24):
            trigs = (1 << (block % 12)) if block % 3 == 0 else 0
            reference, full_count = render(expected, True, bytes(data), trigs, shared)
            output, count = render(actual, False, bytes(data), trigs, shared)
            assert output == reference, (layout, block, next(i for i,(a,b) in enumerate(zip(output,reference)) if a!=b))
            for root, voices, mode in layout:
                levels = {struct.unpack_from('>H',output,0x1c+142*(root+v)+0x5e)[0] for v in range(voices)}
                if len(levels)>1:
                    assert mode, (layout, root, block, levels)
                    distinct.add(root)
            counts.append(count)
            full_counts.append(full_count)
        assert distinct == {root for root, _, mode in layout if mode}, (layout, distinct)
        if shared:
            assert sum(counts)<sum(full_counts), (layout, sum(counts), sum(full_counts))
        print('M/P.MOD', layout, 'steady instructions', counts[-1])
        del expected, actual
        gc.collect()
    print(f'PASS: native modulation and LFO output matched the full-run oracle in {len(layouts)*24} digital, analog and mixed blocks')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core', type=Path, required=True)
    parser.add_argument('--bank', type=Path, required=True)
    parser.add_argument('--modulation', action='store_true', help='also exercise the real EMAC modulation engines')
    args = parser.parse_args()
    verify(args.core, args.bank)
    track_relations(args.core, args.bank)
    poly_sound_locks(args.core, args.bank)
    track_options(args.core, args.bank)
    unison(args.core, args.bank)
    if args.modulation:
        modulation_modes(args.core, args.bank)
