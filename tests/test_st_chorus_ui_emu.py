"""The chorus's controls on the real core 1.2 MAIN image, in Unicorn.

Loads a core build's main.bin with its resident and a bank with chorus-st,
runs the module's real controls init (provider registration, the five site
installs, AMP layouts, page-25 interest) and then the native routines as the
UI calls them: labels, formats and cached records through the core's hooks
(the seven settings and the CHO send, parameter 72), the global value
provider (kit/get/set) on a Kit's FX record, page 25's layout, REVERB's page
vector and draw, AMP's send drawing branch, the dials, and the FX record
codec through the core's notifications. Stock IDs keep the stock answers.
Native string construction, allocation, page drawing and blits are recorded
stubs; no complete view, boot or physical display is modeled.

  "$PY" -B tests/test_st_chorus_ui_emu.py \\
      --core out/core-st-X.Y.Z-name/main.bin --bank <bank with chorus-st>
"""
import argparse
import json
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader.sdk import elf                                           # noqa: E402
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE   # noqa: E402
from unicorn.m68k_const import *                                         # noqa: E402

BASE, BANK, SP, RET = 0x40000400, 0x47000000, 0x48010000, 0x4f000000
W, KIT, VIEW, FRAME, HEAP = 0x49000000, 0x49002000, 0x49006000, 0x49007000, 0x49008000
CHORUS_ID, CHORUS_UI, SEND = 0x403, 0x4ea, 72
NAMES = ['DPTH', 'SPD', 'HPF', 'WDTH', 'DEL', 'REV', 'VOL']
SLOTS = [0x1a, 0x24, 0x27, 0x29, 0x2a, 0x37, 0x46]
STUBS = {0x401bf082: 'string', 0x40103788: 'new', 0x40103794: 'free', 0x4003a244: 'page_draw',
         0x400f70fa: 'bitmap', 0x400f8dac: 'blit', 0x40011e7c: 'stock_dial', 0x40042ce8: 'stock_reverb_draw'}
CALLEE = [UC_M68K_REG_D2, UC_M68K_REG_D3, UC_M68K_REG_D4, UC_M68K_REG_D5, UC_M68K_REG_D6, UC_M68K_REG_D7,
          UC_M68K_REG_A2, UC_M68K_REG_A3, UC_M68K_REG_A4, UC_M68K_REG_A5, UC_M68K_REG_A6]


