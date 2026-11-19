"""Execute linked MAIN's native sender and AUDIO's native stock parser offline.

eMMC, native heap, semaphore wakeups, UI notifications and GPIO/mailbox delivery
are explicit boundaries. Actual target stock planning, page generation, envelope
cache, sender loop, release hook and both CPUs' memcpy execute. This is not a
complete RTOS boot, USB/MIDI test, machine initializer or hardware timing test.
"""
import argparse
import ctypes
import gc
import hashlib
import json
from pathlib import Path
import struct
import zlib
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader.sdk.elf import Elf
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
from unicorn.m68k_const import *

BASE, RET, SP = 0x40000400, 0x403ff000, 0x47fff000
STOCK_AUDIO = 0x4004f750


def run_case(folder, frames, machine_pages=0, service=True):
    report = json.loads((folder/'verification.json').read_text())
    image = (folder/'main.bin').read_bytes()
    assert hashlib.sha256(image).hexdigest() == report['output']['main']['sha256']
    sym, layout = report['symbols'], report['link']
    u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
    ram = (ctypes.c_ubyte * 0x8000000)()
    for alias in (0x40000000, 0x48000000):
        u.mem_map_ptr(alias, len(ram), 7, ctypes.addressof(ram))
    u.mem_map(0x10000000, 0x10000)
    u.mem_write(BASE, image)
    lo, hi = layout['ddr']
    u.mem_write(lo, image[layout['run_load']-BASE:layout['run_load']-BASE+hi-lo])
    u.mem_write(layout['bss'][0], bytes(layout['bss'][1]-layout['bss'][0]))
    # C function bodies must survive every sample write, including aliases.
    executable = {}
    for name in ('sample-pool.c', 'sample-wave.c'):
        obj = Elf.load(ROOT/'mods/core-st/out/core.work'/(name+'.o'))
        for s in obj.symbols:
            if s.size and s.type == 2 and 'core:'+s.name in sym:
                at = sym['core:'+s.name]
                executable[at] = bytes(u.mem_read(at, s.size))
    audio = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    audio.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
    audio.mem_map(0x40000000, 0x8000000)
    audio.mem_write(BASE, (folder/'audio.bin').read_bytes())
    samples = [b'SAMP'+bytes(4)+struct.pack('>I', n)+bytes(52)+
               struct.pack('>H', (i+1)*257)*n for i, n in enumerate(frames)]
    expected = b''.join(struct.pack('>I', n)+sample[64:]
                        for n, sample in zip(frames, samples))
    heap, allocations, freed, kicks, reads = 0x47000000, {}, [], [], 0

    def put(address, *values):
        u.mem_write(address, struct.pack('>'+str(len(values))+'I', *values))

    def word(address):
        return struct.unpack('>I', u.mem_read(address, 4))[0]

    def ret(value=0):
        sp = u.reg_read(UC_M68K_REG_A7)
        u.reg_write(UC_M68K_REG_D0, value & 0xffffffff)
        u.reg_write(UC_M68K_REG_PC, word(sp))
        u.reg_write(UC_M68K_REG_A7, sp+4)

    total_pages = None
    wave_calls = 0

    def boundary(uc, pc, size, user):
        nonlocal heap, reads, wave_calls
        sp = u.reg_read(UC_M68K_REG_A7)
        if pc in (sym['core:core_pool_wave_begin'],sym['core:core_pool_wave_feed'],sym['core:core_pool_wave_finish']):
            wave_calls += 1
        elif pc == sym['core:fw_malloc']:
            n = word(sp+4)
            assert 0 < n < 0x20000, n  # no aggregate sample allocation
            at = heap
            heap += (n+15) & ~15
            assert heap < 0x47100000
            allocations[at] = n
            ret(at)
        elif pc == sym['core:fw_free']:
            at = word(sp+4)
            assert at in allocations and at not in freed
            freed.append(at)
            u.mem_write(at, b'\xdd'*allocations[at])
            ret()
        elif pc == sym['core:fw_mmc_read']:
            lba, n, dest = struct.unpack('>3I', u.mem_read(sp+4, 12))
            assert 8*1024 <= lba < 72*1024, hex(lba)
            limit = 5120 if total_pages is not None and len(kicks) == total_pages else 3072
            assert n <= limit and not n % 512
            slot, offset = (lba>>10)-8, (lba & 1023)*512
            payload = samples[slot][offset:offset+n]
            u.mem_write(dest, payload+bytes(n-len(payload)))
            reads += 1
            ret()
        elif pc == 0x400829f4:  # UI notification queue only
            ret()
        elif pc == 0x4000171e:  # native sender semaphore
            assert total_pages is not None
            if len(kicks) == total_pages:
                u.emu_stop()
            else:
                ret()
        elif pc == 0x40082f6a:  # native kick; receiver has its own RAM
            wire = struct.unpack('>H', u.mem_read(0x1000000c, 2))[0]
            audio.mem_write(STOCK_AUDIO+2048*wire, bytes(u.mem_read(0x10000800, 2048)))
            kicks.append(wire)
            ret()

    for pc in (sym['core:fw_malloc'], sym['core:fw_free'], sym['core:fw_mmc_read'],
               sym['core:core_pool_wave_begin'],sym['core:core_pool_wave_feed'],sym['core:core_pool_wave_finish'],
               0x400829f4, 0x4000171e, 0x40082f6a):
        u.hook_add(UC_HOOK_CODE, boundary, begin=pc, end=pc)

    def call(address, *args):
        put(SP, RET, *args)
        u.reg_write(UC_M68K_REG_SR, 0x2700)
        u.reg_write(UC_M68K_REG_A7, SP)
        u.emu_start(address, RET, count=2000000)
        assert u.reg_read(UC_M68K_REG_PC) == RET, hex(u.reg_read(UC_M68K_REG_PC))
        return u.reg_read(UC_M68K_REG_D0)

    if not service:
        put(sym['core_pool_image'], 0)
    # Execute the actual boot replacement, stopping at the original task
    # startup boundary. The unsafe native aggregate builder must not run.
    put(SP, RET)
    u.reg_write(UC_M68K_REG_SR, 0x2700)
    u.reg_write(UC_M68K_REG_A7, SP)
    u.emu_start(sym['core:core_pool_blob'], 0x40082e9c, count=2000000)
    assert u.reg_read(UC_M68K_REG_PC) == 0x40082e9c
    assert word(sym['core:core_pool_stock_bytes']) == len(expected)
    scratch = next(at for at, n in allocations.items() if n == 5120)
    stock_pages = (len(expected)+2047)//2048
    extra = word(sym['core:core_pool_boot_pages'])
    assert extra == (2 if service else 0)
    machine_data = bytes([0x37])*2048*machine_pages
    bank_file = None
    if machine_pages:
        # 2.1.1: machine-boot.c leaves the whole bank file (MAIN's part copied out)
        # as core_machine_transfer, its AUDIO section at core_machine_audio.
        bank_file, main_part = heap, 0x800
        heap += main_part+len(machine_data)
        allocations[bank_file] = main_part+len(machine_data)
        u.mem_write(bank_file+main_part, machine_data)
        put(sym['core:core_machine_pages'], machine_pages)
        put(sym['core:core_machine_audio'], bank_file+main_part)
        put(sym['core:core_machine_transfer'], bank_file)
    total_pages = stock_pages+machine_pages+extra
    put(0x41bbf424, 0)
    put(SP, RET)
    u.reg_write(UC_M68K_REG_SR, 0x2700)
    u.reg_write(UC_M68K_REG_A7, SP)
    u.emu_start(0x40082f90, RET, count=2000000000)
    assert len(kicks) == total_pages, (len(kicks), total_pages)
    assert kicks == list(range(stock_pages))+list(range(0x4000, 0x4000+machine_pages))+(
        [0x4162, 0x4163] if service else [])
    assert scratch in freed and freed.count(scratch) == 1
    if machine_pages:  # freed once, after its last page reached the mailbox (the receiver's copy checks it)
        assert freed.count(bank_file) == 1
        assert word(sym['core:core_machine_transfer']) == 0 and word(sym['core:core_machine_audio']) == 0
    assert wave_calls == 0  # no sample waveform work during boot
    assert all(n < 100000 for n in allocations.values())  # no waveform cache allocation yet
    assert word(0x41bb32b0) == 4  # native final state after the last mailbox copy
    assert bytes(audio.mem_read(STOCK_AUDIO, len(expected))) == expected
    padded = stock_pages*2048-len(expected)
    assert bytes(audio.mem_read(STOCK_AUDIO+len(expected), padded)) == bytes(padded)
    if machine_pages:
        assert bytes(audio.mem_read(STOCK_AUDIO+0x4000*2048, len(machine_data))) == machine_data
    if service:
        # 2.1.1: MAIN keeps only the service's header and code resident and rebuilds
        # the rest; AUDIO must receive the builder's image (checksum, channel bytes
        # and descriptors are MAIN's, rewritten at the last pages).
        template = (folder/'pool-audio'/'core-pool.bin').read_bytes()
        received = bytes(audio.mem_read(0x42100750, 4096))
        assert received[:8] == template[:8] and received[12:0xb80] == template[12:0xb80]
        assert struct.unpack('>I', received[8:12])[0] == zlib.adler32(received[12:]) & 0xffffffff
    for address, code in executable.items():
        assert bytes(u.mem_read(address, len(code))) == code, hex(address)
    # The actual native CPU2 parser must agree with the core's descriptors.
    audio.mem_write(SP, struct.pack('>I', RET))
    audio.reg_write(UC_M68K_REG_SR, 0x2700)
    audio.reg_write(UC_M68K_REG_A7, SP)
    audio.emu_start(0x40000d06, RET, count=2000000)
    assert audio.reg_read(UC_M68K_REG_PC) == RET
    offset = 0
    for slot, n in enumerate(frames):
        actual_n, actual_p = struct.unpack('>II', audio.mem_read(0x4404f754+8*slot, 8))
        offset += 4
        assert (actual_n, actual_p) == (n, STOCK_AUDIO+offset), (slot, actual_n, hex(actual_p))
        if service:
            pool_n, pool_p = struct.unpack('>II', audio.mem_read(0x42101350+8*slot, 8))
            assert (pool_n, pool_p) == (n, STOCK_AUDIO+offset if n else 0)
        offset += 2*n
    if service:
        wave = call(sym['core:core_pool_wave'], 0)
        assert wave == 0
        before = reads
        for _ in range((frames[0]+2047)//2048):
            call(sym['core:core_pool_wave_idle'], 0, 2048)
        wave = call(sym['core:core_pool_wave'], 0)
        assert reads > before and wave and word(wave+4) == frames[0] and word(wave+8) == 2
        before = reads
        call(sym['core:core_pool_wave_idle'], 0, 2048)
        assert reads == before  # cached waveform needs no IO
    call(sym['core:core_pool_boot_release'])  # idempotent
    call(sym['core:core_machine_audio_release'])  # idempotent
    assert all(at in freed for at,n in allocations.items() if n == 5120)
    return dict(samples=64, pcm_frames=sum(frames), stock_bytes=len(expected),
        stock_pages=stock_pages, machine_pages=machine_pages, service=service,
        native_sender_and_audio_parser=True, core_code_preserved=True,
        transient_pcm_bytes=allocations[scratch], scratch_freed=True, bank_file_freed=bool(machine_pages),
        boot_waveform_calls=0, lazy_waveform_verified=service,
        disk_reads=reads, full_boot=False, hardware_validated=False)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core', type=Path, required=True)
    p.add_argument('--out', type=Path)
    args = p.parse_args()
    cases = [([240000]*64, 0, True), ([1,1022,1023,1024,2047,240000]+[0]*58, 2, True),
             ([4]*64, 0, False)]
    results = []
    for frames, machine_pages, service in cases:
        result = run_case(args.core, frames, machine_pages, service)
        results.append(result)
        print(json.dumps(result), flush=True)
        gc.collect()
    if args.out:
        args.out.write_text(json.dumps(results, indent=2)+'\n')
