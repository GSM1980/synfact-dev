"""The chorus's cost per 32-frame block, and its output against a reference bank.

    python tools/bench_st_chorus.py --bank BANK [--reference BEFORE] [--out REPORT.json]

Fixed scenarios (sends, returns, moving returns, silence; both SRAM windows
free, then both occupied so everything runs from the bank's DDR): 64 blocks
to settle, then 8 measured. Per event (ev_track_send, ev_delay_in,
ev_reverb_in, ev_main_mix): guest instructions, and the distinct 16-byte
lines of the bank image the block touches (code, data). With --reference the
buses must equal the reference bank's word for word. Unicorn counts
instructions as the unit runs them; its EMAC arithmetic is not trusted, so
use tests/test_st_chorus_emu.py --reference (and analog-emu) for exactness.
These are not cycles, cache misses or CPU percentages. No device access.
"""
import argparse
import json
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
import test_st_chorus_emu as emu                                   # noqa: E402
from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE  # noqa: E402

EVENTS = ('chorus_sends', 'chorus_before_delay', 'chorus_before_reverb', 'chorus_master_return')
SCENARIOS = [('0 sends, silent', dict(n=0, silent=True)), ('1 send, VOL', dict(n=1)), ('3 sends, VOL', dict(n=3)),
             ('4 sends, VOL', dict(n=4)), ('5 sends, VOL', dict(n=5)), ('12 sends, VOL', dict(n=12)),
             ('3 sends, DEL+REV+VOL', dict(n=3, returns=3)), ('12 sends, DEL+REV+VOL', dict(n=12, returns=3)),
             ('3 sends, moving returns', dict(n=3, moving=True)), ('12 sends, silent (asleep)', dict(n=12, silent=True))]


def fixture(c, n, moving=False, silent=False, returns=1, frame=0):
    values = [0x7f00, 0x2000, 0, 0x7f00, 0, 0, 0x6400]
    if returns == 3:
        values[4:] = [0x4000, 0x5000, 0x6400]
    if moving:
        values[4:] = [0x3000 + 64 * frame, 0x2000 + 64 * frame, 0x4000 + 64 * frame]
    c.write(emu.FX_RECORD, emu.settings_record(values), 'H')
    c.write(emu.FX_POINTER, [emu.FX_RECORD], 'I')
    c.write(emu.KIT_POINTER, [0], 'I')
    rng = random.Random(421 + frame)
    rows = []
    for k in range(12):
        c.write(emu.PCM[k], [0 if silent else rng.randrange(-2**28, 2**28) for _ in range(32)])
        c.write(emu.MIX_RECORDS + 142 * k + 106, [0x5000 if k < n else 0], 'H')
        rows += [0xc0000000, 0xd0000000, emu.MIX_RECORDS + 142 * k, 0]
    c.write(emu.TABLE, rows + [0], 'I')
    for bus in (emu.SEND_BUS, emu.TRACK_BUS):
        c.write(bus, [rng.choice((-2**31, 2**31 - 1)) if returns == 3 else rng.randrange(-2**29, 2**29)
                      for _ in range(64)])


def measure(bank, scenario, ddr):
    image, s = emu.load_bank(bank)
    c = emu.Cpu()
    c.map(emu.BANK_BASE, image)
    c.uc.mem_write(s['chorus_sync'], b'\x4e\x75')              # CACR is outside the numerical test
    if ddr:                                                     # both SRAM windows occupied
        c.write(0x80007ff0, [1], 'I'); c.write(0x8000ff90, [1], 'I')
    c.call(s['chorus_place']); c.call(s['chorus_dsp_reset'])
    lo, hi = emu.BANK_BASE, emu.BANK_BASE + len(image)
    code, data, armed = set(), set(), [False]

    def on_code(u, at, size, _):
        if armed[0] and lo <= at < hi:
            code.add(at // 16)

    def on_data(u, kind, at, size, value, _):
        if armed[0] and lo <= at < hi:
            data.update(range(at // 16, (at + size - 1) // 16 + 1))
    c.uc.hook_add(UC_HOOK_CODE, on_code)
    c.uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, on_data)
    counts, outputs, lines = [0] * 4, [], []
    for block in range(64 + 8):
        fixture(c, frame=block if block >= 64 else 0, **scenario)
        armed[0] = block >= 64
        code.clear(); data.clear()
        for i, name in enumerate(EVENTS):
            start = c.count
            c.call(s[name], emu.TABLE) if i == 0 else c.call(s[name])
            if block >= 64:
                counts[i] += c.count - start
                if i:
                    outputs.append(c.read(emu.TRACK_BUS if i == 3 else emu.SEND_BUS, 64))
        if block >= 64:
            lines.append((len(code), len(data)))
    return dict(instructions=[x / 8 for x in counts], total=sum(counts) / 8,
                code_lines=max(a for a, b in lines), data_lines=max(b for a, b in lines)), outputs


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--bank', type=Path, required=True)
    p.add_argument('--reference', type=Path)
    p.add_argument('--out', type=Path)
    a = p.parse_args()
    report = {}
    print('%-30s %-45s %s' % ('case', 'instructions (send delay reverb main = total)', 'DDR lines code/data'))
    for ddr in (False, True):
        for name, scenario in SCENARIOS:
            key = ('ddr ' if ddr else '') + name
            got, out = measure(a.bank, scenario, ddr)
            entry = dict(bank=got)
            text = '%.0f %.0f %.0f %.0f = %.0f' % (*got['instructions'], got['total'])
            lines = '%d/%d' % (got['code_lines'], got['data_lines'])
            if a.reference:
                ref, rout = measure(a.reference, scenario, ddr)
                assert out == rout, key + ': the buses differ from the reference'
                entry['reference'] = ref
                text = '%.0f -> %s (%.1f%%)' % (ref['total'], text, 100 * (got['total'] - ref['total']) / ref['total'])
                lines = '%d/%d -> %s' % (ref['code_lines'], ref['data_lines'], lines)
            report[key] = entry
            print('%-30s %-45s %s' % (key, text, lines), flush=True)
    if a.out:
        a.out.parent.mkdir(parents=True, exist_ok=True)
        a.out.write_text(json.dumps(report, indent=2) + '\n')
    print('PASS%s; instructions and bank lines, not cycles; no device access'
          % (': buses equal the reference' if a.reference else ''))


if __name__ == '__main__':
    main()