class Machine:
    def __init__(self, core, bank=None):
        r = json.loads((core.parent / 'verification.json').read_text())
        self.cs = r['symbols']
        u = self.u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        for a, n in [(0x40000000, 0x6000000), (0x46000000, 0x10000), (BANK, 0x200000),
                     (SP - 0x10000, 0x20000), (W, 0x20000), (RET, 0x10000), (0x80000000, 0x10000)]:
            u.mem_map(a, n)
        raw = core.read_bytes()
        u.mem_write(BASE, raw)
        lo, hi = r['link']['ddr']
        at = r['link']['run_load'] - BASE
        u.mem_write(lo, raw[at:at + hi - lo])
        u.mem_write(RET, b'\x4e\x71')
        self.sym = {}
        if bank:
            e = elf.Elf.load(bank.parent / (bank.stem + '.work') / 'main.elf')
            idx = e.section('.image').idx
            self.sym = {s.name: BANK + s.value for s in e.symbols if s.name and s.shndx == idx}
            data = bank.read_bytes()
            size, n = struct.unpack_from('>2I', data, 52)
            image = bytearray(data[80 + 4 * n:80 + 4 * n + size])
            for off in struct.unpack_from('>%dI' % n, data, 80):
                struct.pack_into('>I', image, off, (struct.unpack_from('>I', image, off)[0] + BANK) & 0xffffffff)
            u.mem_write(BANK, bytes(image))
        self.log = []
        self.heap = HEAP
        u.hook_add(UC_HOOK_CODE, self.code)
        # A bank's machines make the core's lookups active (core_machine_bind);
        # here an empty directory: no machine claims any ID.
        self.w(self.cs['core:directory'], W + 0x1f00)
        self.w(self.cs['core:core_machine_active'], 1)

    def read(self, a, n): return bytes(self.u.mem_read(a, n))
    def word(self, a): return int.from_bytes(self.read(a, 4), 'big')
    def half(self, a): return int.from_bytes(self.read(a, 2), 'big')
    def w(self, a, v): self.u.mem_write(a, struct.pack('>I', v & 0xffffffff))
    def string(self, a): return self.read(a, 32).split(b'\0')[0].decode()

    def ret(self, v=0):
        sp = self.u.reg_read(UC_M68K_REG_A7)
        self.u.reg_write(UC_M68K_REG_D0, v & 0xffffffff)
        self.u.reg_write(UC_M68K_REG_A7, sp + 4)
        self.u.reg_write(UC_M68K_REG_PC, self.word(sp))

    def code(self, u, pc, n, _):
        name = STUBS.get(pc)
        if not name:
            return
        sp = u.reg_read(UC_M68K_REG_A7)
        args = struct.unpack('>8I', self.read(sp + 4, 32))
        self.log.append((name, args))
        if name == 'new':
            self.heap += 0x100
            self.ret(self.heap)
            return
        # continuations after a replayed stock prologue: undo its frame, return
        frame = {'stock_reverb_draw': (96, [UC_M68K_REG_D2, UC_M68K_REG_D3, UC_M68K_REG_D4, UC_M68K_REG_D5,
                                           UC_M68K_REG_D6, UC_M68K_REG_D7, UC_M68K_REG_A2, UC_M68K_REG_A3, UC_M68K_REG_A4,
                                           UC_M68K_REG_A5, UC_M68K_REG_A6]),
                 'stock_dial': (20, [UC_M68K_REG_D2, UC_M68K_REG_D3, UC_M68K_REG_D4, UC_M68K_REG_D5,
                                     UC_M68K_REG_D6])}.get(name)
        if frame:
            size, regs = frame
            for i, r in enumerate(regs):
                u.reg_write(r, self.word(sp + 4 * i))
            u.reg_write(UC_M68K_REG_A7, sp + size)
            self.log[-1] = (name, struct.unpack('>8I', self.read(sp + size + 4, 32)))
        self.ret(0)

    def call(self, fn, *args, regs=None):
        u = self.u
        for i, r in enumerate(CALLEE):
            u.reg_write(r, 0x12340000 + i * 256)
        for r, v in (regs or {}).items():
            u.reg_write(r, v)
        before = [u.reg_read(r) for r in CALLEE]
        u.reg_write(UC_M68K_REG_SR, 0x2700)
        u.reg_write(UC_M68K_REG_A7, SP)
        u.mem_write(SP, struct.pack('>%dI' % (1 + len(args)), RET, *[a & 0xffffffff for a in args]))
        u.emu_start(self.sym.get(fn, fn), RET, count=2000000)
        assert u.reg_read(UC_M68K_REG_PC) == RET, hex(u.reg_read(UC_M68K_REG_PC))
        if not regs:
            assert [u.reg_read(r) for r in CALLEE] == before, 'callee-saved registers changed'
        return u.reg_read(UC_M68K_REG_D0)


def init(m):
    # the core's native list holds the module's subscription, as the bind does
    m.w(W + 0x1e00, m.sym['chorus_native']); m.w(W + 0x1e04, 0)
    m.w(m.cs['core:events'] + 11 * 8, W + 0x1e00)
    m.w(W, 3); m.w(W + 4, m.cs['core:core_machine_ui_api'])
    assert m.call('chorus_ui_init', W) == 0
    m.call(m.cs['core:core_native_initialize'])
    assert m.word(m.sym['chorus_refused']) == 0, 'site bytes not stock on this core'


def test_sites(m, stock):
    for at, entry in ((0x4000d870, 'chorus_kit_entry'), (0x4000d94a, 'chorus_get_entry'),
                      (0x4000da32, 'chorus_set_entry'), (0x40042ce0, 'chorus_reverb_draw_entry'),
                      (0x40041e7c, 'chorus_amp_entry')):
        assert m.read(at, 6) == struct.pack('>HI', 0x4ef9, m.sym[entry]), hex(at)
        assert stock.read(at, 6) != m.read(at, 6)
    # AMP: E-H of both layouts are CHO, DEL, REV, PAN; VOL on the second page's E
    for at in (0x41b9f61c, 0x41b9f648):
        assert struct.unpack('>4I', m.read(at, 16)) == (SEND, 78, 79, 80)
    assert m.word(0x41b9f674) == 81 and m.word(0x41b9f6a0) == 81
    strings = [args[1] for name, args in m.log if name == 'string']
    assert [m.string(a) for a in strings] == ['CHO', 'Chorus']
    print('PASS: five sites, AMP layouts and page strings installed')


