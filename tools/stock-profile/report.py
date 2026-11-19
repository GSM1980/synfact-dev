#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read an exact MAIN profile written by analog-emu's dtboot with
analog-emu-xprof.patch applied (XPROF_OUT=FILE, optionally XPROF_CACHE=1).

  report.py rank  PROFILE [N]          functions by modeled self cycles
  report.py tree  PROFILE [DEPTH]      inclusive tree under the audio block
  report.py lines PROFILE [OUTDIR]     I-cache line misses; writes lock/free sets
  report.py ann   PROFILE DIS START [END]  per-block counts on an objdump listing

Model (measured on the unit for AUDIO-side code, docs/STOCK-MAIN-CPU-PROFILE.md):
cycles ~ 0.85 x instructions + 40 x cache misses; 8 KB I and 8 KB D cache,
4-way, 16-byte lines; one audio block = 32 frames at 48 kHz = 166,667 cycles
at 250 MHz. These are emulator estimates, not CPU meter readings.
"""
import bisect
import collections
import sys

AUDIO_BLOCK = 0x400A38CE  # OS 1.42 MAIN audio block routine (an interrupt handler)
BLOCK_CYCLES = 250e6 / 1500


def load(path):
    p = collections.defaultdict(dict)
    p['edge'] = collections.defaultdict(dict)
    calls = collections.Counter()
    for line in open(path):
        f = line.split()
        k = f[0]
        if k in ('total', 'overflow'):
            p[k] = int(f[1])
        elif k == 'misses':
            p[k] = (int(f[1]), int(f[2]))
        elif k == 'calls':
            calls[int(f[1], 16) & ~1] += int(f[2])
        elif k == 'edge':
            p['edge'][int(f[1], 16) & ~1][int(f[2], 16) & ~1] = int(f[3])
        elif k in ('selfm', 'inclm'):
            a = int(f[1], 16) & (~1 if k == 'inclm' else ~0)
            p[k][a] = (int(f[2]), int(f[3]))
        elif k in ('self', 'incl', 'iline', 'tcb'):
            a = int(f[1], 16) & (~1 if k == 'incl' else ~0)
            p[k][a] = int(f[2])
    p['calls'] = calls
    p['blocks'] = calls[AUDIO_BLOCK] or 1
    return p


def cycles(instr, im, dm):
    return 0.85 * instr + 40 * (im + dm)


def rank(p, n=40):
    ents = sorted(p['calls'])
    fn = lambda pc: ents[max(bisect.bisect_right(ents, pc) - 1, 0)]
    ins, im, dm = collections.Counter(), collections.Counter(), collections.Counter()
    for pc, c in p['self'].items():
        ins[fn(pc)] += c
    for pc, (i, d) in p.get('selfm', {}).items():
        im[fn(pc)] += i
        dm[fn(pc)] += d
    b = p['blocks']
    cy = {k: cycles(ins[k], im[k], dm[k]) for k in set(ins) | set(im)}
    tot = sum(cy.values())
    print(f"audio blocks {b}; all MAIN work {tot / b:.0f} modeled cycles/block "
          f"= {100 * tot / b / BLOCK_CYCLES:.1f}% of 250 MHz")
    print("function  cyc/blk   %MAIN  instr/blk imiss/blk dmiss/blk   cumulative")
    acc = 0
    for k, c in sorted(cy.items(), key=lambda x: -x[1])[:n]:
        acc += c
        print(f"{k:08x} {c / b:8.0f} {100 * c / b / BLOCK_CYCLES:6.2f}%  {ins[k] / b:8.0f} "
              f"{im[k] / b:8.1f} {dm[k] / b:8.1f}   {100 * acc / b / BLOCK_CYCLES:5.1f}%")


def tree(p, depth=3):
    b = p['blocks']
    incl, inclm, calls, kids = p['incl'], p.get('inclm', {}), p['calls'], p['edge']
    cy = lambda k: cycles(incl.get(k, 0), *inclm.get(k, (0, 0)))
    root = AUDIO_BLOCK
    R = cy(root) or 1
    i, d = inclm.get(root, (0, 0))
    print(f"{root:08x}: {incl.get(root, 0) / b:.0f} instr, {i / b:.0f} I-miss, {d / b:.0f} D-miss "
          f"per block; {R / b:.0f} cycles = {100 * R / b / BLOCK_CYCLES:.1f}% of 250 MHz")
    print("function   %block  instr/blk imiss/blk dmiss/blk calls/blk")

    def show(f, lvl, seen):
        if lvl > depth:
            return
        for k, n in sorted(kids.get(f, {}).items(), key=lambda kv: -cy(kv[0])):
            if cy(k) / R < 0.004 or k in seen:
                continue
            i, d = inclm.get(k, (0, 0))
            print("  " * lvl + f"{k:08x} {100 * cy(k) / R:5.1f}%  {incl.get(k, 0) / b:7.0f} "
                  f"{i / b:7.0f} {d / b:7.0f}  {n / b:5.1f}")
            show(k, lvl + 1, seen | {k})
    show(root, 1, {root})


def lines(p, outdir=None):
    il, b = p.get('iline', {}), p['blocks']
    print(f"I-miss/block {sum(il.values()) / b:.0f}, D-miss/block "
          f"{p.get('misses', (0, 0))[1] / b:.0f}, distinct code lines missed {len(il)}")
    srt = sorted(il.items(), key=lambda x: -x[1])
    for th in (0.9, 0.5, 0.1):
        hot = [n / b for _, n in srt if n / b >= th]
        print(f"lines missed >= {th} per block: {len(hot)} ({16 * len(hot)} bytes), "
              f"{sum(hot):.0f} misses/block")
    sets = collections.defaultdict(list)
    for l, n in srt:
        sets[l & 127].append((n, l))
    lock = [l for v in sets.values() for _, l in sorted(v, reverse=True)[:2]]
    print(f"half-lock set: {len(lock)} lines, {sum(il[l] for l in lock) / b:.0f} misses/block before")
    if outdir:
        open(f"{outdir}/ilock.txt", 'w').write("\n".join(f"{l:07x}" for l in lock))
        for kb in (2, 4, 8):
            top = [l for l, _ in srt[:kb * 64]]
            open(f"{outdir}/ifree{kb}.txt", 'w').write("\n".join(f"{l:07x}" for l in top))


def ann(p, dis, start, end=None):
    ents = sorted(p['calls'])
    if end is None:
        i = bisect.bisect_right(ents, start)
        end = ents[i] if i < len(ents) else start + 0x400
    b, tot = p['blocks'], p.get('total', 1)
    print(f"; {start:08x}..{end:08x} (instructions per audio block, from each block start)")
    for line in open(dis):
        if len(line) < 9 or line[8] != ':':
            continue
        try:
            a = int(line[:8], 16)
        except ValueError:
            continue
        if a < start:
            continue
        if a >= end:
            break
        n = p['self'].get(a)
        tag = f"{n / b:8.0f}/blk {100 * n / tot:5.2f}%" if n else " " * 20
        print(f"{tag} {line.rstrip()}")


def main(a):
    if len(a) < 2:
        sys.exit(__doc__)
    p = load(a[1])
    if a[0] == 'rank':
        rank(p, int(a[2]) if len(a) > 2 else 40)
    elif a[0] == 'tree':
        tree(p, int(a[2]) if len(a) > 2 else 3)
    elif a[0] == 'lines':
        lines(p, a[2] if len(a) > 2 else None)
    elif a[0] == 'ann':
        ann(p, a[2], int(a[3], 16), int(a[4], 16) if len(a) > 4 else None)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
