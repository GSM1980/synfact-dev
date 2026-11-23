"""kitload-st: the module's logic in Unicorn, against stubs of the OS routines it calls.

Compiles mods/kitload-st/main.c with the configured ColdFire compiler (the
device's cflags; ELEKLOADER_CROSS or --cc), links it alone and runs its event
handlers. Every OS routine it calls is a Python stub at its real address.
It checks: the RELOAD FROM PROJ rows and that building them touches no
storage; LOAD and SAVE need two YES on the same row and another key cancels;
SAVE writes the whole encoded record and LOAD hands the same bytes to the
native decoder, so the module records in the Kit tail ([14e0,15f8): LFO3,
COMP, Playhead) round-trip through the bank; LOAD moves only the sounds,
through the core's COPY path; a long press of SETTINGS opens the page and
holds SETTINGS until released.

It does not build the bank, run the real OS or the core's codec hooks: the
regressions and a unit do.

    python tests/test_st_kitload.py [--cc m68k-elf-gcc]
"""
import argparse, os, shutil, struct, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MOD = ROOT / 'mods/kitload-st'
sys.path.insert(0, str(ROOT))

from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE   # noqa: E402
from unicorn.m68k_const import (UC_CPU_M68K_CFV4E, UC_M68K_REG_A7, UC_M68K_REG_SR,  # noqa: E402
                                UC_M68K_REG_D0, UC_M68K_REG_A0)

CODE, HEAP, OBJ, BASE, STOP = 0x46100000, 0x48000000, 0x48100000, 0x49000000, 0x46fffff0
VFN, PATNAME_VT, UIROOT_V, MGR_V = 0x40001000, 0x40001100, 0x48200000, 0x4f000000
KIT_STORED, KIT_RAM, KITS_AT, KITS_RAM_OFS = 0x1600, 5656, 3015168, 17417756
SND_AT, SND_LEN, FXS_AT, FXS_LEN = 42, 291, 42 + 291 * 12, 142
TAIL = (0x14e0, 0x15f8)
KEYEV_VT, SETTINGS, YES, DOWN = 0x401c23f4, 6, 12, 15
SCRATCH, SAVEBUF = 0x405fe354, 0x40600554


def u32(b, o=0):
    return struct.unpack_from('>I', b, o)[0]


def build(cc):
    sys.path.insert(0, str(ROOT))
    from elekloader.devices import syntakt
    flags = syntakt.DEVICE.toolchain['cflags'] if hasattr(syntakt, 'DEVICE') else None
    if flags is None:
        from elekloader import devices
        flags = devices.get('syntakt').toolchain['cflags']
    w = Path(tempfile.mkdtemp())
    subprocess.run([cc, *flags, '-fno-jump-tables', '-I', str(ROOT / 'elekloader/sdk/include'), '-I', str(MOD),
                    '-c', str(MOD / 'main.c'), '-o', str(w / 'm.o')], check=True)
    ld = cc[:-3] + 'ld'
    subprocess.run([ld, '-o', str(w / 'm.elf'), '-Ttext=%#x' % CODE, '-e', 'kitload_module', str(w / 'm.o')],
                   check=True)
    nm = subprocess.run([cc[:-3] + 'nm', str(w / 'm.elf')], check=True, capture_output=True, text=True).stdout
    sym = {l.split()[2]: int(l.split()[0], 16) for l in nm.splitlines() if len(l.split()) == 3}
    oc = cc[:-3] + 'objcopy'
    subprocess.run([oc, '-O', 'binary', str(w / 'm.elf'), str(w / 'm.bin')], check=True)
    return (w / 'm.bin').read_bytes(), sym


