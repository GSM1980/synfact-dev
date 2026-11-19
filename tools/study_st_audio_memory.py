# SPDX-License-Identifier: GPL-2.0-or-later
"""Map the stock Syntakt AUDIO image's memory use (docs/AUDIO-MEMORY-MAP.md).

    python tools/study_st_audio_memory.py [--stock OS.syx] [--out report.json]

Reads section 7 (the AUDIO image, loaded at 40000400) from the stock OS file
($SYNTAKT_STOCK by default) and:

1. checks the boot facts the map relies on, instruction by instruction (the SRAM
   copies, the clear of 4004f6e0..4404f980, the stack, the cache window, the page
   receiver, the vector of the block interrupt, the idle loop);
2. walks the code from the entry, the vectors, the code pointers its instructions
   take, the image's pointer tables and the function prologues (a start whose path
   meets a word that is not a ColdFire ISA_C/EMAC instruction is dropped);
3. lists every absolute address the reached code names, by region;
4. bounds the stack: frames along the static call graph, each indirect call taken
   to reach any routine of the image's pointer tables.

Nothing from the firmware is written but addresses and counts.
"""
import argparse
import bisect
import collections
import json
import os
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader import syx  # noqa: E402
from elekloader.isa import coldfire as cf  # noqa: E402

BASE = 0x40000400
SRAM_COPIES = ((0x4004f6e0, 0x40057670, 0x80000000, 0x80008000), (0x40057670, 0x4005df10, 0x80008000, 0x80010000))
BSS = (0x4004f6e0, 0x4404f980)
STORE = (0x4004f750, 0x4404f750)
CODE_END = 0x40009f20          # the reached code ends at 40009f1c
REGIONS = ((0x00000000, 0x00001000, 'FPGA window (AUDIO address 0)'), (0x10000000, 0x10010000, 'FPGA window (10000000)'),
           (0x40000000, 0x40000400, 'vector table'), (BASE, CODE_END, 'code'), (CODE_END, BSS[0], 'image data'),
           (BSS[0], STORE[0], 'zeroed data, low'), (STORE[0], STORE[1], 'sample store'),
           (STORE[1], BSS[1], 'zeroed data, high'), (BSS[1], 0x48000000, 'above the zeroed data'),
           (0x80000000, 0x80010000, 'SRAM'), (0xec000000, 0xf0000000, 'peripherals (ec)'),
           (0xfc000000, 0x100000000, 'peripherals (fc)'))
# (address, instruction words, what it shows)
FACTS = ((0x40000400, '40001070', 'the image starts with its entry'),
         (0x4000107a, '2e7c48000000', 'entry: SP = 48000000'),
         (0x400010d4, '203c4007e020', 'ACR0 = 4007e020: 40000000..47ffffff cached'),
         (0x400004be, '45f94004f6e0', 'SRAM copy 1 from 4004f6e0'),
         (0x400004d0, 'b5fc40057670', '... to 40057670 (-> 80000000), the rest of 80000000..80007fff cleared'),
         (0x400004de, 'b1fc80008000', ''),
         (0x400004e6, '45f940057670', 'SRAM copy 2 from 40057670'),
         (0x400004f8, 'b5fc4005df10', '... to 4005df10 (-> 80008000), the rest to 8000ffff cleared'),
         (0x40000506, 'b1fc80010000', ''),
         (0x4000051a, '207c4004f6e0', 'clear from 4004f6e0'),
         (0x40000520, '223c4404f980', '... to 4404f980: the zeroed data, sample store included'),
         (0x40000cdc, '48794004f750', 'sample store at 4004f750 ...'),
         (0x40000ce2, '2f3c04000000', '... of 04000000 bytes (64 MiB)'),
         (0x40000d2a, '45f94404f758', 'sample table: addresses at 4404f758 + 8 i'),
         (0x40000d30, '43f94404f754', 'sample table: lengths at 4404f754 + 8 i'),
         (0x40000d6c, '0c8100000200', '... 64 entries (512 bytes)'),
         (0x40000fa8, '302a000c', 'page receiver: the page index, a 16-bit word of the window'),
         (0x40000fb8, 'e3a8', '... times 2048'),
         (0x40000fba, '06804004f750', '... plus 4004f750: up to 4004f750 + ffff * 800 = 4804ef50'),
         (0x40000dfa, '487940042000', 'boot blob unpacked to 40042000 ...'),
         (0x40000e00, '487940041060', '... from 40041060'),
         (0x40001064, '223c40000934', 'the block interrupt, 40000934 ...'),
         (0x4000106a, '21410180', '... installed as vector 96'),
         (0x4000106e, '60fe', 'then the main context idles (bra .)'))


