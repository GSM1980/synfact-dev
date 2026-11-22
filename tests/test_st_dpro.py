# SPDX-License-Identifier: AGPL-3.0-or-later
"""DPRO-WAVE/DDRW/DENS fixed-point port versus pinned De-Pattern's float32 DSL.

Needs the owner's Monomachine OS 1.32B (--os): the tables are made from it and
never stored. Checks the tables header against tables.json, the integer
increments against upstream exact.py word for word, then 16-frame blocks of
the C port against the DSL at >= 60 dB. --os-rate 2 also checks the
Monomachine's own DDRW/DENS rate (DM_DPRO_OS=2). No device access.
"""
import argparse
import ctypes
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DP = ROOT / 'mods/digimono-st/dpro'
sys.path.insert(0, str(DP))
import reference as R  # noqa: E402

DEFAULTS = {12: [0, 0, 0, 96, 0, 0, 0], 13: [0, 64, 0, 0, 0, 0, 0], 14: [63, 63, 63, 0, 0, 0, 0]}
NAMES = {12: 'DPRO-WAVE', 13: 'DPRO-DDRW', 14: 'DPRO-DENS'}


class Port:
    def __init__(self, lib):
        self.lib = lib
        self.state = ctypes.create_string_buffer(lib.voice_bytes())
        lib.dm_dp_init(self.state)

    def incs(self):
        return list((ctypes.c_uint32 * 7).from_buffer(self.state, self.lib.inc_offset()))


def build(cc, out, rate):
    out.mkdir(parents=True, exist_ok=True)
    wrapper = out / 'size.c'
    wrapper.write_text('#include <stddef.h>\n#include "dpro/voice.h"\n'
                       'unsigned voice_bytes(void){return sizeof(struct dm_dp_voice);}\n'
                       'unsigned inc_offset(void){return offsetof(struct dm_dp_voice,inc);}\n')
    dll = out / f'dpro-os{rate}.dll'
    subprocess.run([str(cc), '-O2', '-Wall', '-Werror', f'-DDM_DPRO_OS={rate}', '-I', 'mods/digimono-st',
                    '-shared', 'mods/digimono-st/dpro.c', str(wrapper), '-o', str(dll)], cwd=ROOT, check=True)
    lib = ctypes.CDLL(str(dll))
    lib.voice_bytes.restype = lib.inc_offset.restype = ctypes.c_uint
    U16 = ctypes.POINTER(ctypes.c_uint16)
    lib.dm_dp_increments.argtypes = [ctypes.c_void_p, ctypes.c_uint, U16, ctypes.c_int32, ctypes.c_int32]
    lib.dm_dp_half.argtypes = [ctypes.c_void_p, ctypes.c_uint, U16, ctypes.c_void_p]
    lib.dm_dp_trig.argtypes = [ctypes.c_void_p, ctypes.c_uint, U16]
    lib.dm_dp_word.argtypes = [ctypes.c_uint, ctypes.c_int32]
    lib.dm_dp_word.restype = ctypes.c_int32
    return lib


def expected_incs(m, kind, F, knobs, pre):
    A = m.X.increment(F)
    if kind == 12:
        return [m.X.wave_inc(A, 21)], m.X.wave_sync(knobs, pre)
    if kind == 14:
        d = m.X.dens_incs(A, knobs)
        return [d['i1'], d['i2'], d['i3'], d['i4']], d['i5']
    d = m.X.ddrw_incs(A, knobs)
    return [d['i1'], d['i2'], d['i3'], d['i4']], None