class Unit:
    def __init__(self, image, sym, bank=True, cur=3):
        self.sym = sym
        self.u = u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        for at, n in ((0x40000000, 0x700000), (0x46000000, 0x1000000), (0x48000000, 0x400000),
                      (BASE, 0x1200000), (MGR_V, 0x10000)):
            u.mem_map(at, n)
        u.mem_write(CODE, image)
        self.heap = HEAP + 0x300000
        self.bank, self.cur = bank, cur
        self.slot = bytearray(KITS_AT + 128 * KIT_STORED)
        for j in range(128):                     # bank kits: record 11, name "KIT nn"
            o = KITS_AT + j * KIT_STORED
            struct.pack_into('>I', self.slot, o, 11)
            self.slot[o + 4:o + 20] = ('KIT %02d' % j).encode().ljust(16, b'\0')
        self.calls = {}
        self.items, self.msgs, self.strings, self.pushes, self.copies = [], [], [], [], []
        self.writes, self.decoded = [], []
        # the project object: vfunc +40 returns BASE; pattern names empty
        u.mem_write(OBJ, struct.pack('>I', OBJ + 0x40000))
        u.mem_write(OBJ + 0x40000 + 40, struct.pack('>I', VFN))
        for k in range(128):
            u.mem_write(OBJ + 13204 + 1664 * k + 44, struct.pack('>I', PATNAME_VT))
        u.mem_write(PATNAME_VT + 40, struct.pack('>I', VFN + 0x10))
        stubs = {
            VFN: lambda a: BASE, VFN + 0x10: lambda a: 0,
            0x4010394c: self.new, 0x401bf31e: self.strctor, 0x400fa84c: self.itemctor,
            0x400fa082: lambda a: self.items.append(a[1]), 0x4016dc10: lambda a: 0,
            0x4016f292: lambda a: self.log('mgr', MGR_V), 0x4011067e: lambda a: self.log('stat', 13 if self.bank else 0),
            0x40110952: lambda a: self.log('refr', 0), 0x401101c4: lambda a: self.log('dev', 2),
            0x40110892: self.sread, 0x40111c0c: self.swrite, 0x400a78a0: self.kdec, 0x400a78f4: self.kenc,
            0x40172414: lambda a: self.log('note', 0), 0x4011613c: self.memcpy,
            0x4016dfac: lambda a: self.log('sprel', 0), 0x4016eec2: self.spcopy,
            0x40177318: self.tmpkit, 0x4016ea6e: lambda a: 1, 0x400c347c: self.msg,
            0x4016eb0a: lambda a: 1, 0x400c2154: lambda a: self.cur, 0x4016e9c8: lambda a: OBJ,
            0x4016e044: lambda a: UIROOT_V, 0x40100d86: self.push,
        }
        for at, fn in stubs.items():
            u.mem_write(at, b'\x4e\x75')
            u.hook_add(UC_HOOK_CODE, self._stub(fn), begin=at, end=at)

    def _stub(self, fn):
        def hook(u, at, size, _):
            sp = u.reg_read(UC_M68K_REG_A7)
            args = struct.unpack('>8I', u.mem_read(sp + 4, 32))
            r = fn(args)
            if r is not None:
                u.reg_write(UC_M68K_REG_D0, r & 0xffffffff)
        return hook

    def log(self, k, r):
        self.calls[k] = self.calls.get(k, 0) + 1
        return r

    def new(self, a):
        p = self.heap
        self.heap += (a[0] + 7) & ~3
        return p

    def strctor(self, a):
        self.strings.append(self.cstr(a[1]))
        return a[0]

    def itemctor(self, a):
        lab, act = (struct.unpack('>4I', self.u.mem_read(x, 16)) for x in (a[1], a[2]))
        self.items_built = getattr(self, 'items_built', []) + [(a[0], lab, act)]

    def sread(self, a):
        self.log('read', 0)
        self.u.mem_write(a[1], bytes(self.slot[a[3] * 512:a[3] * 512 + a[4]]))
        return 0

    def swrite(self, a):
        data = bytes(self.u.mem_read(a[1], a[4]))
        self.slot[a[3] * 512:a[3] * 512 + a[4]] = data
        self.writes.append((a[3], data))
        return 0

    def kenc(self, a):                         # an encoded record with module records in its tail
        rec = bytearray(KIT_STORED)
        struct.pack_into('>I', rec, 0, 11)
        rec[4:20] = b'LIVE KIT'.ljust(16, b'\0')
        for i in range(20, KIT_STORED):
            rec[i] = (i * 7 + a[1]) & 0xff
        for i in range(*TAIL):
            rec[i] = (0xa5 ^ i) & 0xff
        self.encoded = bytes(rec)
        self.u.mem_write(a[0], self.encoded)
        return 1

    def kdec(self, a):
        src = bytes(self.u.mem_read(a[1], KIT_STORED))
        self.decoded.append(src)
        self.u.mem_write(a[0], bytes((i * 3 + 1) & 0xff for i in range(KIT_RAM)))
        return 1

    def memcpy(self, a):
        self.copies.append((a[0], a[1], a[2]))
        self.u.mem_write(a[0], bytes(self.u.mem_read(a[1], a[2])))
        return a[0]

    def spcopy(self, a):
        self.u.mem_write(a[0], bytes(self.u.mem_read(a[1], 4)))

    def tmpkit(self, a):
        out = self.u.reg_read(UC_M68K_REG_A0)
        kit = self.new([KIT_RAM])
        self.u.mem_write(out, struct.pack('>2I', kit, 0x48300000))
        self.tmp = kit
        return out

    def msg(self, a):
        self.msgs.append(self.cstr(a[1]))

    def push(self, a):
        self.pushes.append((a[0], *struct.unpack('>2I', self.u.mem_read(a[1], 8)), a[2]))
        return 1

    def cstr(self, p):
        return bytes(self.u.mem_read(p, 64)).split(b'\0')[0].decode('latin-1')

    def call(self, fn, args, a0=None):
        u = self.u
        sp = 0x46f00000 - 4 * len(args) - 4
        u.mem_write(sp, struct.pack('>%dI' % (len(args) + 1), STOP, *args))
        u.reg_write(UC_M68K_REG_SR, 0x2700)
        u.reg_write(UC_M68K_REG_A7, sp)
        if a0 is not None:
            u.reg_write(UC_M68K_REG_A0, a0)
        u.emu_start(fn, STOP, count=5_000_000)
        return u.reg_read(UC_M68K_REG_D0) & 0xffffffff

    def var(self, name):
        return u32(self.u.mem_read(self.sym[name], 4))

    def menu(self):
        self.call(self.sym['kl_reload_menu'], [0x48010000])
        self.call(self.sym['kl_pattern_menu'], [0x48020000])

    def yes(self, row):                         # row 0 = MODE, 1..128 = kits
        _, lab, act = self.items_built[row]
        any_ = self.new([16])
        self.u.mem_write(any_, struct.pack('>4I', *act))
        self.call(act[3], [any_, 0])

    def label(self, row):
        _, lab, _ = self.items_built[row]
        any_ = self.new([16])
        self.u.mem_write(any_, struct.pack('>4I', *lab))
        self.call(lab[3], [any_], a0=self.new([32]))
        return self.strings[-1]

    def key(self, code, flags):
        ev = self.new([24])
        self.u.mem_write(ev, struct.pack('>5I', KEYEV_VT, 0, 0, code, flags))
        return self.call(self.sym['kl_key'], [0, ev])


