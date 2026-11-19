"""The stereo engine's chain end (mods/samplers-st/stereo/audio.c), executed in Unicorn.

The real engine, built with the bank's compiler and flags, runs block after
block with stand-ins for the four native chain calls
(tests/st_stereo_engine_stubs.c): the first one logs the chain state the R
pass finds, and all of them write that state, as the native chain does.
Before each chain end the test plays the L pass: it writes new control and
signal state, and new L and R buffers.

Checked per block, for masks that come and go:
- the multimode runs from its second part (0x40001ea4) for each claimed
  track, with that track's coefficients from the L pass as its targets;
- the R pass takes the native drive only when a claimed track's OVERDRIVE
  (row word 0x28) is on or its wet weight is not 0, and its own otherwise;
- the R pass starts from the control state of the previous block's L pass
  (or this block's, when the previous block had no stereo track), and from
  R's own signal memory, as the previous R pass left it;
- after the chain end the control and signal state are exactly the L pass's;
- with no stereo track, one empty block, then the window left alone;
- the window block: SEQ = SEQ_END, the mask of tracks with a nonzero D, and
  each mantissa (D = R - L, as tests/test_st_stereo.c checks on the host).
It prints the instructions of a chain end (Unicorn counts instructions, not
cycles) and fails above the budget: the engine's own code, natives excluded.

Usage: test_st_stereo_engine_emu.py [--source <stereo/audio.c>]
Needs the m68k compiler on PATH (docs/SYNTAKT-BUILD.md; ELEKLOADER_CROSS='' and
GCC_EXEC_PREFIX for unprefixed binaries) and Unicorn. Without them it prints
SKIPPED and exits 2. No firmware, no device access.
"""
import argparse
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader.devices.syntakt import DEVICE
from elekloader.sdk import elf

BASE, STACK, RET = 0x47000000, 0x48010000, 0x4f000000
CONTROL = [(0x800083b0, 40), (0x80008550, 12), (0x80008740, 88), (0x800088e0, 8), (0x800049e0, 24)]
SIGNAL = [(0x80008580, 8), (0x800088a0, 16), (0x80008920, 16)]
NATIVES = {'stub_drive': 0x4000112c, 'stub_width': 0x40002320, 'stub_multi': 0x400019d6, 'stub_envelope': 0x40001476,
           'stub_multimode_tail': 0x40001ea4}
TRACKS, R0, WINDOW, ROWS = 0x80003840, 0x8000f400, 0x5b0, 0x80003c40
SEQ, MASK, SHIFT, D0, SEQ_END = 0x5b0, 0x5b4, 0x5b8, 0x5d0, 0x7d0
BUDGET = {1: 3800, 8: 10000, 'idle': 60}     # instructions per chain end, natives excluded


def skipped(why):
    print('SKIPPED: ' + why)
    sys.exit(2)


def build(source, work):
    prefix = os.environ.get('ELEKLOADER_CROSS', DEVICE.toolchain['prefix'])
    if not shutil.which(prefix + 'gcc'):
        skipped('no %sgcc on PATH (docs/SYNTAKT-BUILD.md)' % prefix)
    objs = []
    for k, src in enumerate((source, ROOT / 'tests/st_stereo_engine_stubs.c')):
        obj = work / ('o%d.o' % k)
        subprocess.run([prefix + 'gcc', *DEVICE.toolchain['cflags'], '-fno-jump-tables', '-I',
                        str(ROOT / 'elekloader/sdk/include'), '-c', str(src), '-o', str(obj)], check=True)
        objs.append(str(obj))
    script = work / 'engine.ld'
    script.write_text('SECTIONS { .image 0x%x : { *(.text*) *(.rodata*) *(.data*) *(.bss*) *(COMMON) } '
                      '/DISCARD/ : { *(.comment) *(.note*) *(.gnu.attributes) } }\n' % BASE)
    exe = work / 'engine.elf'
    subprocess.run([prefix + 'ld', '-T', str(script), '-o', str(exe), *objs], check=True)
    e = elf.Elf.load(exe)
    sec = e.section('.image')
    syms = {s.name: s.value for s in e.symbols if s.name and s.shndx == sec.idx}
    return sec.data, syms, sec.size