def test_metadata(m, stock):
    for i, name in enumerate(NAMES):
        assert m.string(m.call(0x40012b24, 0, CHORUS_ID + i)) == name
        assert m.string(m.call(0x4007bb6a, CHORUS_ID + i, 0x4080)) == ('64.50' if i in (0, 1, 4, 5) else '64')
        assert m.call(0x4007b9b2, CHORUS_ID + i) == stock.call(0x4007b9b2, 0x13)          # cached record
        assert m.call(0x400a636a, CHORUS_ID + i) == CHORUS_UI + i                          # engine -> UI
        assert m.call(0x400a634e, CHORUS_UI + i) == CHORUS_ID + i                          # UI -> engine
        # native range check 400123e6 (the relative edit's bound): module IDs pass
        # the core's 1.2 bound (0x478) and use the provider's range
        assert m.call(0x400123e6, W, CHORUS_ID + i, 0x7f00) & 0xff and m.call(0x400123e6, W, CHORUS_ID + i, 0) & 0xff
        assert not m.call(0x400123e6, W, CHORUS_ID + i, 0x7f01) & 0xff
        assert not m.call(0x400123e6, W, 0x479, 0) & 0xff                                    # past the module range
    assert m.string(m.call(0x40012b24, 0, SEND)) == 'CHO'
    assert m.string(m.call(0x4007bb6a, SEND, 0x4080)) == '64'
    assert m.call(0x4007b9b2, SEND) == stock.call(0x4007b9b2, 78)
    # every other stock ID answers as on the core without the chorus
    for pid in [i for i in range(0, 0x1f9) if i != SEND]:
        for fn, args in ((0x40012b24, (0, pid)), (0x4007b9b2, (pid,))):
            assert m.call(fn, *args) == stock.call(fn, *args), (hex(fn), pid)
    print('PASS: labels, formats, cached records and UI IDs of the seven settings and CHO; %d stock IDs unchanged' % 0x1f8)


def kit(m, words=None):
    m.u.mem_write(KIT, bytes(0x1000))
    if words:
        m.u.mem_write(KIT + 0xdce, struct.pack('>71H', *words))
    m.w(0x800030b8, KIT); m.w(0x800030bc, KIT + 0xdce)


def test_values(m):
    kit(m)
    assert [m.call(0x4000d94a, W, CHORUS_ID + i) for i in range(7)] == [0x7f00, 0x2000, 0, 0x7f00, 0, 0, 0x6400]
    for i, v in enumerate((0x100, 0x2000, 0x7f00, 0, 0x3000, 0x4000, 0)):
        assert m.call(0x4000da32, W, CHORUS_ID + i, v, 0, 0) == 0
    fx = KIT + 0xdce
    assert m.half(fx) == 0x4332
    assert [m.half(fx + 2 * s) for s in SLOTS] == [0x8100, 0xa000, 0xff00, 0x8000, 0xb000, 0xc000, 0x8000]
    assert [m.call(0x4000d94a, W, CHORUS_ID + i) for i in range(7)] == [0x100, 0x2000, 0x7f00, 0, 0x3000, 0x4000, 0]
    assert m.call(0x4000da32, W, CHORUS_ID, 0x9000, 0, 0) == 0 and m.call(0x4000d94a, W, CHORUS_ID) == 0x7f00
    assert m.call(0x4000d870, W, CHORUS_ID) == 1
    # a stock ID leaves the entries for the stock code, registers and stack as given
    for at, cont in ((0x4000d94a, 0x4000d954), (0x4000da32, 0x4000da3a), (0x4000d870, 0x4000d878)):
        m.u.mem_write(SP, struct.pack('>4I', RET, W, 0x20, 0))
        m.u.reg_write(UC_M68K_REG_A7, SP)
        m.u.emu_start(at, cont, count=100)
        assert m.u.reg_read(UC_M68K_REG_PC) == cont
    print('PASS: global value provider: defaults, set/get on the Kit\'s FX record, clamp, stock IDs pass through')


def test_codec(m):
    raw = W + 0x3000
    m.u.mem_write(raw, bytes(0x9c))
    m.call(m.cs['core:core_fx_codec_notify'], 28, raw, KIT + 0xdce, 1)
    kit(m)
    m.call(m.cs['core:core_fx_codec_notify'], 29, KIT + 0xdce, raw, 1)
    assert [m.call(0x4000d94a, W, CHORUS_ID + i) for i in range(7)] == [0x7f00, 0x2000, 0x7f00, 0, 0x3000, 0x4000, 0]
    m.call(m.cs['core:core_fx_codec_notify'], 29, KIT + 0xdce, W + 0x4000, 0)              # failed decode: no change
    assert m.call(0x4000d94a, W, CHORUS_ID + 4) == 0x3000
    print('PASS: settings saved and loaded through the core\'s FX record codec notifications')