def check(image, sym):
    m = Unit(image, sym)
    m.menu()
    assert len(m.items_built) == 129 and len(m.items) == 129
    assert not any(k in m.calls for k in ('stat', 'read', 'mgr')), m.calls      # building touches no storage
    assert m.label(0) == 'MODE: LOAD KIT' and m.label(1) == 'A01 KIT 00' and m.label(128) == 'H16 KIT 127'
    print('rows: 129 built without storage access; labels read the bank on draw')

    # LOAD: two YES; another key cancels
    m.yes(6)
    assert m.msgs[-1] == 'LOAD A06? YES again' and 'note' not in m.calls
    assert m.key(DOWN, 1) == 0 and m.var('kl_armed') == 0                       # cancels, not consumed
    m.yes(6)
    assert 'note' not in m.calls
    m.yes(6)
    assert m.calls['note'] == 1 and m.msgs[-1] == 'LOADED A06 KIT 05'
    live = BASE + KITS_RAM_OFS + KIT_RAM * m.cur
    sizes = [n for d, s, n in m.copies]
    assert sizes == [SND_LEN] * 12 + [FXS_LEN]                                 # only the sounds, via COPY
    assert [d - live for d, s, n in m.copies] == [SND_AT + SND_LEN * t for t in range(12)] + [FXS_AT]
    print('load: confirmation, cancel, sounds only through the COPY path, engine notified')

    # SAVE then LOAD: the encoded tail round-trips
    m.yes(0)
    assert m.msgs[-1] == 'List mode: SAVE' and m.label(9) == 'SAVE A09 KIT 08'
    m.yes(9)
    assert m.msgs[-1] == 'SAVE A09 IN P128? YES' and not m.writes
    m.yes(9)
    (block, data), = m.writes
    assert block * 512 == KITS_AT + 8 * KIT_STORED and len(data) == KIT_STORED
    assert data[TAIL[0]:TAIL[1]] == m.encoded[TAIL[0]:TAIL[1]]                  # module records written
    assert data[20:] == m.encoded[20:] and m.msgs[-1].startswith('SAVED A09')
    m.yes(0)
    m.decoded.clear()
    m.yes(9)
    m.yes(9)
    (src,) = m.decoded
    assert src == data and src[TAIL[0]:TAIL[1]] == m.encoded[TAIL[0]:TAIL[1]]   # ... and handed back to the decoder
    print('save/load: the whole record, Kit tail [14e0,15f8) included, round-trips through the bank')

    # no bank: nothing read or written
    n = Unit(image, sym, bank=False)
    n.menu()
    assert n.label(0) == 'BANK P128 NOT SAVED' and n.label(1) == 'STATUS 0 (13=OK)'
    n.yes(3)
    n.yes(3)
    assert 'read' not in n.calls and not n.writes and n.msgs[-1] == 'Bank P128 not saved'

    # long press of SETTINGS
    k = Unit(image, sym)
    assert k.key(SETTINGS, 0x20) == 0 and not k.pushes                         # before PatternMenuView: stock
    k.menu()
    pm = 0x48020000
    k.u.mem_write(pm + 472, struct.pack('>2I', 0x48030000, 0x48040000))
    assert k.key(SETTINGS, 0x20) == 1
    assert k.pushes == [(UIROOT_V + 64, 0x48030000, 0x48040000, 0)]
    assert k.key(SETTINGS, 0x21) == 1 and k.key(SETTINGS, 0x08) == 1           # still held
    assert k.key(SETTINGS, 0x00) == 1 and k.var('kl_eat') == 0                  # the release, held too
    assert k.key(SETTINGS, 0x01) == 0 and k.key(SETTINGS, 0x10) == 0           # then SETTINGS is stock
    assert k.key(SETTINGS, 0x00) == 0 and len(k.pushes) == 1
    other = k.new([24])
    k.u.mem_write(other, struct.pack('>5I', 0x401c0000, 0, 0, SETTINGS, 0x20))
    assert k.call(k.sym['kl_key'], [0, other]) == 0                             # other event classes pass
    print('keys: long press opens the page once, SETTINGS held until released, other events pass')


def main():
    p = argparse.ArgumentParser()
    cross = os.environ.get('ELEKLOADER_CROSS', 'm68k-elf-')
    p.add_argument('--cc', default=shutil.which(cross + 'gcc'))
    a = p.parse_args()
    if not a.cc:
        print('skipped: no ColdFire compiler (set ELEKLOADER_CROSS or --cc)')
        return
    check(*build(a.cc))


if __name__ == '__main__':
    main()
