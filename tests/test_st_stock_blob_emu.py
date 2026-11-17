"""Reproduce native stock-sample writes into the resident core, offline.

Run with --stock <official OS 1.41.syx> --stock <official OS 1.42.syx>.
The actual stock blob builder, getter, memcpy and memset execute in Unicorn.
Only the sample-reader and malloc/free boundaries are fixtures. The reader
returns valid SDK-generated mono SAMP headers and PCM, without emulating eMMC,
the native file parser, project loading, a complete boot or AUDIO execution.
DDR aliases share host backing, modelling the 128 MiB address wrap also used
by analog-emu. A core-sized sentinel represents resident code/data; no core
build is needed. This is a corruption reproduction, not a fix regression.
"""
import argparse
import ctypes
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader import formats, syx
from elekloader.sdk.syntakt.long_samples import samp_body
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.m68k_const import (UC_CPU_M68K_CFV4E, UC_M68K_REG_A7,
    UC_M68K_REG_D0, UC_M68K_REG_PC, UC_M68K_REG_SR)

PROFILES = {
    '1.41': (0x400822bc, 0x4011320a, 0x4010350c, 0x40103712, 0x4d84f2c0),
    '1.42': (0x40082334, 0x401133ce, 0x401036d0, 0x401038d6, 0x4d84f2c0),
}
CORE, CORE_SIZE = 0x46000000, 65536
STOP, STACK, SCRATCH = 0x403f0000, 0x47fff000, 0x41000000


def reproduce(stock_path):
    stock, device, release = formats.load(stock_path)
    assert device.key == 'syntakt' and release.version in PROFILES
    image = stock.section(3)
    assert syx.sha(image) == release.main_sha256
    builder, reader, malloc, free, blob = PROFILES[release.version]
    pcm_start = blob + 5
    physical_start = pcm_start - 0x08000000
    gap = CORE - physical_start
    cases = [
        ('16 x 5 s', [240000]*16 + [0]*48),
        ('17 x 5 s', [240000]*17 + [0]*47),
        ('64 x 5 s', [240000]*64),
    ]
    # Keep all 64 four-byte length fields. The blob starts at an odd address,
    # so its maximum non-overlapping size is one byte smaller than the gap.
    edge_frames = (gap - 64*4)//2
    for label, total in [('last safe frame', edge_frames),
                         ('first overlapping frame', edge_frames+1)]:
        frames = []
        for _ in range(64):
            n = min(240000, total)
            frames.append(n)
            total -= n
        assert total == 0
        cases.append((label, frames))
    results = []
    for label, frames in cases:
        u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        u.mem_map(0x40000000, 0x400000)
        u.mem_write(device.main_load, image)
        u.mem_map(SCRATCH, 0x80000)
        u.mem_map(0x47ff0000, 0x10000)
        # [45800000,47800000) and [4d800000,4f800000) are the same RAM.
        backing = (ctypes.c_ubyte * 0x2000000)()
        for alias in (0x45800000, 0x4d800000):
            u.mem_map_ptr(alias, len(backing), 7, ctypes.addressof(backing))
        sentinel = b'\xa5'*CORE_SIZE
        u.mem_write(CORE, sentinel)
        reads, writes = [], []
        active = {'slot': None}

        def word(address):
            return struct.unpack('>I', u.mem_read(address, 4))[0]

        def ret(value=0):
            sp = u.reg_read(UC_M68K_REG_A7)
            u.reg_write(UC_M68K_REG_D0, value & 0xffffffff)
            u.reg_write(UC_M68K_REG_PC, word(sp))
            u.reg_write(UC_M68K_REG_A7, sp+4)

        def boundary(uc, pc, size, user):
            sp = u.reg_read(UC_M68K_REG_A7)
            if pc == malloc:
                assert word(sp+4) == 0x80000
                ret(SCRATCH)
            elif pc == free:
                assert word(sp+4) == SCRATCH
                ret()
            elif pc == reader:
                slot, header, dest, offset, capacity = struct.unpack(
                    '>5I', u.mem_read(sp+4, 20))
                assert slot == len(reads) and offset == 0 and capacity == 0x80000
                active['slot'] = slot+1
                reads.append(slot)
                if not frames[slot]:
                    # Stock negative-result path writes a zero length field.
                    ret(-1)
                    return
                pcm = struct.pack('>H', (slot+1)*257)*frames[slot]
                body, _ = samp_body(pcm, 'valid fixture')
                assert len(body) <= 480064 and len(body) == 64+2*frames[slot]
                assert word(sp+12) == SCRATCH
                # Native reader returns parsed SAMP metadata separately from PCM.
                u.mem_write(header, body[:16])
                u.mem_write(dest, body[64:])
                ret(0)

        def core_write(uc, access, address, size, value, user):
            if address+size > CORE+0x08000000 and not writes:
                writes.append(dict(slot=active['slot'],
                    pc=hex(u.reg_read(UC_M68K_REG_PC)),
                    alias_address=hex(address), physical_address=hex(address-0x08000000),
                    size=size))

        for address in (malloc, free, reader):
            u.hook_add(UC_HOOK_CODE, boundary, begin=address, end=address)
        u.hook_add(UC_HOOK_MEM_WRITE, core_write,
                   begin=CORE+0x08000000-16, end=CORE+0x08000000+CORE_SIZE-1)
        u.mem_write(STACK, struct.pack('>I', STOP))
        u.reg_write(UC_M68K_REG_SR, 0x2700)
        u.reg_write(UC_M68K_REG_A7, STACK)
        u.emu_start(builder, STOP, count=50000000)
        assert u.reg_read(UC_M68K_REG_PC) == STOP
        assert reads == list(range(64))
        expected = b''.join(struct.pack('>I', n) +
            struct.pack('>H', (slot+1)*257)*n for slot, n in enumerate(frames))
        assert word(blob+1) == len(expected), (label, hex(word(blob+1)), hex(len(expected)))
        assert bytes(u.mem_read(pcm_start, len(expected))) == expected
        core_after = bytes(u.mem_read(CORE, CORE_SIZE))
        overlap = max(0, min(CORE_SIZE, len(expected)-gap))
        assert core_after[:overlap] == expected[gap:gap+overlap]
        assert core_after[overlap:] == sentinel[overlap:]
        assert bool(writes) == bool(overlap), (label, writes, overlap)
        results.append(dict(case=label, seconds=sum(frames)/48000,
            blob_bytes=len(expected), core_bytes_overwritten=overlap,
            first_core_write=writes[0] if writes else None,
            reported_pc_bytes={hex(pc): core_after[pc-CORE:pc-CORE+2].hex()
                               for pc in (0x460001d0, 0x460001d6)}))
    return dict(version=release.version, main_sha256=syx.sha(image),
        blob_start_alias=hex(pcm_start), blob_start_physical=hex(physical_start),
        bytes_before_core=gap, mib_before_core=gap/(1024*1024),
        actual_native_builder_and_memory_copy=True,
        sample_reader_and_heap_are_fixtures=True,
        full_boot=False, hardware_validated=False, cases=results)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stock', type=Path, action='append', required=True)
    parser.add_argument('--out', type=Path)
    args = parser.parse_args()
    results = [reproduce(path) for path in args.stock]
    encoded = json.dumps(results, indent=2)+'\n'
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(encoded)
    print(encoded)