def region(v):
    for lo, hi, name in REGIONS:
        if lo <= v < hi: return name
    return None


class Image:
    def __init__(self, img):
        self.img = img

    def phys(self, a):
        if BASE <= a < BASE + len(self.img) - 1: return a - BASE
        for src, end, dst, _top in SRAM_COPIES:
            if dst <= a < dst + end - src: return a - dst + src - BASE
        return None

    def at(self, addr):
        def r(off):
            o = self.phys(addr + off)
            if o is None: raise IndexError(addr + off)
            return self.img[o] << 8 | self.img[o + 1]
        return r

    def u32(self, a): return struct.unpack_from('>I', self.img, a - BASE)[0]


def trace(im, code_hi):
    seen, refs, cand, rejected = {}, collections.defaultdict(set), set(), set()

    def code_space(v): return not v & 1 and BASE <= v < code_hi

    def explore(start):
        local, found, work = {}, set(), [start]
        while work:
            pc, prev = work.pop(), None
            while pc not in seen and pc not in local:
                if im.phys(pc) is None or pc & 1: return None
                try: ins = cf.decode(im.at(pc), pc)
                except Exception: return None
                if ins.flags & (cf.ILLEGAL | cf.FPU | cf.LINEF):
                    if prev is not None and prev.flow in (cf.F_JSR, cf.F_BSR): break   # a call that does not return
                    return None
                prev = local[pc] = ins
                if ins.src in (cf.ABS, cf.IMM) or ins.dst == cf.ABS or ins.op.startswith(('lea', 'pea')):
                    w = [im.at(pc)(k) for k in range(0, ins.length, 2)]
                    found.update((w[k] << 16 | w[k + 1], pc) for k in range(1, len(w) - 1))
                if ins.flags & cf.PCREL and ins.op.startswith(('lea', 'pea')):
                    d = im.at(pc)(2); found.add((pc + 2 + (d - 0x10000 if d & 0x8000 else d), pc))
                if isinstance(ins.target, int) and ins.flow in (cf.F_BCC, cf.F_BSR, cf.F_JSR, cf.F_BRA, cf.F_JMP):
                    work.append(ins.target)
                if ins.flow in (cf.F_RTS, cf.F_RTE, cf.F_BRA, cf.F_JMP): break
                pc += ins.length
        return local, found

    def add(v):
        r = explore(v)
        if r is None: rejected.add(v); return
        seen.update(r[0])
        for t, pc in r[1]:
            refs[t].add(pc)
            if code_space(t): cand.add(t)

    for s in (0x40001070, 0x40000404, 0x40000442, 0x40000934): add(s)
    img, tables = im.img, set()
    for off in range(0, BSS[0] - BASE - 8, 2):          # pointer tables anywhere in the image
        v, w = struct.unpack_from('>II', img, off)
        if code_space(v) and code_space(w): tables.update((v, w))
    prologues = set()
    for off in range(2, code_hi - BASE, 2):
        w, p = img[off] << 8 | img[off + 1], img[off - 2] << 8 | img[off - 1]
        if (w == 0x4fef and img[off + 2] & 0x80 or w == 0x4e56) and p in (0x4e75, 0x4e73, 0x4e71, 0):
            prologues.add(off + BASE)
    cand |= tables | prologues
    while cand:
        v = cand.pop()
        if v not in seen and v not in rejected: add(v)
    return seen, refs, rejected


