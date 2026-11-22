"""The STEREO module's MAIN read of the window by eDMA (samplers-st stereo/main.c), in Unicorn.

tests/st_stereo_main_dma_check.c builds the module's MAIN side with the bank's
compiler and flags; Unicorn runs it with the shared window at 0x10000000 and
MAIN eDMA channel 46 emulated: a write of 46 to SSRT checks the TCD (16-byte
bursts, the channel linked to itself, descending: SEQ_END read first and SEQ
last) and copies the block, chunk by chunk, in that order. Checked:
- ev_render_in starts one transfer into slot 0 (16-byte aligned), and none
  when AUDIO wrote no block since the last one;
- the first mix event takes the block whose L fingerprint matches, as D;
- a block AUDIO writes after the transfer is read by the CPU (LATE);
- a transfer that overlaps AUDIO's write (SEQ != SEQ_END) is not used
  (TORN), and the CPU reads the finished block;
- an untagged window and an empty mask give no D.
Unicorn counts instructions, not cycles, and has no eDMA: the emulation
follows the eDMA's documented TCD semantics, not a measured unit.

Usage: test_st_stereo_main_dma_emu.py
Needs the m68k compiler on PATH (docs/SYNTAKT-BUILD.md) and Unicorn (the
patched build: LIBUNICORN_PATH). Without them it prints SKIPPED and exits 2.
No firmware, no device access.
"""
import os
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
WIN, SEQ, MASK, SHIFT, PRINT, TAG, D0, SEQ_END = 0x10000000, 0x5b0, 0x5b4, 0x5b8, 0x5c0, 0x5c4, 0x5d0, 0x7d0
MAGIC, TRACK_PCM, TCD, SSRT = 0x53545232, 0x80003d50, 0xfc0455c0, 0xfc04401e
COUNTERS = ('interrupts', 'received', 'torn', 'dropped', 'stale', 'late', 'mask', 'again', 'read', 'mix')


def skipped(why):
    print('SKIPPED: ' + why)
    sys.exit(2)


def build(work):
    prefix = os.environ.get('ELEKLOADER_CROSS', DEVICE.toolchain['prefix'])
    if not shutil.which(prefix + 'gcc'):
        skipped('no %sgcc on PATH (docs/SYNTAKT-BUILD.md)' % prefix)
    objs = []
    stereo = ROOT / 'mods/samplers-st/stereo'
    for k, src in enumerate((ROOT / 'tests/st_stereo_main_dma_check.c', stereo / 'resolve-begin.s',
                             stereo / 'main-resolve.c', stereo / 'resolve-end.s', stereo / 'main-kernels.s')):
        obj = work / ('o%d.o' % k)
        if src.suffix == '.c':
            cmd = [prefix + 'gcc', *DEVICE.toolchain['cflags'], '-fno-jump-tables', '-I',
                   str(ROOT / 'elekloader/sdk/include'), '-c', str(src), '-o', str(obj)]
        else:
            cmd = [prefix + 'as', *DEVICE.toolchain['asflags'], str(src), '-o', str(obj)]
        subprocess.run(cmd, check=True)
        objs.append(str(obj))
    script = work / 'main.ld'
    script.write_text('SECTIONS { .image 0x%x : { *(.text*) *(.rodata*) *(.data*) . = ALIGN(16); *(.bss*) *(COMMON) } '
                      '/DISCARD/ : { *(.comment) *(.note*) *(.gnu.attributes) } }\n' % BASE)
    exe = work / 'main.elf'
    subprocess.run([prefix + 'ld', '-T', str(script), '-o', str(exe), *objs], check=True)
    e = elf.Elf.load(exe)
    sec = e.section('.image')
    syms = {s.name: s.value for s in e.symbols if s.name and s.shndx == sec.idx}
    return sec.data, syms, sec.size


def s16(x):
    return x - 0x10000 if x & 0x8000 else x


def fingerprint(tracks):
    h = 0x811c9dc5
    for t in range(8):
        for i in range(0, 32, 8):
            h = ((h * 33) ^ (tracks[32 * t + i] & 0xffffffff)) & 0xffffffff
    return h


