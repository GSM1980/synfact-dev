"""Real linked Digi Mono AUDIO in ColdFire, including native Syntakt gain calls.

Needs a built bank with sibling .work/audio.elf, stock Syntakt 1.41, a native
64-bit host compiler and Unicorn with ColdFire EMAC (LIBUNICORN_PATH).
Compares the oscillator PCM against the unchanged host engine and verifies
ABI/track isolation. Runs native TwinShot update/declick/gain/mix, but does
not boot a device or simulate the scheduler, FLTR/AMP/FX or hardware cycles.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import random
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader.sdk.elf import Elf
from elekloader.syx import Syx
from test_st_fm_audio import CPU, RETURN, PRESERVED, require_emac
from unicorn import UC_HOOK_CODE

SLOTS = (0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x1a, 0x19)
PATCHES = ((64, 0, 0, 0, 0, 0, 0, 0), (64, 0, 0, 0, 0, 0, 0, 0),
           (64, 0, 40, 0, 0, 0, 0, 0), (64, 0, 40, 0, 0, 64, 0, 40),
           (64, 63, 63, 63, 0, 64, 0, 127), (64, 43, 113, 64, 0, 0, 40, 100),
           (64, 63, 63, 63, 64, 64, 0, 0), (64, 0, 0, 0, 16, 64, 0, 127),
           (64,0,0,127,0,0,0,64),(64,61,64,80,32,82,64,64),
           (64,61,64,82,64,104,80,64),(64,64,64,64,0,74,80,32))
ENGINES = (0, 1, 2, 3, 4, 5, 6, 8, None, None, None, None)


def host_engine(cc, out):
    src = out / 'voice-size.c'
    src.write_text('#include "types.h"\nunsigned dm_voice_bytes(void){return sizeof(struct mono_voice);}\n'
                   'unsigned dm_m2k_bytes(void){return sizeof(struct dm_m2k_voice);}\n'
                   'unsigned dm_ens_bytes(void){return sizeof(struct dm_ens_voice);}\n')
    dll = (out / 'digimono-host.dll').resolve()
    subprocess.run([str(cc), '-O3', '-shared', '-I', 'mods/digimono-st', str(src),
                    'mods/digimono-st/engine.c', 'mods/digimono-st/sine.c', 'mods/digimono-st/mono2k.c', 'mods/digimono-st/fmplus.c',
                    '-o', str(dll)], cwd=ROOT, check=True)
    lib = ctypes.CDLL(str(dll))
    lib.dm_voice_bytes.restype = ctypes.c_uint
    lib.dm_m2k_bytes.restype = ctypes.c_uint
    lib.dm_ens_bytes.restype = ctypes.c_uint
    lib.dm_ens_init.argtypes = [ctypes.c_void_p]
    lib.dm_ens_trig.argtypes = [ctypes.c_void_p]
    lib.dm_ens_render.argtypes = [ctypes.c_void_p,ctypes.c_void_p,ctypes.c_int32,ctypes.c_void_p,ctypes.c_void_p]
    lib.dm_m2k_init.argtypes = [ctypes.c_void_p]
    lib.dm_m2k_trig.argtypes = [ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p]
    lib.dm_m2k_render.argtypes = [ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_int32,ctypes.c_int32,ctypes.c_void_p]
    lib.dm_mono_pitch_inc.argtypes = [ctypes.c_int32]
    lib.dm_mono_pitch_inc.restype = ctypes.c_uint32
    lib.dm_mono_init.argtypes = [ctypes.c_void_p]
    lib.dm_mono_trig.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.dm_mono_render.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p,
                                 ctypes.c_uint32, ctypes.c_void_p, ctypes.c_int]
    return lib


class HostVoice:
    def __init__(self,lib):
        self.lib=lib;self.kind=None
        self.old=ctypes.create_string_buffer(lib.dm_voice_bytes())
        self.new=ctypes.create_string_buffer(lib.dm_m2k_bytes())
        self.ens=ctypes.create_string_buffer(lib.dm_ens_bytes())

    def block(self,kind,knobs,note=60,trig=False,previous=None):
        lib=self.lib
        if self.kind!=kind:
            assert trig
            lib.dm_mono_init(self.old);lib.dm_m2k_init(self.new);lib.dm_ens_init(self.ens);self.kind=kind
        out=(ctypes.c_int16*32)()
        right=(ctypes.c_int16*32)()
        pitch=note*128+((knobs[0]-0x4000)>>1)
        if kind==4:
            params=(ctypes.c_uint16*7)(*(min(v,0x7f00) for v in knobs[1:]))
            if trig:lib.dm_ens_trig(self.ens)
            lib.dm_ens_render(self.ens,params,pitch>>1,out,right)
            return tuple(v*65536 for v in out),tuple(v*65536 for v in right)
        elif kind<8:
            params=(ctypes.c_uint8*7)(*(min(v>>8,127) for v in knobs[1:]))
            if trig:lib.dm_mono_trig(self.old,ENGINES[kind])
            lib.dm_mono_render(self.old,ENGINES[kind],params,lib.dm_mono_pitch_inc(pitch),out,32)
        else:
            params=(ctypes.c_uint16*7)(*(min(v,0x7f00) for v in knobs[1:]))
            if trig:lib.dm_m2k_trig(self.new,kind,params)
            if previous is None:previous=48*64+((params[6]*24)>>8)
            lib.dm_m2k_render(self.new,kind,params,pitch>>1,previous,out)
        mono=tuple(v*65536 for v in out)
        return mono,mono


class Port(CPU):
    def __init__(self, stock, image, address, symbols, stereo=True):
        # Empty native sample descriptors (TwinShot update reads 4404f754).
        super().__init__([(0x40000000, 0x60000), (0x42000000, 0x100000),
                          (0x44000000, 0x80000), (0x80000000, 0x10000)])
        self.symbols = symbols
        self.stereo = stereo and bool(symbols.get('st_stereo_claim'))
        self.uc.mem_write(0x40000400, stock)
        self.uc.mem_write(address, image)
        self.call(0x400004bc)  # stock SRAM table copies; no boot/peripherals
        # Explicit fixture: extended-value service returns our transported TYPE.
        self.uc.mem_write(RETURN+0x40, bytes.fromhex('203c480020004e75'))
        self.uc.mem_write(0x40040f40, struct.pack('>2I', 0x454c0001, RETURN+0x40))
        if self.stereo:self.write32(0x40040f30,0x454c5301)
        self.write32(0x80008184, 1000)
        for i,reg in enumerate(PRESERVED):
            self.uc.reg_write(reg,0x1a000000+i*0x1234)
        self.instructions = 0
        self.capture = []
        self.uc.hook_add(UC_HOOK_CODE, self.hook)

    def expected(self,pair):
        if self.stereo:return pair
        folded=tuple(((l//65536+r//65536)>>1)*65536 for l,r in zip(*pair))
        return folded,folded

    def hook(self, uc, address, size, data):
        self.instructions += 1
        # First native call after oscillator generation: capture the REAL buffer.
        if address == 0x400045a0:
            from unicorn.m68k_const import UC_M68K_REG_A7
            sp = uc.reg_read(UC_M68K_REG_A7)
            buf = struct.unpack('>I', uc.mem_read(sp+4, 4))[0]
            voice = struct.unpack('>I', uc.mem_read(sp+8, 4))[0]
            self.capture.append(struct.unpack('>32i', uc.mem_read(buf, 128)))
            self.ramps.append(self.read32(voice+0x3d4))
        elif address == 0x400049f4:
            from unicorn.m68k_const import UC_M68K_REG_A7
            sp = uc.reg_read(UC_M68K_REG_A7)
            voice = struct.unpack('>I', uc.mem_read(sp+20, 4))[0]
            self.starts.append(bytes(uc.mem_read(voice+0x18,8)))

    def block(self, track, kind, knobs, note=60, trig=False):
        voice = 0x80000000+track*0x708
        params = [0]*71
        for slot, value in zip(SLOTS, knobs):
            params[slot] = value
        self.uc.mem_write(RETURN+0x1000, struct.pack('>71H', *params))
        self.uc.mem_write(RETURN+0x2000, struct.pack('>24H', kind<<8, *([0]*23)))
        self.write32(voice+0x34, int(trig)); self.write32(voice+0x38, int(trig))
        self.write32(voice+0x56c, RETURN+0x4000+track*64)
        # Keep native AMP active; the native chain is exercised separately by OS.
        self.write32(0x800049e0+track*12, 1)
        self.instructions = 0; self.capture = [];self.ramps=[];self.starts=[]
        self.call(self.symbols['dm_update'], note<<16, voice, RETURN+0x1000)
        update = self.instructions; self.instructions = 0
        self.call(self.symbols['dm_render'], RETURN+0x3000, voice)
        assert len(self.capture) in (1,2)
        assert len(self.starts)==len(self.capture)
        if len(self.capture)==2:
            assert self.ramps[0]==self.ramps[1], 'R gain ramp advanced twice'
            assert self.starts[0]==self.starts[1], 'R mix ramp advanced twice'
        assert self.read32(voice+0x56c) == RETURN+0x4000+track*64
        return (self.capture[0],self.capture[-1]), (update, self.instructions)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bank', type=Path, required=True)
    p.add_argument('--stock', type=Path, required=True)
    p.add_argument('--host-cc', type=Path, required=True)
    p.add_argument('--out', type=Path, default=Path('out/digimono-emu'))
    a = p.parse_args()
    require_emac()
    a.out.mkdir(parents=True, exist_ok=True)
    lib = host_engine(a.host_cc, a.out)
    body = a.bank.read_bytes()
    report = json.loads(Path(str(a.bank)+'.json').read_text())
    assert hashlib.sha256(body).hexdigest() == report['sha256']
    elf = Elf.load(a.bank.with_suffix('.work')/'audio.elf')
    sec = elf.section('.image')
    symbols = {s.name: s.value for s in elf.symbols if s.name}
    address = struct.unpack_from('>I', elf.raw, struct.unpack_from('>I', elf.raw, 32)[0]+sec.idx*40+12)[0]
    at = 48+struct.unpack_from('>I', body, 72)[0]+address-0x4204f750
    assert body[at:at+len(sec.data)] == sec.data
    stock = Syx.load(a.stock).section(7)
    rows = []; total = 0; rnd = random.Random(71)
    for kind, engine in enumerate(ENGINES):
        port = Port(stock, sec.data, address, symbols)
        peak = [0, 0]
        voice = HostVoice(lib)
        for b in range(48):
            trig = b in (0, 17, 31)
            knobs = [v<<8 for v in PATCHES[kind]]
            if b >= 24:
                knobs[1:] = [rnd.randrange(128)<<8 for _ in range(7)]
                knobs[0] = (64+12)<<8 | 128
            note = 60 if b<24 else 48
            expected=voice.block(kind,knobs,note,trig)
            raw, counts = port.block(kind%8, kind, knobs, note, trig)
            assert raw == port.expected(expected), (kind, b, raw, expected)
            peak = [max(old, new) for old, new in zip(peak, counts)]
            total += 1
        rows.append(dict(type=kind, update_instructions_max=peak[0], render_instructions_max=peak[1]))
        print('PASS: type', kind, '48 exact ColdFire/host blocks; update/render max instructions', peak, flush=True)
    # A -> B -> A on the same native voice and interleaved independent tracks.
    port = Port(stock, sec.data, address, symbols)
    voices = [HostVoice(lib) for _ in range(8)]
    previous = [None]*8
    for step in range(12):
        for track in range(8):
            kind = (track+step)%len(PATCHES)
            knobs=[v<<8 for v in PATCHES[kind]]
            neighbour=(track-1)%8
            expected=voices[track].block(kind,knobs,trig=True,previous=60*64 if previous[neighbour] is not None else None)
            raw, _ = port.block(track, kind, knobs, trig=True)
            assert raw == port.expected(expected), (track, step)
            previous[track] = kind; total += 1
    # A full second fills and wraps the half-rate delay line. Both actual
    # channels, fractional controls, retriggers and chorus clock continuity.
    port = Port(stock,sec.data,address,symbols)
    voice = HostVoice(lib);different=0
    for b in range(1500):
        knobs=[64<<8,63<<8,63<<8,63<<8,0,64<<8,127<<8,127<<8]
        if b>=750:knobs[1]=103<<8|128
        expected=voice.block(4,knobs,48,trig=b in (0,1000))
        raw,_=port.block(3,4,knobs,48,trig=b in (0,1000))
        assert raw==port.expected(expected),(b,'ENS stereo/fold-down')
        different+=sum(l!=r for l,r in zip(*raw));total+=1
    assert different>1000 if port.stereo else different==0
    # Service unavailable: exact centered fold-down, never a lost channel.
    port=Port(stock,sec.data,address,symbols,stereo=False);voice=HostVoice(lib)
    for b in range(128):
        pair=voice.block(4,knobs,48,trig=b==0)
        folded=tuple(((l//65536+r//65536)>>1)*65536 for l,r in zip(*pair))
        raw,_=port.block(0,4,knobs,48,trig=b==0)
        assert raw==(folded,folded);total+=1
    result = dict(blocks=total, types=rows, stereo_service=bool(symbols.get('st_stereo_claim')),
                  oracle='Digi Mono and 48 kHz MONO2K host engines',
                  native='actual stock update/declick/gain/mix; PCM comparison is before native gains',
                  limitations='instruction counts are not CPU cycles; no full boot, scheduler, FLTR/AMP/FX or device')
    (a.out/'results.json').write_text(json.dumps(result, indent=2)+'\n')
    print('PASS:', total, 'exact ColdFire blocks, native calls/ABI, independent tracks and type switches; no device access')


if __name__ == '__main__':
    main()
