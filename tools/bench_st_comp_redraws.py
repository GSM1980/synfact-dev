"""How often COMP's GR meter makes the UI recompose the screen.

Each recompose redraws the whole page (double-buffered, every visible view),
so on the COMP page the meter, not the DSP, sets the UI's CPU cost. This runs
the linked bank's audio callback 50 blocks per UI tick (1500 blocks/s at
30 ticks/s) and the real comp_tick/comp_draw, counting the invalidations per
second for a few musical signals, and the instructions of one native COMP
page draw. Instruction counts, not cycles; no device access.
"""
import argparse
import json
import math
from pathlib import Path
import random
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from test_st_comp_meter_emu import fixture
from test_st_comp_emu import set_values, block, W
from unicorn import UC_HOOK_CODE

SECONDS, TICKS, BLOCKS = 8, 30, 50


def signal(kind, n):
    """Block n (32 frames, 48 kHz) of a test signal, as (left, right)."""
    out = []
    for i in range(32):
        t = (n * 32 + i) / 48000
        beat = t % 0.5                                     # 120 BPM
        kick = math.exp(-beat * 18) * math.sin(2 * math.pi * 55 * beat)
        if kind == 'kick':
            v = kick
        elif kind == 'drums':
            hat = math.exp(-(t % 0.125) * 60) * (random.random() * 2 - 1)
            v = 0.8 * kick + 0.25 * hat
        else:                                              # sustained pad
            v = 0.5 * math.sin(2 * math.pi * 220 * t) + 0.3 * math.sin(2 * math.pi * 330 * t)
        out.append(int(v * 2 ** 30))
    return out, out


def run(core, bank, kind, scf):
    random.seed(7)
    m, view, bitmap, pixels = fixture(core, bank)
    set_values(m, [0x3000, 0x0800, 0x2000, 0x1000, 0x300, 0x7f00, scf])
    ctrl, node = W + 0xc000, W + 0xc100
    m.w(ctrl + 0x1c, node); m.w(node, ctrl + 0x14); m.w(node + 8, view); m.w(view + 0x2c, ctrl)
    m.w(0x444e2334, W + 0xb000)
    n = 0
    for _ in range(TICKS):                                 # settle one second
        for _ in range(BLOCKS):
            block(m, *signal(kind, n)); n += 1
        m.invalidations.clear(); m.call('comp_tick', ctrl)
        if m.invalidations: m.call('comp_draw', view, bitmap, 0)
    redraws = 0
    for _ in range(SECONDS * TICKS):
        for _ in range(BLOCKS):
            block(m, *signal(kind, n)); n += 1
        m.invalidations.clear(); m.call('comp_tick', ctrl)
        if m.invalidations:
            redraws += 1
            m.call('comp_draw', view, bitmap, 0)           # the recompose's overlay
    return redraws / SECONDS


def page_draw(core, bank):
    m, view, bitmap, pixels = fixture(core, bank)
    count = [0]
    h = m.u.hook_add(UC_HOOK_CODE, lambda u, p, s, x: count.__setitem__(0, count[0] + 1))
    m.call(0x40030b84, view, bitmap)
    m.call('comp_draw', view, bitmap, 0)
    m.u.hook_del(h)
    return count[0]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for key in ('core', 'bank', 'out'):
        p.add_argument('--' + key, type=Path, required=True)
    a = p.parse_args()
    draw = page_draw(a.core, a.bank)
    rows = {}
    for kind in ('kick', 'drums', 'pad'):
        for scf in (0, 0x2a00):
            rate = run(a.core, a.bank, kind, scf)
            rows['%s/scf%d' % (kind, scf >> 8)] = rate
            print('%-6s SCF %d: %.1f recomposes/s, ~%d page-draw instructions/s'
                  % (kind, scf >> 8, rate, rate * draw), flush=True)
    print('one COMP page draw (native view + overlay): %d instructions' % draw)
    a.out.write_text(json.dumps(dict(units='UI recomposes per second (30 Hz tick); page draw in guest instructions',
                                     page_draw_instructions=draw, cases=rows,
                                     excludes='other views, LCD transfer, cache misses, real hardware',
                                     device_access=False), indent=2) + '\n')
    print('PASS; no device access')


if __name__ == '__main__':
    main()