def cases(kind):
    base = DEFAULTS[kind]
    out = [('base', base, 60 * 64, None)]
    for note in (12, 24, 36, 48, 69, 84, 96, 108):
        out.append((f'note-{note}', base, note * 64, None))
    for k in range(7):
        if kind == 14 and k == 4:
            continue
        for value in (0, 1, 31, 32, 63, 64, 65, 96, 127):
            patch = list(base); patch[k] = value
            out.append((f'p{k}-{value}', patch, 60 * 64, None))
    # fractional controls, and a control change in the middle of a note
    patch = list(base); patch[1] = 37
    out.append(('fraction', [x * 256 + 77 for x in patch], 57 * 64 + 13, 'raw'))
    if kind == 12:
        out.append(('sync-sfrq', [3, 40, 20, 96, 60, 70, 0], 48 * 64, None))
        out.append(('sync-prch', [9, 0, 0, 0, 100, 0, 0], 60 * 64, None))
    if kind == 13:
        out.append(('wave-change', [5, 64, 9, 40, 0, 30, 0], 60 * 64, 'change'))
        out.append(('crush', [7, 100, 22, 0, 110, 80, 127], 48 * 64, None))
    if kind == 14:
        out.append(('chord', [70, 75, 51, 33, 0, 0, 0], 48 * 64, None))
    return out


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc', type=Path, required=True, help='x86_64 host clang')
    p.add_argument('--os', type=Path, required=True, help='Monomachine OS 1.32B .syx')
    p.add_argument('--out', type=Path, default=Path('out/dpro-tests'))
    p.add_argument('--os-rate', type=int, choices=(1, 2), default=1)
    p.add_argument('--blocks', type=int, default=24)
    a = p.parse_args()
    out = a.out.resolve()
    info = json.loads((DP / 'tables.json').read_text())
    header = (DP / 'tables.h').read_bytes()
    assert hashlib.sha256(header).hexdigest() == info['tables_sha256'], 'tables.h differs: run import_tables.py'
    assert hashlib.sha256(a.os.read_bytes()).hexdigest() == info['os_sha256']
    print('PASS: tables.h matches tables.json', info['tables_sha256'][:12], flush=True)
    lib = build(a.cc, out, a.os_rate)
    m = R.load(str(a.os), a.os_rate)
    bank = R.digibank(m, DP / 'out' / f'digibank-{info["os_sha256"][:12]}-'
                      f'{hashlib.sha256((DP / "upstream/dpro.py").read_bytes()).hexdigest()[:12]}.bin')
    rows = []
    # pitch words and increments, against exact.py
    for kind in (12, 13, 14):
        for pitch in (-64, 0, 1, 2, 3, 100, 60 * 64, 60 * 64 + 1, 127 * 64, 140 * 64, 150 * 64):
            pc = min(140 * 64, max(0, pitch))
            want = min(0x5800, pc * 8 // 3 + 341 + (2048 if kind == 12 else 0))
            assert lib.dm_dp_word(kind, pitch) == want, (kind, pitch)
        for F in list(range(0, 0x5801, 97)) + [0x5800]:
            for knobs in ([0] * 8, [127] * 8, [63, 64, 1, 96, 64, 127, 33, 64], [17, 99, 45, 2, 43, 86, 120, 64],
                          [61, 62, 66, 0, 85, 1, 0, 64], [0, 0, 127, 127, 86, 0, 0, 64]):
                for pre in (0, F, 0x1234):
                    c = (ctypes.c_uint16 * 8)(*[k << 8 for k in knobs])
                    v = Port(lib)
                    lib.dm_dp_increments(v.state, kind, c, F, pre)
                    got = v.incs()
                    incs, extra = expected_incs(m, kind, F, knobs, pre)
                    if kind == 13:
                        assert got[:2] == incs[:2] and got[4:6] == incs[2:4], (kind, F, knobs, got, incs)
                    else:
                        assert got[:len(incs)] == incs, (kind, F, knobs, got, incs)
                        assert got[6] == extra, (kind, F, knobs, got[6], extra)
    print('PASS: pitch words and integer increments equal exact.py', flush=True)
    for kind in (12, 13, 14):
        worst = 200.0
        for label, patch, pitch, mode in cases(kind):
            controls = list(patch) if mode == 'raw' else [x << 8 for x in patch]
            controls = controls + [64 << 8]
            ref = R.Voice(str(a.os), kind, a.os_rate, bank)
            port = Port(lib)
            F = lib.dm_dp_word(kind, pitch)
            pre = lib.dm_dp_word(13, 48 * 64)
            error = power = 0.0
            for block in range(a.blocks):
                c = list(controls)
                if mode == 'change' and block >= a.blocks // 3:
                    c[0] = 21 << 8
                if mode == 'change' and block >= 2 * a.blocks // 3:
                    c[2] = 50 << 8
                rc, glide = (R.ddrw_host(m, c) if kind == 13 else (c, None))
                expected = ref.block(F, rc, block == 0, pre, glide)
                arr = (ctypes.c_uint16 * 8)(*c)
                if block == 0:
                    lib.dm_dp_trig(port.state, kind, arr)
                lib.dm_dp_increments(port.state, kind, arr, F, pre)
                pcm = (ctypes.c_int32 * 16)()
                lib.dm_dp_half(port.state, kind, arr, pcm)
                for x, y in zip(pcm, expected):
                    sample = x / 2 ** 26
                    assert math.isfinite(y) and abs(sample) <= 1.0
                    error += (sample - y) ** 2; power += y * y
            snr = 10 * math.log10(max(power, 1e-30) / max(error, 1e-30))
            quiet = power / (16 * a.blocks) < 1e-12
            ok = snr >= 60 or (quiet and error / (16 * a.blocks) < 1e-12)
            rows.append(dict(type=kind, case=label, snr_db=round(snr, 2), quiet=quiet, ok=ok))
            if not quiet:
                worst = min(worst, snr)
            if not ok:
                print('FAIL', NAMES[kind], label, f'{snr:.1f} dB', flush=True)
        print(f'{NAMES[kind]}: {sum(r["type"] == kind for r in rows)} cases, worst {worst:.1f} dB', flush=True)
    (out / f'results-os{a.os_rate}.json').write_text(json.dumps(dict(rows=rows, os_rate=a.os_rate,
                                                                    hardware_tested=False), indent=2) + '\n')
    bad = [r for r in rows if not r['ok']]
    assert not bad, bad
    print('PASS:', len(rows), 'DigiPRO DSL cases at >= 60 dB; no device access')


if __name__ == '__main__':
    main()