def test_pages(m):
    layout = m.call(0x4007b9d0, 25)
    assert struct.unpack('>8I', m.read(layout + 8, 32)) == (CHORUS_ID, CHORUS_ID + 1, CHORUS_ID + 2, CHORUS_ID + 3,
                                                            0, CHORUS_ID + 4, CHORUS_ID + 5, CHORUS_ID + 6)
    for cap in (8, 4):                                     # room for page 25, or a new vector
        pages = W + 0x5000
        m.u.mem_write(VIEW, bytes(0x100))
        m.write_pages = None
        m.w(pages, 21); m.w(VIEW + 0x7c, pages); m.w(VIEW + 0x80, pages + 4); m.w(VIEW + 0x84, pages + cap)
        m.log.clear()
        m.call(0x40042ce0, VIEW, FRAME)
        assert [n for n, a in m.log] == (['stock_reverb_draw'] if cap == 8 else ['new', 'free', 'stock_reverb_draw'])
        p = m.word(VIEW + 0x7c)
        assert (m.word(p), m.word(p + 4)) == (21, 25) and m.word(VIEW + 0x80) == p + 8
        m.w(VIEW + 0x90, 1); m.log.clear()
        m.call(0x40042ce0, VIEW, FRAME)
        assert m.log == [('page_draw', (VIEW, FRAME) + m.log[0][1][2:])]
        m.w(VIEW + 0x90, 0)
    m.w(VIEW + 0x7c, W + 0x5000); m.w(W + 0x5000, 22); m.log.clear()
    m.call(0x40042ce0, VIEW, FRAME)                         # another FX page vector: untouched
    assert [n for n, a in m.log] == ['stock_reverb_draw'] and m.word(W + 0x5000) == 22
    print('PASS: page 25 layout; REVERB gains page 25 (in place and reallocated) and draws it')


def test_amp(m, stock):
    # D1's upper bytes are zero here in the native code: CMP.L compares all of it
    stops = (0x40041ec8, 0x40041ede, 0x40041ee2)
    for a6, target in ((74, 0x40041ec8), (SEND, 0x40041ede), (78, 0x40041ee2), (81, 0x40041ee2), (0, 0x40041ee2)):
        reached = []
        for c in (m, stock):
            hook = c.u.hook_add(UC_HOOK_CODE, lambda u, pc, n, _: pc in stops and (reached.append(pc), u.emu_stop()))
            c.u.reg_write(UC_M68K_REG_A6, a6)
            c.u.reg_write(UC_M68K_REG_D1, 0)
            c.u.emu_start(0x40041e7c, RET, count=20)
            c.u.hook_del(hook)
            assert c.u.reg_read(UC_M68K_REG_D1) == 74
        expect = [target, 0x40041ee2 if a6 == SEND else target]             # stock: CHO falls to the others
        assert reached == expect, (a6, [hex(x) for x in reached])
    print('PASS: AMP drawing: HOLD and the other controls as stock, CHO on the send controls\' branch')


def test_dials(m):
    for pid, kind, last in ((CHORUS_ID + 2, 'chorus_hpf_bits', 10), (CHORUS_ID + 3, 'chorus_width_bits', 4),
                            (SEND, 'chorus_cone_bits', 14)):
        m.log.clear()
        m.call(0x40011e74, W, pid, 0x7f00, 1, 2, FRAME, 10, 20)
        (b, bargs), (c, cargs) = m.log
        assert b == 'bitmap' and c == 'blit' and cargs[0] == FRAME and cargs[2:4] == (10, 20)
        assert bargs[3] == m.sym[kind] + last * 19 * 4 and bargs[1:3] == (19, 17), kind   # full scale: last frame
    for pid, native in ((CHORUS_ID + 4, 0x4e), (CHORUS_ID + 5, 0x4f)):
        m.log.clear()
        m.call(0x40011e74, W, pid, 0x2000, 1, 2, FRAME, 10, 20)
        assert m.log[0][0] == 'stock_dial'
    m.log.clear()
    m.call(0x40011e74, W, 0x20, 0x2000, 1, 2, FRAME, 10, 20)                               # a stock ID
    assert [n for n, a in m.log] == ['stock_dial']
    print('PASS: dials: HPF, WDTH and CHO bitmaps; DEL/REV and stock IDs on the native dial')


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--core', type=Path, required=True)
    p.add_argument('--bank', type=Path, required=True)
    a = p.parse_args()
    m, stock = Machine(a.core, a.bank), Machine(a.core)
    init(m)
    test_sites(m, stock)
    test_metadata(m, stock)
    test_values(m)
    test_codec(m)
    test_pages(m)
    test_amp(m, stock)
    test_dials(m)
    print('ALL PASS; no device access')


if __name__ == '__main__':
    main()