class Main:
    def __init__(self, image, syms, size):
        from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_MEM_WRITE
        from unicorn.m68k_const import UC_CPU_M68K_CFV4E
        self.u = u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        for at, n in ((WIN, 0x1000), (BASE, (size + 0xffff) & ~0xffff), (STACK - 0x10000, 0x20000),
                      (RET, 0x1000), (0x80000000, 0x10000), (0xfc070000, 0x1000), (0xfc044000, 0x2000)):
            u.mem_map(at, n)
        image = bytearray(image)
        # Unicorn has no cache and no CPUSHL: start_read's pushes (the state
        # is in DDR here) become NOPs; check they are there.
        at, pushes = syms.get('start_read', syms['receive']) - BASE, 0     # GCC may inline it
        for k in range(at, at + 0x200, 2):
            if image[k] == 0xf4 and image[k + 1] & 0xf8 == 0x68:
                image[k:k + 2] = b'\x4e\x71'
                pushes += 1
        assert pushes == 1, 'start_read pushes the block lines (CPUSHL DC,(An))'
        u.mem_write(BASE, bytes(image))
        u.mem_write(RET, b'\x4e\x71')
        self.syms = syms
        self.transfers = 0
        self.during = None          # called between the chunks of a transfer: (chunk index)
        u.hook_add(UC_HOOK_MEM_WRITE, self.ssrt, begin=SSRT, end=SSRT)

    def ssrt(self, u, access, at, size, value, user):
        assert value == 46, ('SSRT', value)
        saddr, attr, soff, nbytes, slast, daddr, citer, doff, dlast, biter, csr = \
            struct.unpack('>IHHIIIHHIHH', bytes(u.mem_read(TCD, 32)))
        assert (attr, nbytes, slast, dlast, csr) == (0x0404, 16, 0, 0, 0), 'TCD fields'
        assert soff == doff == 0xfff0, ('descending', hex(soff), hex(doff))
        assert citer == biter and citer >> 15 == 1 and (citer >> 9) & 0x3f == 46, ('link', hex(citer))
        loops = citer & 0x1ff
        assert loops == 35 and saddr == WIN + SEQ + 0x220 and daddr & 15 == 0, (loops, hex(saddr), hex(daddr))
        for k in range(loops):
            if self.during:
                self.during(k)
            u.mem_write(daddr - 16 * k, bytes(u.mem_read(saddr - 16 * k, 16)))
        u.mem_write(TCD + 0x1e, struct.pack('>H', 0x80))      # DONE
        self.transfers += 1

    def call(self, name, *args):
        from unicorn.m68k_const import UC_M68K_REG_A7
        self.u.mem_write(STACK, struct.pack('>%dI' % (len(args) + 1), RET, *args))
        self.u.reg_write(UC_M68K_REG_A7, STACK)
        self.u.emu_start(self.syms[name], RET, count=2000000)
        from unicorn.m68k_const import UC_M68K_REG_D0
        return self.u.reg_read(UC_M68K_REG_D0)

    def words(self, at, n):
        return list(struct.unpack('>%dI' % n, bytes(self.u.mem_read(at, 4 * n))))

    def counters(self):
        return dict(zip(COUNTERS, self.words(self.syms['st_stereo_main_counters'], len(COUNTERS))))

    def write(self, at, data):
        self.u.mem_write(at, data)


class Audio:
    """AUDIO's side: L for MAIN's track DMA and the window block (ascending)."""
    def __init__(self, main):
        self.m = main
        self.seq = 0
        self.l = [0] * 256

    def block(self, mask, seed, tag=MAGIC):
        self.seq += 1
        s, r = self.seq, seed
        self.l = []
        for i in range(256):
            r = (r * 1103515245 + 12345) & 0xffffffff
            self.l.append(r - (1 << 32) if r >> 31 else r)
        shifts = [(seed + t) % 17 for t in range(8)]
        d = [[s16((seed * 131 + 7 * t + 977 * i) & 0xffff) for i in range(32)] for t in range(8)]
        for t in range(8):
            d[t] = [max(v, -32767) for v in d[t]]
        words = [s, mask, struct.unpack('>I', bytes(shifts[:4]))[0], struct.unpack('>I', bytes(shifts[4:]))[0],
                 fingerprint(self.l), tag, s, 0]
        body = b''.join(struct.pack('>I', w) for w in words)
        body += b''.join(struct.pack('>32h', *d[t]) for t in range(8))
        body += struct.pack('>I', s)
        assert len(body) == 0x224
        return body, shifts, d

    def write(self, body, upto=None):
        """Ascending, as AUDIO's eDMA writes; upto: only the first bytes."""
        self.m.write(WIN + SEQ, body[:upto] if upto is not None else body)

    def show_l(self):
        self.m.write(TRACK_PCM, struct.pack('>256i', *self.l))