def stack(im, seen):
    starts = {0x40001070, 0x40000934}
    for a, i in seen.items():
        if i.flow in (cf.F_JSR, cf.F_BSR) and isinstance(i.target, int): starts.add(i.target)
        if im.at(a)(0) in (0x4fef, 0x4e56) and a - 2 in seen and seen[a - 2].flow in (cf.F_RTS, cf.F_RTE): starts.add(a)
    pointers = {im.u32(BASE + o) for o in range(0, BSS[0] - BASE - 4, 2)}
    tabled = sorted(s for s in pointers if s in seen)
    starts = sorted((starts | set(tabled)) & set(seen))
    frame = dict.fromkeys(starts, 0); calls = {s: set() for s in starts}; indirect = dict.fromkeys(starts, False)
    for a, i in seen.items():
        f = starts[bisect.bisect_right(starts, a) - 1] if a >= starts[0] else None
        if f is None: continue
        w = im.at(a)(0)
        if w in (0x4fef, 0x4e56):
            d = im.at(a)(2); d = d - 0x10000 if d & 0x8000 else d
            if w == 0x4e56: frame[f] += 4 - d
            elif d < 0: frame[f] += -d
        elif (w & 0xffc0) == 0x2f00 or w in (0x4878, 0x4879) or (w & 0xfff8) == 0x4850:
            frame[f] += 4                                   # every push counted: an upper bound
        if i.flow in (cf.F_JSR, cf.F_BSR):
            if isinstance(i.target, int) and i.target in frame: calls[f].add(i.target)
            else: indirect[f] = True
    sys.setrecursionlimit(20000)
    memo, active = {}, set()

    def depth(f):
        if f in memo: return memo[f]
        if f in active: return 0
        active.add(f)
        sub = [depth(c) for c in calls[f]] + ([depth(t) for t in tabled if t != f] if indirect[f] else [])
        active.discard(f)
        memo[f] = frame[f] + 4 + max(sub, default=0)
        return memo[f]
    return {'functions': len(starts), 'table_routines': len(tabled), 'boot_path_bytes': depth(0x40001070),
            'interrupt_path_bytes': depth(0x40000934)}


def main():
    p = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    p.add_argument('--stock', default=os.environ.get('SYNTAKT_STOCK'), help='stock Syntakt OS 1.42 .syx ($SYNTAKT_STOCK)')
    p.add_argument('--out', type=Path, help='write the report as JSON')
    a = p.parse_args()
    if not a.stock: p.error('give --stock or set $SYNTAKT_STOCK')
    im = Image(syx.Syx.load(a.stock).section(7))
    report = {'image': [hex(BASE), hex(BASE + len(im.img))], 'facts': [], 'failed': []}
    for at, words, what in FACTS:
        o = im.phys(at); got = im.img[o:o + len(words) // 2].hex()
        (report['facts'] if got == words else report['failed']).append('%08x %s %s' % (at, got, what))
    seen, refs, rejected = trace(im, CODE_END)
    code = sorted(seen)
    covered = sum(seen[x].length for x in code)
    report['code'] = {'from': hex(code[0]), 'to': hex(code[-1] + seen[code[-1]].length), 'instructions': len(code),
                      'bytes_reached': covered, 'starts_dropped': len(rejected),
                      'unreached_over_32_bytes': ['%08x+%d' % (a + seen[a].length, b - a - seen[a].length)
                                                  for a, b in zip(code, code[1:]) if b - a - seen[a].length > 32]}
    by = collections.defaultdict(list)
    for v, pcs in refs.items():
        name = region(v)
        if name and any(pc in seen for pc in pcs): by[name].append(v)
    report['references'] = {k: len(v) for k, v in sorted(by.items())}
    for name in ('zeroed data, low', 'zeroed data, high', 'above the zeroed data', 'vector table'):
        report[name] = ['%08x' % v for v in sorted(by.get(name, ()))]
    report['stack'] = stack(im, seen)
    text = json.dumps(report, indent=2)
    if a.out: a.out.write_text(text + '\n', encoding='utf-8')
    print(text)
    if report['failed']: raise SystemExit('FAIL: the image does not match the facts above (another OS version?)')


if __name__ == '__main__':
    main()
