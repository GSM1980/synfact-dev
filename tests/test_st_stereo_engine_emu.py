"""The stereo engine's chain end and multimode entry (mods/samplers-st/stereo/audio.c), in Unicorn.

The real engine, built with the bank's compiler and flags, runs block after
block with stand-ins for the native chain calls
(tests/st_stereo_engine_stubs.c), which count their calls and write the
chain state as the native chain does. (Whether the engine's R equals a
native R pass, word for word, is tests/test_st_stereo_natives_emu.py's
check_chain, with the real natives in analog-emu.)

Checked per block, for masks that come and go:
- a claim installs the chain-end hook (word 1) and the multimode entry
  (word 3) of a version-2 chain directory; a version-1 directory gets no
  stereo track;
- the multimode entry sends a mono track straight to the native routine,
  and runs a claimed track's L call, its R tail (0x40001ea4) and its R
  envelope;
- the chain end leaves the chain state alone, keeps the drive state for the
  next block's R drive, and zeroes R of a track that left the claim;
- with no stereo track, one empty block, then the window left alone;
- the window block, sent by AUDIO eDMA channel 46 (emulated: the TCD links
  the channel to itself, one start moves the block): SEQ = SEQ_END, the mask
  of tracks with a nonzero D, and each mantissa (D = R - L, as
  tests/test_st_stereo.c checks on the host).
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
BUDGET = {1: 2000, 8: 5200, 'idle': 60}      # instructions per chain end, natives excluded (R zeroing included)


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
    obj = work / 'multimode.o'
    subprocess.run([prefix + 'as', *DEVICE.toolchain['asflags'], str(ROOT / 'mods/samplers-st/stereo/multimode.s'),
                    '-o', str(obj)], check=True)
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
                      (STACK - 0x10000, 0x20000), (RET, 0x1000), (0x80000000, 0x10000), (0xfc070000, 0x1000),
                      (0xfc044000, 0x2000)):
            u.mem_map(at, n)
        # eDMA: a write of 46 to SSRT runs channel 46's TCD as the unit would
        # (one start, minor loops linked to the channel itself) and checks it.
        from unicorn import UC_HOOK_MEM_WRITE
        self.dma = 0
        u.hook_add(UC_HOOK_MEM_WRITE, self.ssrt, begin=0xfc04401e, end=0xfc04401e)
        u.mem_write(BASE, bytes(image))
        u.mem_write(RET, b'\x4e\x71')
        for name, at in NATIVES.items():           # jmp stub
            u.mem_write(at, struct.pack('>HI', 0x4ef9, syms[name]))
        u.mem_write(0x40040f30, struct.pack('>4I', 0x454c5301, 0x40040f40, 2, 0x400019d6))
        u.mem_write(0x40040f40, b'\x4e\x75')
        self.syms = syms
        self.count = 0
        # Only the engine's code counts: the stand-ins are linked after it.
        stubs = min(at for name, at in syms.items() if name in NATIVES or name == 'touch')
        u.hook_add(UC_HOOK_CODE, self.code, begin=BASE, end=stubs - 1)

    def code(self, u, at, size, user):
        self.count += 1

    def ssrt(self, u, access, at, size, value, user):
        assert value == 46, ('SSRT', value)
        tcd = bytes(u.mem_read(0xfc0455c0, 32))
        # ColdFire order: SADDR, ATTR, SOFF, NBYTES, SLAST, DADDR, CITER, DOFF,
        # DLAST_SGA, BITER, CSR
        saddr, attr, soff, nbytes, slast, daddr, citer, doff, dlast, biter, csr = struct.unpack('>IHHIIIHHIHH', tcd)
        assert (attr, soff, nbytes, slast, doff, dlast, csr) == (0x0404, 16, 16, 0, 16, 0, 0), \
            ('TCD fields', attr, soff, nbytes, slast, doff, dlast, csr)
        assert citer == biter and citer >> 15 == 1 and (citer >> 9) & 0x3f == 46, ('minor loop link', hex(citer))
        loops = citer & 0x1ff
        assert daddr == WINDOW and loops * 16 == 0x230 and saddr & 15 == 0, ('block', hex(daddr), loops, hex(saddr))
        u.mem_write(daddr, bytes(u.mem_read(saddr, loops * 16)))
        self.dma += 1

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
    # A version-1 directory (core 1.8 to 2.4): no stereo track.
    old = Engine(image, syms, size)
    old.write(0x40040f30, [0x454c5301, 0x40040f40, 1, 0])
    old.call('st_stereo_claim', 0)
    assert old.words(0x40040f30, 4)[1:] == [0x40040f40, 1, 0], 'version-1 directory'
    eng = Engine(image, syms, size)
    rnd = random.Random(20261110)
    previous_mask = 0
    masks = [0xff, 0xff, 0x01, 0x81, 0, 0x3c, 0xff, 0, 0, 0xfe, 0xff, 0x10]
    costs = {}
    counted = ('stub_multis', 'stub_tails', 'stub_envelopes')
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
        for t in range(8):
            if mask >> t & 1:
                eng.call('st_stereo_claim', t)
                eng.write(R0 + 128 * t, r[t], 'i')
        if mask:
            assert eng.words(0x40040f30, 4) == [0x454c5301, syms['st_stereo_chain_end'], 2,
                                                syms['st_stereo_multimode']], 'installed'
            # the multimode entry: a mono track (not track 0, whose call also
            # runs R's drive and base-width) and a claimed track
            mono = next((t for t in range(1, 8) if not mask >> t & 1), None)
            stereo = max(t for t in range(8) if mask >> t & 1)
            for t in ([mono] if mono is not None else []) + [stereo]:
                before = [eng.words(syms[n], 1)[0] for n in counted]
                rbefore = eng.words(R0 + 128 * t, 32)
                eng.call('st_stereo_multimode', ROWS + 142 * t + 0x52, TRACKS + 128 * t, 0, t, 0, 0)
                after = [eng.words(syms[n], 1)[0] for n in counted]
                assert after[0] == before[0] + 1, ('the native multimode on L', block, t)
                if t == stereo and t:
                    assert after[1:] == [before[1] + 1, before[2] + 1], ('R tail and envelope', block, t)
                    assert eng.words(syms['stub_tail_track'], 1)[0] == t
                elif t != stereo:
                    assert after[1:] == before[1:], ('a mono track gets no R', block, t)
                    assert eng.words(R0 + 128 * t, 32) == rbefore
            eng.set_state(CONTROL, lctl)
            eng.set_state(SIGNAL, lsig)
            for t in range(8):
                if mask >> t & 1:
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
            assert eng.words(0x8000f8a0, 40) == lctl[:40], ('the drive state kept for the next block', block)
        for t in range(8):
            got = eng.words(R0 + 128 * t, 32)
            if previous_mask >> t & 1 and not mask >> t & 1:
                assert got == [0] * 32, ('R of a track that left the claim', block, t)
            elif mask >> t & 1:
                assert got == [x & 0xffffffff for x in r[t]], ('R kept', block, t)
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
        previous_mask = mask
    assert eng.dma > 0
    print('chain end, natives excluded (instructions): ' +
          ', '.join('%s tracks %d' % (k, costs[k]) for k in sorted(costs, key=str)))
    for tracks, budget in BUDGET.items():
        assert costs[tracks] <= budget, ('over budget', tracks, costs[tracks], budget)
    print('PASS: version-2 directory installed (none on version 1); multimode entry (mono: native; '
          'stereo: L, R tail, R envelope); the chain end leaves the chain state, keeps the drive state, '
          'zeroes R that left the claim; D mantissas and mask by eDMA (%d blocks); within budget' % len(masks))


if __name__ == '__main__':
    main()
