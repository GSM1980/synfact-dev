"""livemute-st: the NOTE_ACCEPT handler in Unicorn, against the OS's mute words.

Compiles mods/livemute-st/main.c with the configured ColdFire compiler (the
device's cflags; ELEKLOADER_CROSS or --cc), links it alone and calls its
native handler with events and mute states written where the OS keeps them
(80003048 track mute, 8000304c solo, 43bdd828 pattern/song mute). It checks:
a live note-on (kind 2, action 1) on a muted track is consumed with a nonzero
result; the same note on an unmuted track, a sequencer trig (kind 1), a
release (action 2), the FX track and other operations pass untouched; solo
mutes every track not soloed; the pattern/song mute counts too; the module
registers its interest in NOTE_ACCEPT only, and refuses a host before API 10;
SETTINGS > LIVE MUTE starts ON, YES toggles it, LEFT/RIGHT set OFF/ON, each
redraws the menu, and while OFF every note passes; with host API 20 the
setting is read from the core preferences (key 3) at start and written on
each change, a stored value other than 0/1 is ignored, and before API 20 the
preferences are not touched.

It does not run the real OS or the core's dispatcher.

    python tests/test_st_livemute.py [--cc m68k-elf-gcc]
"""
import argparse, os, shutil, struct, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MOD = ROOT / 'mods/livemute-st'
sys.path.insert(0, str(ROOT))

from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE   # noqa: E402
from unicorn.m68k_const import UC_CPU_M68K_CFV4E, UC_M68K_REG_A7, UC_M68K_REG_D0  # noqa: E402

CODE, DATA, STOP = 0x46100000, 0x48000000, 0x46fffff0
EVENT, ARGS, RESULT, HOST, UI, API, INTEREST = (DATA + o for o in (0, 0x100, 0x200, 0x300, 0x400, 0x500, 0x600))
NOTE_ACCEPT = 20
ADDITEM, VIEW, PREFS, PERSIST = DATA + 0x700, DATA + 0x800, DATA + 0xa00, DATA + 0xb00


def build(cc):
    from elekloader.devices import syntakt
    flags = syntakt.DEVICE.toolchain['cflags']
    w = Path(tempfile.mkdtemp())
    subprocess.run([cc, *flags, '-fno-jump-tables', '-I', str(ROOT / 'elekloader/sdk/include'), '-I', str(MOD),
                    '-c', str(MOD / 'main.c'), '-o', str(w / 'm.o')], check=True)
    pre = cc[:-3]
    subprocess.run([pre + 'ld', '-o', str(w / 'm.elf'), '-Ttext=%#x' % CODE, '-e', 'livemute_module', str(w / 'm.o')],
                   check=True)
    nm = subprocess.run([pre + 'nm', str(w / 'm.elf')], check=True, capture_output=True, text=True).stdout
    sym = {l.split()[2]: int(l.split()[0], 16) for l in nm.splitlines() if len(l.split()) == 3}
    subprocess.run([pre + 'objcopy', '-O', 'binary', str(w / 'm.elf'), str(w / 'm.bin')], check=True)
    return (w / 'm.bin').read_bytes(), sym


class Unit:
    def __init__(self, image, sym):
        self.sym, self.u = sym, Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        u = self.u
        u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        for at, n in ((0x40000000, 0x200000), (0x43b00000, 0x100000), (0x46000000, 0x1000000), (DATA, 0x10000), (0x80000000, 0x10000)):
            u.mem_map(at, n)
        u.mem_write(CODE, image)
        self.interest, self.invalidated, self.rows = [], [], []
        self.prefs, self.pref_calls = {}, []
        for at, fn in ((INTEREST, self._interest), (0x400ffe22, self._invalidate), (ADDITEM, self._additem),
                       (PREFS, self._prefs)):
            u.mem_write(at, b'\x4e\x75')
            u.hook_add(UC_HOOK_CODE, fn, begin=at, end=at)

    def _invalidate(self, u, at, size, _):
        self.invalidated.append(struct.unpack('>I', u.mem_read(u.reg_read(UC_M68K_REG_A7) + 4, 4))[0])

    def _prefs(self, u, at, size, _):           # preferences(key, words, count, write): 1 found/ok, 0 absent
        key, v, n, write = struct.unpack('>4I', u.mem_read(u.reg_read(UC_M68K_REG_A7) + 4, 16))
        self.pref_calls.append((key, n, write))
        if write:
            self.prefs[key] = list(struct.unpack('>%dI' % n, u.mem_read(v, 4 * n)))
            r = 1
        elif key in self.prefs:
            u.mem_write(v, struct.pack('>%dI' % n, *self.prefs[key][:n]))
            r = 1
        else:
            r = 0
        u.reg_write(UC_M68K_REG_D0, r)

    def _additem(self, u, at, size, _):
        menu, row = struct.unpack('>2I', u.mem_read(u.reg_read(UC_M68K_REG_A7) + 4, 8))
        self.rows.append((menu, struct.unpack('>4I', u.mem_read(row, 16))))

    def text(self):
        return bytes(self.u.mem_read(self.call(self.sym['lm_text'], []), 20)).split(b'\0')[0].decode()

    def _interest(self, u, at, size, _):
        sp = u.reg_read(UC_M68K_REG_A7)
        self.interest.append(struct.unpack('>2I', u.mem_read(sp + 4, 8)))

    def call(self, fn, args):
        u = self.u
        sp = 0x46f00000 - 4 * len(args) - 4
        u.mem_write(sp, struct.pack('>%dI' % (len(args) + 1), STOP, *args))
        u.reg_write(UC_M68K_REG_A7, sp)
        u.emu_start(fn, STOP, count=200000)
        return u.reg_read(UC_M68K_REG_D0)

    def mutes(self, track=0, solo=0, pattern=0):
        self.u.mem_write(0x80003048, struct.pack('>2I', track, solo))
        self.u.mem_write(0x43bdd828, struct.pack('>H', pattern))

    def note(self, track, kind=2, action=1, op=NOTE_ACCEPT):
        self.u.mem_write(EVENT, struct.pack('>4I', 0, action, track, kind))
        self.u.mem_write(ARGS, struct.pack('>4I', EVENT, 0, 0, 0))
        self.u.mem_write(RESULT, struct.pack('>I', 0xdead))
        r = self.call(self.sym['lm_native'], [op, ARGS, RESULT])
        return r, struct.unpack('>I', self.u.mem_read(RESULT, 4))[0]


