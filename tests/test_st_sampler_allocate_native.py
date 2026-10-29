"""Execute the linked SLICE menu and real native pattern locks with a core.

The core's actual machine metadata/ID adapters and native per-step getter,
setter, clearer, trig predicates and pattern matrices execute in Unicorn.
App ownership/model data getters and notification queues are explicit fixtures.
No full boot, physical device, filesystem or MIDI operation is performed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader.sdk.elf import Elf
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
from unicorn.m68k_const import (UC_CPU_M68K_CFV4E, UC_M68K_REG_A7,
    UC_M68K_REG_SR, UC_M68K_REG_D0, UC_M68K_REG_PC)


def verify(bank, core):
    raw = bank.read_bytes()
    report = json.loads(bank.with_suffix('.smz4.json').read_text())
    assert hashlib.sha256(raw).hexdigest() == report['sha256']
    verification = json.loads((core/'verification.json').read_text())
    main = (core/'main.bin').read_bytes()
    assert hashlib.sha256(main).hexdigest() == verification['output']['main']['sha256']
    layout = verification['link']
    symbols = verification['symbols']
    elf = Elf.load(bank.with_suffix('.work')/'main.elf')
    local = {s.name: s.value for s in elf.symbols if s.name}
    base, view, app, settings, selection = 0x44000000, 0x45000000, 0x45001000, 0x45002000, 0x45003000
    manager, proxy, matrix_model, matrix = 0x45004000, 0x45005000, 0x45006000, 0x45100000
    tracks, pages, event, fake_vtable = 0x45200000, 0x45008000, 0x45008100, 0x45009000
    stop, get_data, notify = 0x403b0000, 0x403b0100, 0x403b0200
    stack = 0x47fff000
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    uc.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
    for address, size in ((0x40000000, 0x400000), (0x41b90000, 0x20000),
                          (base, 0x100000), (view, 0x300000), (0x46000000, 0x10000),
                          (0x47000000, 0x100000), (0x47ff0000, 0x10000)):
        uc.mem_map(address, size)
    uc.mem_write(0x40000400, main)
    lo, hi = layout['ddr']
    uc.mem_write(lo, main[layout['run_load']-0x40000400:layout['run_load']-0x40000400+hi-lo])
    length, rels = struct.unpack_from('>II', raw, 52)
    image = bytearray(raw[80+4*rels:80+4*rels+length])
    for at in struct.unpack_from('>'+str(rels)+'I', raw, 80):
        value = struct.unpack_from('>I', image, at)[0]
        struct.pack_into('>I', image, at, value+base)
    uc.mem_write(base, bytes(image))

    def put(address, *values):
        uc.mem_write(address, struct.pack('>'+str(len(values))+'I', *[v & 0xffffffff for v in values]))

    def word(address):
        return struct.unpack('>I', uc.mem_read(address, 4))[0]

    def half(address):
        return struct.unpack('>h', uc.mem_read(address, 2))[0]

    def ret(value=0):
        sp = uc.reg_read(UC_M68K_REG_A7)
        uc.reg_write(UC_M68K_REG_D0, value & 0xffffffff)
        uc.reg_write(UC_M68K_REG_PC, word(sp))
        uc.reg_write(UC_M68K_REG_A7, sp+4)

    state = dict(track=4, grid=0, selecting=0, invalidations=0, notifications=0)
    native = {address: 0 for address in (0x40037942, 0x400387b4, 0x400379a8,
                                        0x40017602, 0x4001722a, 0x4001741e,
                                        0x4002787c, 0x400278d8, 0x40027f3a)}
    heap = 0x47000000

    def hook(uc, pc, size, user):
        nonlocal heap
        sp = uc.reg_read(UC_M68K_REG_A7)
        if pc in native:
            native[pc] += 1
        if pc == 0x4010350c:
            result = heap
            heap += (word(sp+4)+15) & ~15
            ret(result)
        elif pc == get_data:
            model = word(sp+4)
            ret(word(model+16))
        elif pc == notify:
            state['notifications'] += 1
            ret()
        elif pc == 0x4016e804:
            ret(app)
        elif pc == 0x40018458:
            assert word(sp+4) == app
            ret(settings)
        elif pc == 0x4001ff74:
            ret(state['track'])
        elif pc == 0x40019000:
            assert word(sp+4) == app
            ret(manager)
        elif pc == 0x4001c422:
            ret(proxy)
        elif pc == 0x40030622:
            ret(69)
        elif pc == 0x40039160:
            assert word(sp+8) == 0x29f
            uc.mem_write(word(sp+12), b'\0')
            ret(state['grid']<<8)
        elif pc in (0x400c2d2a, 0x400c34d6):
            ret(0)
        elif pc == 0x400c3294:
            ret(state['selecting'])
        elif pc == 0x400c2caa:
            ret(4711)
        elif pc == 0x400ffc5e:
            state['invalidations'] += 1
            ret()
        elif pc == 0x400c33d0:
            ret()
        elif pc == 0x40029faa:
            raise AssertionError('allocation attempted to create a trig')

    boundaries = set(native) | {0x4010350c, get_data, notify, 0x4016e804, 0x40018458,
        0x4001ff74, 0x40019000, 0x4001c422, 0x40030622, 0x40039160, 0x400c2d2a,
        0x400c34d6, 0x400c3294, 0x400c2caa, 0x400ffc5e, 0x400c33d0, 0x40029faa}
    for address in boundaries:
        uc.hook_add(UC_HOOK_CODE, hook, begin=address, end=address)

    def call(address, *args):
        uc.reg_write(UC_M68K_REG_SR, 0x2700)
        uc.reg_write(UC_M68K_REG_A7, stack)
        put(stack, stop, *args)
        try:
            uc.emu_start(address, stop, count=500000)
        except Exception as error:
            raise RuntimeError('PC '+hex(uc.reg_read(UC_M68K_REG_PC))) from error
        assert uc.reg_read(UC_M68K_REG_PC) == stop, hex(uc.reg_read(UC_M68K_REG_PC))
        value = uc.reg_read(UC_M68K_REG_D0)
        return value if value < 0x80000000 else value-0x100000000

    put(fake_vtable+0x28, get_data)
    put(fake_vtable+0x10, notify)
    put(matrix_model, fake_vtable)
    put(matrix_model+16, matrix)
    for t in range(13):
        model = manager+(t+1)*0x70
        put(model, fake_vtable)
        put(model+16, tracks+t*0x499)
        put(model+0x2c, matrix_model)
        put(model+0x38, t)
    put(view, 0x401ca0dc)
    put(view+0x68, app, selection, 0, settings)
    put(view+0x7c, pages)
    put(pages, 6)
    put(view+0x88, 0x14)
    put(proxy, 0x401c3aac)
    put(symbols['core:core_machine_ui_definitions'], base+local['sp_slice_ui'])
    assert call(symbols['core:core_machine_bind'], base+local['sp_slice'], 1) == 1
    ui = symbols['core:core_machine_ui_api']
    api = event+0x100
    put(api, 3, ui)
    assert call(base+local['sp_input_init'], api) == 0

    def tap(key):
        for flags in (1, 0):
            put(event+12, key, flags)
            assert call(base+local['sp_input_key_pre'], view, event, 1) == 1

    def lock(t, step, parameter=0x16):
        return matrix+t*0x2447+step*144+parameter*2

    matrices_size = 13*0x2447
    allocations = 0
    for t in (4, 7, 11):
        state['track'] = t
        for page in (0, 1):
            put(view+0x90, page)
            for grid in range(5):
                state['grid'] = grid
                for track_length in (7, 16, 33, 64):
                    uc.mem_write(matrix, bytes([255])*matrices_size)
                    uc.mem_write(tracks, bytes(13*0x499))
                    record = tracks+t*0x499
                    uc.mem_write(record+0x3c9, struct.pack('>H', track_length))
                    steps = list(range(0, track_length, 3))
                    for i, step in enumerate(steps):
                        # Sound trigs and trigless locks are both placed native
                        # steps. Empty steps contain other unrelated lock data.
                        uc.mem_write(record+2*step, struct.pack('>H', 1 if i%2 else 0x801))
                    for step in range(64):
                        uc.mem_write(lock(t, step, 0x17), struct.pack('>H', 0x3210))
                    uc.mem_write(lock(t, 0), struct.pack('>H', 0x4000))
                    before = bytes(uc.mem_read(matrix, matrices_size))
                    trigs_before = bytes(uc.mem_read(tracks, 13*0x499))
                    tap(12)  # open
                    tap(14)  # LINEAR
                    tap(12)  # apply, close
                    assert not call(base+local['sp_allocate_active'], view)
                    expected = bytearray(before)
                    for ordinal, step in enumerate(steps):
                        at = lock(t, step)-matrix
                        struct.pack_into('>H', expected, at, ((ordinal%(4<<grid))+1)<<8)
                        row_at = t*0x2447+step*144
                        locked_count = sum(struct.unpack_from('>h',expected,row_at+2*p)[0]>=0
                                           for p in range(71))
                        struct.pack_into('>H',expected,row_at+142,locked_count)
                    # Word 71 of every step is the native lock-count summary;
                    # bytes 0x2400..0x2446 hold the track's lane summary. Compare
                    # all scalar values plus independently counted step totals.
                    after = bytes(uc.mem_read(matrix, matrices_size))
                    for row in range(13):
                        start, end = row*0x2447, row*0x2447+0x2400
                        differences = [(hex(i-start), after[i], expected[i]) for i in range(start,end)
                                       if after[i] != expected[i]]
                        assert not differences, (t,page,grid,track_length,row,differences[:12],native)
                    assert bytes(uc.mem_read(tracks, 13*0x499)) == trigs_before
                    tap(12)
                    tap(15)
                    tap(12)
                    assert not call(base+local['sp_allocate_active'], view)
                    for step in steps:
                        value = half(lock(t, step))
                        assert 256 <= value <= (4<<grid)*256 and not value & 255
                    assert bytes(uc.mem_read(tracks, 13*0x499)) == trigs_before
                    allocations += 2
    # Run the borrowed native clear service as used by rollback, and verify
    # that absence (-1) and unrelated locks survive that exact proxy route.
    assert call(0x40037942, view, 0, 0x29d) >= 0
    call(0x400379a8, view, 0, 0x29d)
    assert call(0x40037942, view, 0, 0x29d) == -1
    assert half(lock(state['track'], 0, 0x17)) == 0x3210
    assert all(native.values()), native
    result = dict(ok=True, bank_sha256=report['sha256'], core=str(core),
                  core_main_sha256=hashlib.sha256(main).hexdigest(),
                  completed_native_allocations=allocations,
                  real_native_calls={hex(k):v for k,v in native.items()},
                  scalar_matrices_and_unrelated_trig_bytes_verified=True,
                  application_and_notification_boundaries_are_fixtures=True,
                  full_boot=False, hardware_validated=False, device_access=False)
    name = 'native-allocate-'+core.name+'.json'
    (bank.parent/name).write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bank', type=Path, required=True)
    parser.add_argument('--core', type=Path, required=True)
    args = parser.parse_args()
    verify(args.bank.resolve(), args.core.resolve())
