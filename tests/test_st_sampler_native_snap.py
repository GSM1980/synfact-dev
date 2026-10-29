"""Narrow ColdFire/native snap fixture; synthetic UI context, never a full boot.

Execute the built sampler's encoder callback and real stock increment, snap
gate, periodic decay and ordered-list lookup. Native model/edit services and
the borrowed runtime policy object are explicit doubles. No device access.
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
from elekloader.syx import Syx
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
from unicorn.m68k_const import (UC_CPU_M68K_CFV4E, UC_M68K_REG_SR,
    UC_M68K_REG_A7, UC_M68K_REG_D0, UC_M68K_REG_PC)


def verify(bank, stock):
    body = bank.read_bytes()
    report = json.loads(bank.with_suffix('.smz4.json').read_text())
    assert hashlib.sha256(body).hexdigest() == report['sha256']
    main = Syx.load(stock).section(3)
    digest = hashlib.sha256(main).hexdigest()
    assert digest == '8cf2b4b4ea80d58953da46ca453328454fb3fca5cb5f844a2f7bad4b72241f53'
    elf = Elf.load(bank.with_suffix('.work') / 'main.elf')
    symbols = {s.name: s.value for s in elf.symbols if s.name}
    base, view, reference = 0x44000000, 0x45000000, 0x45000400
    ui, api, event, policy, increment = [view + n for n in (0x1000, 0x1100, 0x1200, 0x1300, 0x1400)]
    stop, stack, edit = 0x403b0000, 0x47fff000, 0x403b0100
    size, count = struct.unpack_from('>II', body, 52)
    at = 80 + 4*count
    image = bytearray(body[at:at+size])
    for offset in struct.unpack_from('>'+str(count)+'I', body, 80):
        value = struct.unpack_from('>I', image, offset)[0]
        struct.pack_into('>I', image, offset, value+base)
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    uc.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
    for address, length in ((0x40000000, 0x400000), (0x40600000, 0x100000),
                            (base, 0x100000), (view, 0x10000), (0x47ff0000, 0x10000)):
        uc.mem_map(address, length)
    uc.mem_write(0x40000400, main)
    uc.mem_write(base, bytes(image))

    def put(address, *values):
        uc.mem_write(address, struct.pack('>'+str(len(values))+'I', *[v & 0xffffffff for v in values]))

    def word(address):
        return struct.unpack('>I', uc.mem_read(address, 4))[0]

    def signed(value):
        return value if value < 0x80000000 else value - 0x100000000

    def ret(value=0):
        sp = uc.reg_read(UC_M68K_REG_A7)
        uc.reg_write(UC_M68K_REG_D0, value & 0xffffffff)
        uc.reg_write(UC_M68K_REG_PC, word(sp))
        uc.reg_write(UC_M68K_REG_A7, sp+4)

    state = dict(raw=0x4000, type=66, func=1, edits=0)
    native_calls = dict(increment=0, arm=0, blocked=0, lookup=0, periodic=0)
    addresses = {0x400f68d2: 'increment', 0x400f6ad4: 'arm',
                 0x400f6b02: 'blocked', 0x4007b97e: 'lookup', 0x400f6818: 'periodic'}

    def hook(uc, pc, length, user):
        sp = uc.reg_read(UC_M68K_REG_A7)
        if pc in addresses:
            native_calls[addresses[pc]] += 1
        if pc == 0x40030622:
            assert word(sp+4) == view
            ret(state['type'])
        elif pc == 0x40102d7c:
            ret(state['func'] if word(sp+4) == 1 else 0)
        elif pc == 0x4007b9b2:
            assert word(sp+4) == 0x1f1
            ret(policy)
        elif pc == 0x400c2caa:
            ret(300)  # Explicit native clock boundary.
        elif pc == 0x40039160:
            assert word(sp+4) == view
            uc.mem_write(word(sp+12), b'\0')
            ret(state['raw'])
        elif pc == 0x400c2d2a:
            ret(0)
        elif pc == edit:
            assert word(sp+4) == view and word(sp+16) == 0
            state['raw'] += signed(word(sp+12))
            state['edits'] += 1
            ret()

    # Trace native call boundaries only; periodic decay otherwise executes
    # thousands of instructions per vector and needs no Python interception.
    boundaries = set(addresses) | {0x40030622, 0x40102d7c, 0x4007b9b2,
                                  0x400c2caa, 0x40039160, 0x400c2d2a, edit}
    for address in boundaries:
        uc.hook_add(UC_HOOK_CODE, hook, begin=address, end=address)

    def call(address, *args):
        uc.reg_write(UC_M68K_REG_SR, 0x2700)
        uc.reg_write(UC_M68K_REG_A7, stack)
        put(stack, stop, *args)
        uc.emu_start(address, stop, count=50000)
        assert uc.reg_read(UC_M68K_REG_PC) == stop, hex(uc.reg_read(UC_M68K_REG_PC))
        return signed(uc.reg_read(UC_M68K_REG_D0))

    put(view+0x88, 0x14)
    put(ui, 7)
    put(ui+24, edit)
    put(api, 3, ui)
    # Synthetic policy object with the borrowed native TUNE increment layout.
    # It deliberately exercises thresholds, pressed scaling and native decay.
    uc.mem_write(increment, bytes.fromhex('00010100') + struct.pack('>6i', 0, 6, 1, 0, 6, 1))
    put(policy+4, 1, 2)
    put(policy+16, increment)
    assert call(base+symbols['sp_input_init'], api) == 0
    # Independent literal oracle list, separate from the relocated production
    # table: wrong notes or bad internal relocations cannot pass by agreement.
    list_address = view+0x1500
    notes = (-60, -48, -36, -24, -12, 0, 12, 24)
    put(list_address, list_address+12, list_address+96, 0)
    for i, note in enumerate(notes, 1):
        put(list_address+12*i, list_address+12*(i+1) if i<8 else list_address,
            list_address+12*(i-1), note)
    raw_gate = view+0x94+0x24
    ref_gate = reference+0x24
    ref_active = 0
    expected_raw = 0x4000
    events = 0

    def tick(n):
        for _ in range(n):
            call(0x400f6818, view+0x94, 0)
            call(0x400f6818, reference, 0)

    def turn(delta, pressed):
        nonlocal ref_active, expected_raw, events
        put(event+16, delta)
        uc.mem_write(event+20, bytes([pressed]))
        # Independent orchestration of the documented stock source-view path,
        # using the real firmware routines and a separate native state block.
        inc_delta = call(0x400f66a2, event, 1, 2)
        processed = call(0x400f68d2, reference, 0, inc_delta, increment, pressed)
        did_edit = False
        if processed:
            if not ref_active:
                call(0x400f6ad4, reference, 0)
            ref_active = 1
            if not (call(0x400f6b02, reference, 0) & 255):
                note = call(0x4007b97e, (expected_raw-0x4000)//256, processed, list_address)
                expected_raw = 0x4000+256*note
                call(0x400f6ad4, reference, 0)
                did_edit = True
        before = state['edits']
        assert call(base+symbols['sp_input_encoder'], view, event,
                    0x1f9+(state['type']-64)*32) == 1
        assert state['raw'] == expected_raw
        assert state['edits']-before == int(did_edit)
        assert word(raw_gate) == word(ref_gate)
        assert bytes(uc.mem_read(view+0x94+32, 20)) == bytes(uc.mem_read(reference+32, 20))
        events += 1

    values = (0x0400, 0x1000, 0x1c00, 0x2800, 0x3400, 0x4000, 0x4c00, 0x5800,
              0x27ff, 0x3401, 0x3fff, 0x4001)
    for machine in (66, 67, 69):
        state['type'] = machine
        for page in (0, 1):
            put(view+0x90, page)
            for pressed in (0, 255):
                for raw in values:
                    for direction in (-1, 1):
                        uc.mem_write(view+0x94, bytes(0x100))
                        uc.mem_write(reference, bytes(0x100))
                        call(0x400f6730, view+0x94)
                        call(0x400f6730, reference)
                        uc.mem_write(0x4060c748, b'\0')
                        ref_active = 0
                        state['raw'] = expected_raw = raw
                        for delta, delay in ((1, 48), (5, 0), (6, 0), (1, 3), (6, 1),
                                             (6, 0), (12, 0), (1, 4), (24, 48),
                                             (-6, 0), (-12, 4), (0, 48)):
                            tick(delay)
                            turn(delta*direction, pressed)
    assert all(native_calls.values())
    assert native_calls['arm'] and state['edits']
    result = dict(ok=True, bank_sha256=report['sha256'], stock_main_sha256=digest,
                  target_encoder_events=events, real_native_calls=native_calls,
                  native_increment_and_snap_gate_state_match=True,
                  ui_model_clock_policy_and_edit_are_fixtures=True,
                  full_boot=False, device_access=False, hardware_validated=False)
    (bank.parent/'native-snap-validation.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bank', required=True, type=Path)
    parser.add_argument('--stock', required=True, type=Path)
    args = parser.parse_args()
    verify(args.bank.resolve(), args.stock.resolve())