def offsets(cc):
    """The host pointer in st_machine_ui_api and native_interest in st_machine_host_api, as compiled."""
    w = Path(tempfile.mkdtemp())
    src = w / 'o.c'
    src.write_text('#include <stddef.h>\n#include <syntakt/modules.h>\n#include <syntakt/host.h>\n'
                   'const unsigned int o[4]={offsetof(struct st_machine_ui_api,host),'
                   'offsetof(struct st_machine_host_api,native_interest),'
                   'offsetof(struct st_machine_host_api,additem),'
                   'offsetof(struct st_machine_host_api,persistence)};\n')
    subprocess.run([cc, '-mcpu=5475', '-I', str(ROOT / 'elekloader/sdk/include'), '-c', str(src), '-o', str(w / 'o.o')],
                   check=True)
    subprocess.run([cc[:-3] + 'objcopy', '-O', 'binary', '-j', '.rodata', str(w / 'o.o'), str(w / 'o.bin')], check=True)
    return struct.unpack('>4I', (w / 'o.bin').read_bytes()[:16])


def check(image, sym):
    u = Unit(image, sym)
    # live note-ons on muted tracks are consumed; everything else passes
    u.mutes(track=1 << 3)
    assert u.note(3) == (1, 1)                                                 # muted, live note-on: dropped
    assert u.note(4) == (0, 0xdead)                                            # not muted
    assert u.note(3, kind=1) == (0, 0xdead)                                    # sequencer: the OS's own filter
    assert u.note(3, action=2) == (0, 0xdead)                                  # release: passes, nothing hangs
    assert u.note(12) == (0, 0xdead) and u.note(40) == (0, 0xdead)             # FX track, out of range
    assert u.note(3, op=19) == (0, 0xdead)                                     # other operations
    print('live: a live note-on on a muted track is dropped; unmuted, sequencer, release, FX and other ops pass')
    u.mutes(track=1 << 3, solo=1 << 5)                                         # solo T6: all others muted
    assert u.note(5) == (0, 0xdead) and u.note(3) == (1, 1) and u.note(0) == (1, 1)
    u.mutes(pattern=1 << 7)
    assert u.note(7) == (1, 1) and u.note(6) == (0, 0xdead)                    # pattern/song mute
    u.mutes()
    assert all(u.note(t) == (0, 0xdead) for t in range(12))                    # nothing muted: all pass
    assert u.u.mem_read(0x80003048, 8) == b'\0' * 8                            # read only
    print('rule: solo mutes every other track, the pattern/song mute counts, nothing muted passes all')
    # SETTINGS > LIVE MUTE: ON after power-on; YES toggles; LEFT/RIGHT set; each redraws
    fn = DATA + 0x900
    u.u.mem_write(fn, struct.pack('>I', VIEW))
    u.mutes(track=1 << 3)
    assert u.text() == 'LIVE MUTE: ON'
    u.call(sym['lm_select'], [fn])
    assert u.text() == 'LIVE MUTE: OFF' and u.invalidated == [VIEW + 0x38]
    assert u.note(3) == (0, 0xdead)                                            # OFF: muted track plays live notes
    u.call(sym['lm_change'], [fn, 1])
    assert u.text() == 'LIVE MUTE: ON' and u.note(3) == (1, 1)
    u.call(sym['lm_change'], [fn, 0xffffffff])
    assert u.text() == 'LIVE MUTE: OFF' and u.note(3) == (0, 0xdead)
    u.call(sym['lm_select'], [fn]); u.call(sym['lm_select'], [0])           # no view: no redraw
    assert u.text() == 'LIVE MUTE: OFF' and len(u.invalidated) == 4
    print('settings: LIVE MUTE starts ON, YES toggles, LEFT/RIGHT set OFF/ON, redraws, OFF passes every note')
    print('dropped count:', struct.unpack('>I', u.u.mem_read(sym['lm_dropped'], 4))[0])