class Engine:
    def __init__(self, image, syms, size):
        from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
        from unicorn.m68k_const import UC_CPU_M68K_CFV4E
        self.u = u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        for at, n in ((0, 0x1000), (0x40000000, 0x100000), (BASE, (size + 0xffff) & ~0xffff),
                      (STACK - 0x10000, 0x20000), (RET, 0x1000), (0x80000000, 0x10000), (0xfc070000, 0x1000)):
            u.mem_map(at, n)
        u.mem_write(BASE, bytes(image))
        u.mem_write(RET, b'\x4e\x71')
        for name, at in NATIVES.items():           # jmp stub
            u.mem_write(at, struct.pack('>HI', 0x4ef9, syms[name]))
        u.mem_write(0x40040f30, struct.pack('>4I', 0x454c5301, 0x40040f40, 1, 0))
        u.mem_write(0x40040f40, b'\x4e\x75')
        self.syms = syms
        self.count = 0
        # Only the engine's code counts: the stand-ins are linked after it.
        stubs = min(at for name, at in syms.items() if name in NATIVES or name == 'touch')
        u.hook_add(UC_HOOK_CODE, self.code, begin=BASE, end=stubs - 1)

    def code(self, u, at, size, user):
        self.count += 1

    def call(self, name, *args):
        from unicorn.m68k_const import UC_M68K_REG_A7, UC_M68K_REG_PC
        u = self.u
        u.mem_write(STACK, struct.pack('>%dI' % (len(args) + 1), RET, *args))
        u.reg_write(UC_M68K_REG_A7, STACK)
        self.count = 0
        u.emu_start(self.syms[name], RET, count=2000000)
        assert u.reg_read(UC_M68K_REG_PC) == RET
        return self.count

    def words(self, at, n):
        return list(struct.unpack('>%dI' % n, bytes(self.u.mem_read(at, 4 * n))))

    def write(self, at, values, fmt='I'):
        self.u.mem_write(at, struct.pack('>%d%s' % (len(values), fmt), *values))

    def state(self, spans):
        return [w for at, n in spans for w in self.words(at, n)]

    def set_state(self, spans, values):
        k = 0
        for at, n in spans:
            self.write(at, values[k:k + n])
            k += n


def lctl_wet(lctl, previous_l, previous_mask, t):
    """Track t's drive wet weight (0x800083b0 + 4t, control word t) as the R pass finds it."""
    start = previous_l if previous_mask else lctl
    return start[t] != 0