def expect_d(main, t, shift, d):
    out = [s16(w >> 16) << 16 | (w & 0xffff) for w in main.words(main.syms['dma_out'], 32)]
    out = [w - (1 << 32) if w >> 31 else w for w in main.words(main.syms['dma_out'], 32)]
    e = shift - 15
    want = [v << e if e >= 0 else v >> -e for v in d]
    assert out == want, ('D', t)


def main():
    try:
        import unicorn  # noqa: F401
    except ImportError:
        skipped('no Unicorn')
    with tempfile.TemporaryDirectory() as tmp:
        image, syms, size = build(Path(tmp))
    m = Main(image, syms, size)
    a = Audio(m)
    m.call('dma_init')
    slot0 = m.call('dma_slot0')
    assert slot0 & 15 == 0, hex(slot0)

    # A new block: one transfer, D of the matching block.
    body, shifts, d = a.block(0x81, 11)
    a.write(body)
    a.show_l()
    flags = m.call('dma_receive')
    assert m.transfers == 1 and flags & 4, flags
    assert m.call('dma_track', 0) == 1 and m.call('dma_used') == 1
    expect_d(m, 0, shifts[0], d[0])
    assert m.call('dma_track', 7) == 1
    expect_d(m, 7, shifts[7], d[7])
    assert m.call('dma_track', 3) == 0
    c = m.counters()
    assert c['received'] == 1 and c['torn'] == 0 and c['late'] == 0, c

    # No block since: no transfer, the same D again.
    flags = m.call('dma_receive')
    assert m.transfers == 1 and flags & 1 and not flags & 4, flags
    assert m.call('dma_track', 0) == 1
    expect_d(m, 0, shifts[0], d[0])
    assert m.counters()['again'] == 1

    # AUDIO writes again after MAIN's transfer: the CPU reads the newer block.
    body, shifts, d = a.block(0x3c, 23)
    a.write(body)
    m.call('dma_receive')
    assert m.transfers == 2
    body, shifts, d = a.block(0x3c, 29)
    a.write(body)
    a.show_l()                       # MAIN mixes the newer L
    assert m.call('dma_track', 2) == 1 and m.call('dma_used') == 2
    expect_d(m, 2, shifts[2], d[2])
    assert m.counters()['late'] == 1

    # A transfer that overlaps AUDIO's write: SEQ_END (read first) is the
    # old block's, SEQ (read last) the new one's. Not used; the CPU reads
    # the finished block.
    body, shifts, d = a.block(0xff, 37)
    def overlap(k):
        if k == 30:
            a.write(body, upto=0x40)
    m.during = overlap
    torn = m.counters()['torn']
    m.call('dma_receive')
    m.during = None
    assert m.transfers == 3
    slot = m.words(slot0, 0x230 // 4)
    assert slot[0] != slot[0x220 // 4], 'the emulated overlap'
    a.write(body)
    a.show_l()
    assert m.call('dma_track', 5) == 1 and m.call('dma_used') == 2
    expect_d(m, 5, shifts[5], d[5])
    assert m.counters()['torn'] == torn + 1

    # An empty mask, then an untagged window.
    body, shifts, d = a.block(0, 41)
    a.write(body)
    a.show_l()
    m.call('dma_receive')
    assert m.call('dma_track', 0) == 0 and m.counters()['mask'] == 0
    body, shifts, d = a.block(0xff, 43, tag=0)
    a.write(body)
    a.show_l()
    flags = m.call('dma_receive')
    assert m.call('dma_track', 0) == 0
    print('PASS: MAIN eDMA read of the window: %d transfers (descending, self-linked TCD), new/again/late/torn/'
          'empty/untagged blocks paired as the CPU read pairs them; counters %s' % (m.transfers, m.counters()))


if __name__ == '__main__':
    main()