def start(image, sym, offs, version, stored=None):
    """A unit whose initialize ran on a host of this version (preferences from API 20)."""
    u = Unit(image, sym)
    if stored is not None:
        u.prefs[3] = [stored]
    desc = struct.unpack('>4I', image[sym['livemute_module'] - CODE:sym['livemute_module'] - CODE + 16])
    u.u.mem_write(HOST, struct.pack('>I', version) + b'\0' * 0x100)
    u.u.mem_write(HOST + offs[1], struct.pack('>I', INTEREST))
    u.u.mem_write(HOST + offs[2], struct.pack('>I', ADDITEM))
    if version >= 20:
        u.u.mem_write(HOST + offs[3], struct.pack('>I', PERSIST))
        u.u.mem_write(PERSIST, struct.pack('>6I', 1, 0, 0, 0, 0, PREFS))
    u.u.mem_write(UI, b'\0' * 0x40)
    u.u.mem_write(UI + offs[0], struct.pack('>I', HOST))
    u.u.mem_write(API, struct.pack('>2I', 3, UI))
    assert u.call(desc[1], [API]) == 0
    return u


def check_prefs(image, sym, offs):
    fn = DATA + 0x900
    u = start(image, sym, offs, 20)                                            # nothing stored: ON
    assert u.text() == 'LIVE MUTE: ON' and u.pref_calls == [(3, 1, 0)]
    u.u.mem_write(fn, struct.pack('>I', VIEW))
    u.call(sym['lm_select'], [fn])
    assert u.prefs[3] == [0] and u.pref_calls[-1] == (3, 1, 1)               # OFF, written
    u.call(sym['lm_change'], [fn, 1])
    assert u.prefs[3] == [1]
    u.call(sym['lm_change'], [fn, 0xffffffff])
    assert u.prefs[3] == [0]
    v = start(image, sym, offs, 20, stored=u.prefs[3][0])                     # power-on: OFF kept
    v.mutes(track=1 << 3)
    assert v.text() == 'LIVE MUTE: OFF' and v.note(3) == (0, 0xdead)
    assert start(image, sym, offs, 20, stored=7).text() == 'LIVE MUTE: ON'    # not 0/1: ignored
    w = start(image, sym, offs, 18)                                            # before API 20: untouched
    w.u.mem_write(fn, struct.pack('>I', VIEW))
    w.call(sym['lm_select'], [fn])
    assert w.text() == 'LIVE MUTE: OFF' and not w.pref_calls
    print('prefs: API 20 reads key 3 at start, writes each change, OFF survives power-on, '
          'bad value ignored, older hosts untouched')


def check_init(image, sym, offs):
    for version, ok in ((9, False), (10, True), (18, True)):
        u = Unit(image, sym)
        # the descriptor's init pointer: {name, init, subs, count}
        desc = struct.unpack('>4I', image[sym['livemute_module'] - CODE:sym['livemute_module'] - CODE + 16])
        u.u.mem_write(HOST, struct.pack('>I', version) + b'\0' * 0x100)
        u.u.mem_write(HOST + offs[1], struct.pack('>I', INTEREST))
        u.u.mem_write(HOST + offs[2], struct.pack('>I', ADDITEM))
        u.u.mem_write(UI, b'\0' * 0x40)
        u.u.mem_write(UI + offs[0], struct.pack('>I', HOST))
        u.u.mem_write(API, struct.pack('>2I', 3, UI))
        rc = u.call(desc[1], [API])
        if ok:
            assert rc == 0 and u.interest == [(sym['lm_native'], 1 << 20)], (version, rc, u.interest)
            u.call(sym['lm_settings'], [0x48009000])
            assert u.rows == [(0x48009000, (sym['lm_label'], sym['lm_select'], sym['lm_draw'], sym['lm_change']))]
        else:
            assert rc == 1 and not u.interest, (version, rc)
        assert desc[3] == 2
    print('init: refuses host API < 10; registers NOTE_ACCEPT interest only; SETTINGS row added through additem')


def main():
    a = argparse.ArgumentParser()
    a.add_argument('--cc', default=shutil.which(os.environ.get('ELEKLOADER_CROSS', 'm68k-elf-') + 'gcc'))
    args = a.parse_args()
    if not args.cc:
        print('SKIP: no ColdFire compiler'); return
    image, sym = build(args.cc)
    offs = offsets(args.cc)
    check_init(image, sym, offs)
    check_prefs(image, sym, offs)
    check(image, sym)


if __name__ == '__main__':
    main()
