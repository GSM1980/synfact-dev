"""The digital tracks' base-width filter (stock AUDIO 40002320), executed in Unicorn.

The filter takes its poles from the table at 40040060: T[BASE>>6] (high-pass)
and T[(BASE+WDTH)>>6] (low-pass). On the stock OS the table is 508 poles and a
zero tail; the core's AUDIO companion runs in that tail (40040860..40041050),
so core 1.7.2 bounds the low-pass index to 508 (mods/core-st/audio-bw-filter.s).

This runs the same stock routine on the stock AUDIO image and on the built
one, over BASE/WDTH values from the dial and modulation extremes, with
noise in all eight tracks for several blocks. The outputs and the filter
state must be identical. Negative control: the built image with the two
lookups restored to the stock instructions must differ (it reads the
companion's code as poles), and only where BASE+WDTH reaches the window.

Usage: test_st_bw_filter_emu.py --stock <Syntakt OS 1.41 .syx> <core build folder>
Needs a Unicorn with ColdFire EMAC, including MAC with load (LIBUNICORN_PATH;
docs/SETUP.md). Without it the test prints SKIPPED and exits 2. No device access.
"""
import argparse
import json
import random
import struct
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from unicorn import Uc, UcError, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN
from unicorn.m68k_const import UC_CPU_M68K_CFV4E, UC_M68K_REG_A7, UC_M68K_REG_PC
from elekloader.syx import Syx
from elekloader.sdk.syntakt.audio import BW_SITES, BW_STOCK

BASE, ENTRY, RET, STACK = 0x40000400, 0x40002320, 0x4f000000, 0x48010000
OUT, ROWS, STATE = 0x80003840, 0x80003c40, 0x800088a0   # the AUDIO IRQ's buffers
ROW, BASE_WORD, WDTH_WORD = 142, 102, 104                # row stride; byte offsets of 0x33/0x34
BLOCKS = 4


class Audio:
    def __init__(self, image):
        u = self.u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        for at, size in ((0x40000000, 0x80000), (0x80000000, 0x10000), (STACK - 0x10000, 0x20000), (RET, 0x1000)):
            u.mem_map(at, size)
        u.mem_write(BASE, image)
        u.mem_write(RET, b'\x4e\x71')

    def block(self, params, pcm):
        u = self.u
        for t, (base, width) in enumerate(params):
            u.mem_write(ROWS + ROW * t + BASE_WORD, struct.pack('>HH', base, width))
            u.mem_write(OUT + 128 * t, struct.pack('>32i', *pcm[t]))
        u.mem_write(STACK, struct.pack('>3I', RET, OUT, ROWS))
        u.reg_write(UC_M68K_REG_A7, STACK)
        u.emu_start(ENTRY, RET, count=100000)
        assert u.reg_read(UC_M68K_REG_PC) == RET and u.reg_read(UC_M68K_REG_A7) == STACK + 4
        return bytes(u.mem_read(OUT, 8 * 128)) + bytes(u.mem_read(STATE, 64))


def cases(seed):
    """Groups of eight (BASE, WDTH) words: one group fills the eight tracks."""
    dial = [v << 8 for v in (0, 1, 2, 3, 4, 5, 7, 8, 16, 32, 64, 100, 126, 127)]
    words = dial + [0x0040, 0x0140, 0x7e80, 0x7fc0, 0x7fff]
    pairs = [(b, w) for b in words for w in (0, 0x0100, 0x4000, 0x6400, 0x7f00, 0x7fff)]
    rnd = random.Random(seed)
    pairs += [(rnd.randrange(0x8000), rnd.randrange(0x8000)) for _ in range(160)]
    pairs += [(0, 0x7f00)] * (-len(pairs) % 8)
    groups = [pairs[i:i + 8] for i in range(0, len(pairs), 8)]
    return groups + [list(reversed(g)) for g in groups]   # each pair on both lookups


def opens_window(base, width):
    return (base + width) >> 6 >= 512


def run(stock, built, seed):
    stock_audio, built_audio = stock.section(7), built
    unbounded = bytearray(built_audio)
    for at in BW_SITES:
        unbounded[at - BASE:at - BASE + len(BW_STOCK)] = BW_STOCK
    rnd = random.Random(seed ^ 0x5a5a)
    same = differs = 0
    for group in cases(seed):
        machines = [Audio(stock_audio), Audio(built_audio), Audio(bytes(unbounded))]
        for _ in range(BLOCKS):
            pcm = [[rnd.randrange(-2**29, 2**29) for _ in range(32)] for _ in range(8)]
            ref, dut, bad = (m.block(group, pcm) for m in machines)
            assert dut == ref, ('bounded lookup differs from stock', group)
            if bad != ref:
                differs += 1
                assert any(opens_window(b, w) for b, w in group), ('stock differs below the window', group)
            else:
                same += 1
    assert differs, 'negative control: the unbounded lookup never read the companion window'
    return len(cases(seed)), differs, same


def emac_available(stock):
    try:
        Audio(stock.section(7)).block([(0, 0x7f00)] * 8, [[0] * 32] * 8)
        return True
    except UcError:
        return False


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stock', type=Path, required=True)
    p.add_argument('folder', type=Path)
    p.add_argument('--seed', type=int, default=1)
    a = p.parse_args()
    stock = Syx.load(a.stock)
    built = (a.folder / 'audio.bin').read_bytes()
    symbol = json.loads((a.folder / 'verification.json').read_text())['audio']['symbols']['audio_bw_index']
    for at in BW_SITES:
        assert built[at - BASE:at - BASE + 8] == b'\x4e\xb9' + struct.pack('>I', symbol) + b'\x4e\x71', hex(at)
    if not emac_available(stock):
        print('SKIPPED: this Unicorn has no ColdFire MAC with load (set LIBUNICORN_PATH; docs/SETUP.md)')
        sys.exit(2)
    groups, differs, same = run(stock, built, a.seed)
    print('PASS: base-width filter identical to stock over %d track groups x %d blocks; '
          'unbounded lookup differs in %d blocks (all reaching the window), same in %d'
          % (groups, BLOCKS, differs, same))


if __name__ == '__main__':
    main()
