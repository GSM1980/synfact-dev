"""Execute a built FM adapter and pinned original Digitone DSP in ColdFire.

Explicit fixtures: scheduler calls, extended-control API, clock, pitch and gates.
Raw mono Q31 before Syntakt AMP/filter/FX; no boot, POLY allocator or hardware.
Requires a Unicorn build with native ColdFire EMAC (including MAC-with-load).
The retained x2 branch is selected only in emulator memory, never in the bank.
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
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN
import unicorn.m68k_const as r

DN_SHA = 'e7bc77121df2cd5edd29cb0242cd9159ac522af1922ac69eb49065f24de50751'
ST_SHA = 'daf6451cf9587c0b628e901b7bb6b25f4e2633d448c534c0c181dd35ec783bc2'
RETURN, STACK = 0x48000000, 0x47fff000
PRESERVED = [getattr(r, 'UC_M68K_REG_'+prefix+str(i))
             for prefix, indexes in (('D', range(2, 8)), ('A', range(2, 7))) for i in indexes]


def sha(data):
    return hashlib.sha256(data).hexdigest()


class CPU:
    def __init__(self, areas):
        self.uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        self.uc.ctl_set_cpu_model(r.UC_CPU_M68K_CFV4E)
        for address, size in [*areas, (0x47ff0000, 0x10000), (RETURN, 0x10000)]:
            self.uc.mem_map(address, size)
        self.uc.reg_write(r.UC_M68K_REG_SR, 0x2700)
        self.uc.mem_write(RETURN+0x10, bytes.fromhex('a93c000000a0'))  # native MACSR
        self.uc.emu_start(RETURN+0x10, RETURN+0x16)

    def write32(self, at, value):
        self.uc.mem_write(at, struct.pack('>I', value & 0xffffffff))

    def read32(self, at):
        return struct.unpack('>I', self.uc.mem_read(at, 4))[0]

    def call(self, address, *args):
        self.uc.mem_write(STACK, struct.pack('>'+str(1+len(args))+'I', RETURN, *args))
        self.uc.reg_write(r.UC_M68K_REG_A7, STACK)
        saved = [self.uc.reg_read(reg) for reg in PRESERVED]
        self.uc.emu_start(address, RETURN, count=300000)
        assert self.uc.reg_read(r.UC_M68K_REG_PC) == RETURN, hex(address)
        assert self.uc.reg_read(r.UC_M68K_REG_A7) == STACK+4, 'Stack imbalance'
        assert saved == [self.uc.reg_read(reg) for reg in PRESERVED], 'Callee-save corruption'

    def state(self):
        # All four operator scalar states, including phase/increment and envelope.
        # +48..54 contain relocated wave pointers and cannot compare numerically.
        return b''.join(
            bytes(self.uc.mem_read(self.context+op*0x58, 0x48)) for op in range(4))


def require_emac():
    cpu = CPU([(0, 0x10000)])
    # Independent semantic check: native multiply BEFORE the simultaneous load.
    cpu.uc.mem_write(0x1000, bytes.fromhex('a907a1c1a3c1a89100c6a181'))
    cpu.uc.mem_write(0x2000, bytes.fromhex('12345678'))
    for reg, value in ((r.UC_M68K_REG_D7, 0x20), (r.UC_M68K_REG_D0, 0x40002000),
                       (r.UC_M68K_REG_D6, 0x40000001), (r.UC_M68K_REG_A1, 0x2000)):
        cpu.uc.reg_write(reg, value)
    cpu.uc.emu_start(0x1000, 0x100c, count=20)
    assert cpu.uc.reg_read(r.UC_M68K_REG_D1) == 0x20000000
    assert cpu.uc.reg_read(r.UC_M68K_REG_D4) == 0x12345678


class Port(CPU):
    def __init__(self, stock, section, address, symbols, mode, algorithm, phase):
        super().__init__([(0x40000000, 0x60000), (0x42000000, 0x100000),
                          (0x80000000, 0x10000)])
        self.symbols = symbols
        self.context = 0x80000570
        self.uc.mem_write(0x40000400, stock)
        self.uc.mem_write(address, section)
        self.call(0x400004bc)  # stock SRAM table copies; no boot/peripherals
        self.uc.mem_write(RETURN+0x40, bytes.fromhex('203c480020004e75'))
        self.uc.mem_write(0x40040f40, struct.pack('>2I', 0x454c0001, RETURN+0x40))
        self.write32(0x80008184, 1000)  # explicit synthetic clock
        self.uc.mem_write(symbols['fm_state']+0x31ec, bytes([mode]))
        # Descriptor values, not the adapter's historical fm_ext_defaults tail.
        extended = [0x6000, 0x2000, 0x7f00, 0x4000,
                    0x6000, 0x2000, 0x7f00, 0x4000,
                    0, 0x100, 0x100, phase, 0, 0x100, 0x100, 0x4000,
                    0, 0x4000, 0x4000, 0x4000]
        self.uc.mem_write(RETURN+0x2000, struct.pack('>20H', *extended))
        self.raw = [algorithm*256, 0x300, 0x300, 60*88,
                    round(70.5*256), 0, 0, 0, 0x2000]

    def block(self, trigger=False, release=False, note=69):
        # FDBK is consumed by update, as in native scheduler working parameters.
        self.uc.mem_write(RETURN+0x1024, struct.pack('>9H', *self.raw))
        self.uc.mem_write(0x80000038, struct.pack('>2I', int(trigger), int(release)))
        self.call(self.symbols['fm_update'], note << 16, 0x80000000, RETURN+0x1000)
        self.call(self.symbols['fm_render'], RETURN+0x3000, 0x80000000)
        return struct.unpack('>32i', self.uc.mem_read(RETURN+0x3000, 128))


class Digitone(CPU):
    def __init__(self, original, parameters):
        super().__init__([(0x40000000, 0x20000), (0x80000000, 0x10000)])
        self.context = 0x400167c8
        self.uc.mem_write(0x40000400, original)
        for lo, hi, target in ((0x400166e0, 0x40017cf0, 0x80000000),
                               (0x40017cf0, 0x4001f190, 0x80008000)):
            self.uc.mem_write(target, original[lo-0x40000400:hi-0x40000400])
        self.call(0x400004d8)  # bounded original BSS clear
        self.call(0x40000bfc)  # pure DSP initialization suffix
        self.call(0x40001fde, 4400)  # A4 = 440.0 Hz
        self.write32(0x80000378, 1000)
        self.uc.mem_write(0x800003a0, parameters)

    def block(self, trigger=False, release=False, note=69):
        p = 0x800003a0
        feedback, mix = struct.unpack('>2H', self.uc.mem_read(p+0x40, 4))
        self.write32(self.context+0x178, int(trigger))
        self.write32(self.context+0x16c, feedback << 16)
        for at, args in ((0x4000200a, (0, note << 16, p)),
                         (0x40000d94, (0, int(trigger), int(release), p)),
                         (0x4000326e, (0, int(trigger), p)),
                         (0x40002504, (0, p)), (0x40001d2a, (0, mix << 16)),
                         (0x40001bc6, (0,))):
            self.call(at, *args)
        return struct.unpack('>32i', self.uc.mem_read(0x80000e00, 128))


def verify(bank, stock_path, digitone_audio):
    require_emac()
    body = bank.read_bytes()
    report = json.loads(bank.with_suffix(bank.suffix+'.json').read_text())
    assert sha(body) == report['sha256']
    stock, original = Syx.load(stock_path).section(7), digitone_audio.read_bytes()
    assert sha(stock) == ST_SHA and sha(original) == DN_SHA
    elf = Elf.load(bank.with_suffix('.work')/'audio.elf')
    section = elf.section('.image')
    address = struct.unpack_from('>I', elf.raw,
        struct.unpack_from('>I', elf.raw, 32)[0]+section.idx*40+12)[0]
    symbols = {s.name: s.value for s in elf.symbols if s.name}
    audio_at = 48+struct.unpack_from('>I', body, 72)[0]
    offset = audio_at+address-0x4204f750
    assert body[offset:offset+len(section.data)] == section.data, 'ELF does not match bank'
    rows = []
    for mode in (0, 1):
        for algorithm in range(8):
            for phase in (0, 0x100, 0x400):
                port = Port(stock, section.data, address, symbols, mode, algorithm, phase)
                a = port.block(trigger=True)
                params = bytes(port.uc.mem_read(symbols['fm_state']+0xf28, 158))
                assert struct.unpack_from('>8H', params, 0x34) == (
                    algorithm*256, 0x300, 0x300, 120, round(70.5*256), 0, 0x2000, 0)
                assert struct.unpack_from('>H', params, 0x44)[0] == phase
                assert struct.unpack_from('>14H', params, 0x48) == (
                    0x6000, 0x2000, 0x7f00, 0x4000, 0x6000, 0x2000, 0x7f00, 0x4000,
                    0, 0x100, 0x100, 0, 0x100, 0x100)
                assert struct.unpack_from('>4H', params, 0x76) == (0x4000,)*4
                native = Digitone(original, params)
                b = native.block(trigger=True)
                maxdiff, jumps = 0, []
                samples, previous = [], None
                for block in range(33):
                    if block:
                        trig = block in (11, 23)
                        a = port.block(trigger=trig, release=block >= 28)
                        b = native.block(trigger=trig, release=block >= 28)
                    assert port.state() == native.state(), (mode, algorithm, phase, block)
                    difference = max(abs(x-y) for x, y in zip(a, b))
                    if mode:
                        assert difference == 0, (algorithm, phase, block, difference)
                    maxdiff = max(maxdiff, difference)
                    if block in (11, 23):
                        jumps.append(abs(a[0]-previous)/2**31)
                    samples.extend(a)
                    previous = a[-1]
                assert max(samples) != min(samples), 'Silent test case'
                immutable = symbols['fm_state']-address
                assert bytes(port.uc.mem_read(address, immutable)) == section.data[:immutable]
                assert bytes(native.uc.mem_read(0x40000400, 0x6390)) == original[:0x6390]
                rows.append(dict(x2=bool(mode), algorithm=algorithm+1, phrt=phase//256,
                                 pcm_max_difference=maxdiff, scalar_states_equal=True,
                                 retrigger_boundary_jumps_q31=jumps))
                print('PASS x%d ALGO %d PHRT %d' % (mode+1, algorithm+1, phase//256), flush=True)
    return dict(bank=str(bank), bank_sha256=sha(body), stock_audio_sha256=ST_SHA,
                digitone_audio_sha256=DN_SHA, cases=rows, blocks_per_case=33,
                matched_x2_cases=sum(row['x2'] for row in rows),
                raw_audio_before_amp_filter_fx=True, full_boot=False, hardware=False,
                poly_allocator_exercised=False, firmware_written=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bank', required=True, type=Path)
    parser.add_argument('--stock', required=True, type=Path)
    parser.add_argument('--digitone-audio', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    result = verify(args.bank.resolve(), args.stock, args.digitone_audio)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print('PASS 48 cases: 1,584 matching operator states; 24 x2 cases sample-exact')