def expected_block(l, r):
    """D mantissas and shift of one track, or None when D is zero (stereo/audio.c)."""
    d = [(b >> 1) - (a >> 1) for a, b in zip(l, r)]
    big = 0
    for x in d:
        big |= (~x if x < 0 else x)
    if big < 0x8000:                                  # |D| under ST_STEREO_QUIET: mono
        return None
    e = 0
    while e < 16 and (big >> e) > 32767:
        e += 1
    return e, [max(x >> e, -32767) for x in d]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--source', default=str(ROOT / 'mods/samplers-st/stereo/audio.c'))
    args = ap.parse_args()
    try:
        import unicorn  # noqa: F401
    except ImportError:
        skipped('no unicorn (docs/SETUP.md)')
    with tempfile.TemporaryDirectory() as tmp:
        image, syms, size = build(Path(args.source), Path(tmp))
    eng = Engine(image, syms, size)
    rnd = random.Random(20261110)
    rsig = eng.state([(0x8000f800, 40)])               # ST_STEREO_SIGNAL: R's own memory
    previous_l, previous_mask = None, 0
    masks = [0xff, 0xff, 0x01, 0x81, 0, 0x3c, 0xff, 0, 0, 0xfe, 0xff, 0x10]
    costs = {}
    for block, mask in enumerate(masks):
        # The L pass: new chain state and buffers.
        lctl = [rnd.getrandbits(32) for _ in range(172)]
        lsig = [rnd.getrandbits(32) for _ in range(40)]
        eng.set_state(CONTROL, lctl)
        eng.set_state(SIGNAL, lsig)
        l = [[rnd.randrange(-2 ** 31, 2 ** 31) for _ in range(32)] for _ in range(8)]
        r = [[x if (t == 2 and block % 2) else rnd.randrange(-2 ** 31, 2 ** 31) >> rnd.randrange(0, 20)
              for x in l[t]] for t in range(8)]
        for t in range(8):
            eng.write(TRACKS + 128 * t, l[t], 'i')
        calls = eng.words(syms['stub_calls'], 1)[0]
        eng.write(syms['stub_pass'], [0])
        eng.write(syms['stub_tails'], [0])
        # OVERDRIVE (row word 0x28) on for some tracks in some blocks: the
        # engine runs the native drive then, its own otherwise.
        drive_on = block % 3 == 1
        for t in range(8):
            eng.write(ROWS + 142 * t + 0x50, [0x100 if drive_on and t == 3 else 0], 'h')
        for t in range(8):
            if mask >> t & 1:
                assert eng.call('st_stereo_claim', t) is not None
                eng.write(R0 + 128 * t, r[t], 'i')
        window = bytes(eng.u.mem_read(SEQ, 0x224))
        n = eng.call('st_stereo_chain_end')
        k = bin(mask).count("1")
        if mask or previous_mask:
            costs[k] = max(costs.get(k, 0), n)        # 0 tracks: the block that sends the empty one
        else:
            costs['idle'] = max(costs.get('idle', 0), n)
        if not mask and not previous_mask and block:
            # still no stereo track: the empty block already sent stays
            assert bytes(eng.u.mem_read(SEQ, 0x224)) == window, ('window rewritten while idle', block)
        assert eng.state(CONTROL) == lctl, ('control state after the chain end', block)
        assert eng.state(SIGNAL) == lsig, ('signal state after the chain end', block)
        if mask:
            assert eng.words(syms['stub_pass'], 1)[0] >= 1
            native_drive = eng.words(syms['stub_calls'], 1)[0] - calls
            assert native_drive == (1 if (drive_on and mask >> 3 & 1) or any(lctl_wet(lctl, previous_l, previous_mask, t)
                                                                                for t in range(8) if mask >> t & 1) else 0),                 ('native drive call', block)
            log = eng.words(syms['stub_log'], 212)
            start = previous_l if previous_mask else lctl
            assert log[:172] == start, ('the R pass control state', block)
            assert log[172:] == rsig, ('the R pass signal state', block)
            rsig = eng.state([(0x8000f800, 40)])
            assert rsig != log[172:]                    # the R pass's writes are kept for R
            # the multimode's second part, once a claimed track, with the L
            # pass's coefficients of the track as its targets
            assert eng.words(syms['stub_tails'], 1)[0] == bin(mask).count('1'), ('multimode tails', block)
            last = max(t for t in range(8) if mask >> t & 1)
            assert eng.words(syms['stub_tail_track'], 1)[0] == last
            assert eng.words(syms['stub_tail_targets'], 8) == lctl[52 + 8 * last:60 + 8 * last], ('targets', block)
        else:
            assert eng.words(syms['stub_pass'], 1)[0] == 0
        seq, got_mask = eng.words(SEQ, 2)
        assert eng.words(SEQ_END, 1)[0] == seq
        shifts = bytes(eng.u.mem_read(SHIFT, 8))
        want_mask = 0
        for t in range(8):
            if not mask >> t & 1:
                continue
            want = expected_block(l[t], r[t])
            if want is None:
                continue
            want_mask |= 1 << t
            e, m = want
            assert shifts[t] == e, ('shift', block, t)
            words = eng.words(D0 + 64 * t, 16)
            got = []
            for w in words:
                got += [(w >> 16) - ((w >> 15) & 0x10000), (w & 0xffff) - ((w << 1) & 0x10000)]
            assert got == m, ('mantissas', block, t)
        assert got_mask == want_mask, ('mask', block, hex(got_mask), hex(want_mask))
        previous_l, previous_mask = lctl, mask
    print('chain end, natives excluded (instructions): ' +
          ', '.join('%s tracks %d' % (k, costs[k]) for k in sorted(costs, key=str)))
    for tracks, budget in BUDGET.items():
        assert costs[tracks] <= budget, ('over budget', tracks, costs[tracks], budget)
    print('PASS: R pass from the previous L state and its own signal memory, L state restored, '
          'D mantissas and mask (%d blocks); within budget' % len(masks))


if __name__ == '__main__':
    main()
