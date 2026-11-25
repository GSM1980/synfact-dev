# SPDX-License-Identifier: GPL-2.0-or-later
"""SY WAVETABLE (mods/wavetable-st): the engine built for the host against its
Python reference (tests/wavetable_st_reference.py), the generated tables, the
descriptor's shape and the sound's basic behaviour. No device, no firmware.

    python tests/test_wavetable_st.py --cc "$LLVM_MINGW_BIN/i686-w64-mingw32-clang.exe" --out out/wavetable-st-tests
"""
import argparse
import math
import random
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tests'))
sys.path.insert(0, str(ROOT / 'mods' / 'wavetable-st' / 'tools'))
import wavetable_st_reference as R  # noqa: E402
import gen_tables  # noqa: E402
from elekloader.sdk.syntakt import wavetable as W  # noqa: E402

FIELDS = ('pos', 'lev', 'spd', 'alev', 'apos', 'anim', 'tempo')


def table(waves, seed):
    """A canonical table from saw-to-square waves (the real converter)."""
    rnd = random.Random(seed)
    src = []
    for w in range(waves):
        mix = w / max(1, waves - 1)
        src += [(1 - mix) * (2 * i / 2048 - 1) + mix * (1 if i < 1024 else -1) + 0.05 * rnd.uniform(-1, 1)
                for i in range(2048)]
    _, body, _ = W.convert([src], 'test')
    return list(struct.unpack('>%dh' % (len(body) // 2), body))


def scenario(seed, mode=0):
    """mode 0: a canonical table of 1..64 waves; 1, 2: a plain mono or stereo
    sample played as a wavetable (more than 64 waves, a few, less than one)."""
    rnd = random.Random(seed)
    if mode:
        size = rnd.choice([2048 * 100 + 77, 2048 * 5 + 3, 2048, 1500])     # frames
        raw = R.lcg_samples(seed, size * mode)
        pcm, waves = raw, size
    else:
        waves = rnd.choice([1, 2, 5, 64])
        pcm = table(waves, seed)
    blocks = []
    p = dict(pos=0, lev=0x6400, spd=0x4000, alev=0x4000, apos=0x4000, anim=0x0600, tempo=14400)
    pitch = 60 * 256
    for b in range(400):
        trig = b == 0 or rnd.random() < 0.03
        if rnd.random() < 0.15:
            key = rnd.choice(FIELDS + ('pitch', 'table'))
            if key == 'pitch':
                pitch = rnd.randint(-600, 37000)
            elif key == 'anim':
                p['anim'] = rnd.randint(0, 11) << 8
            elif key == 'tempo':
                p['tempo'] = rnd.choice([0, 3600, 14400, 36000, 40000, rnd.randint(3600, 36000)])
            elif key != 'table':
                p[key] = rnd.choice([0, 0x4000, 0x7f00, rnd.randint(0, 0x7f00)])
        present = 0 if (b // 50) % 4 == 3 else 1     # the table goes away for a while
        blocks.append((int(trig), pitch, dict(p), present))
    return waves, pcm, 0x9e3779b9 * (seed + 1) & 0xffffffff, blocks


def run_c(exe, out, waves, pcm, seed, blocks, mode=0):
    words = [mode, waves] + [v & 0xffffffff for v in pcm] + [seed, len(blocks)]
    for trig, pitch, p, present in blocks:
        words += [trig, pitch & 0xffffffff] + [p[k] for k in FIELDS] + [present]
    src, dst = out / 'scenario.bin', out / 'result.bin'
    src.write_bytes(struct.pack('<%dI' % len(words), *words))
    subprocess.run([str(exe), str(src), str(dst)], check=True)
    return dst.read_bytes()


def run_python(waves, pcm, seed, blocks, mode=0):
    o = R.Osc(seed)
    out = bytearray()
    starts = R.sample_starts(waves, mode == 2) if mode else None
    if mode:
        waves = len(starts)
    out += struct.pack('<I', waves) + struct.pack('<%dI' % len(starts or []), *(starts or []))
    for trig, pitch, p, present in blocks:
        q = dict(p, inc=R.increment(pitch), starts=starts, shift=1 if mode == 2 else 0)
        if trig:
            o.trig()
        acc = [0] * 32
        o.block(q, pcm, waves if present else 0, acc)
        out += struct.pack('<32i', *acc)
        out += struct.pack('<IIii', o.phase, o.anim, o.pos, o.gain)
    for i in range(0, 36001, 7):
        v = R.increment(i - 512)
        out += struct.pack('<II', v, R.level(v))
    return bytes(out)


def check_tables():
    text = (ROOT / 'mods/wavetable-st/tables.h').read_text(encoding='utf-8')
    assert text.replace('\r\n', '\n') == gen_tables.header(), 'tables.h differs from tools/gen_tables.py'
    t = gen_tables.TABLES
    assert all(0 < v < 2 ** 32 for k in t for v in t[k])
    # Pitch: A4 (MIDI 69) is 440 Hz within a cent; octaves double.
    hz = lambda inc: inc * 48000 / 2 ** 32
    assert abs(1200 * math.log2(hz(R.increment(69 * 256)) / 440)) < 1
    assert abs(R.increment(81 * 256) / R.increment(69 * 256) - 2) < 1e-4
    # Every level's top harmonic stays under 28 kHz.
    for p8 in range(0, 135 * 256, 64):
        inc = R.increment(p8)
        k = R.level(inc)
        if k < 8:
            assert hz(inc) * ((1024 >> k) - 1) <= 28000.5, (p8, k)


def check_descriptor():
    src = (ROOT / 'mods/wavetable-st/main.c').read_text(encoding='utf-8')
    assert re.search(r'wt_machine=\{72,17,"SY WAVETABLE","WTBL","SYN"', src)
    assert 'ST_MACHINE_TEMPO_PARAMETER' in src
    labels = re.findall(r'P\("([^"]+)"', src)
    assert labels[:7] == ['LEV', 'POS', 'SLOT', 'SPD', 'A.LEV', 'A.POS', 'ANIM'], labels
    # audio.c's default extended words follow the descriptor's initial values.
    audio = (ROOT / 'mods/wavetable-st/audio.c').read_text(encoding='utf-8')
    assert '{0x4000,0,0,0x4000,0x4000,0x4000,0x4000,0,WT_TEMPO_DEFAULT}' in audio


def check_sound():
    """Behaviour the design promises, on the reference."""
    pcm = table(2, 7)
    base = dict(pos=0, lev=0x7f00, spd=0x4000, alev=0x4000, apos=0x4000, anim=0x0600, tempo=14400, inc=R.increment(57 * 256))
    o = R.Osc(1)
    o.trig()
    acc = [0] * 32
    o.block(base, pcm, 2, acc)
    assert max(map(abs, acc)) <= 2 ** 30 and max(map(abs, acc)) > 2 ** 29   # one osc at full level: about -6 dB
    # Position 127 plays the last wave; 0 the first: different sounds.
    def render(**kw):
        o = R.Osc(1)
        o.trig()
        out = []
        for _ in range(8):
            acc = [0] * 32
            o.block(dict(base, **kw), pcm, 2, acc)
            out += acc
        return out
    assert render(pos=0) != render(pos=0x7f00)
    assert set(render(lev=0)) == {0}
    # A looping ANIM on position moves the sound; at 0 depth it does not.
    assert render(apos=0x7f00, anim=0x0800, spd=0) != render(apos=0x4000, anim=0x0800, spd=0)
    # A loop of one beat at 120 BPM takes 0.5 s: about 750 blocks.
    o = R.Osc(1)
    o.trig()
    p = dict(base, anim=0x0800, spd=round(7 * 128 / 17 + 1) << 8)
    assert R.LOOP[(p['spd'] >> 8) * 17 >> 7] == gen_tables.TABLES['loop'][7]
    blocks = 0
    while True:
        before = o.anim
        o.anim_value(p, pcm, 2, 0)
        blocks += 1
        if o.anim < before:
            break
    assert abs(blocks - 750) <= 1, blocks
    # A one-shot ramp at SPD 0 ends in 2 ms (3 blocks) and holds.
    o = R.Osc(1)
    o.trig()
    vals = [o.anim_value(dict(base, anim=0x0300, spd=0), pcm, 2, 0) for _ in range(6)]
    assert vals[-1] == vals[-2] == 32767, vals


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cc', required=True)
    ap.add_argument('--out', required=True)
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    check_tables()
    check_descriptor()
    check_sound()
    exe = out / 'wavetable-host.exe'
    subprocess.run([a.cc, '-O2', '-Wall', '-Werror', '-I', str(ROOT / 'elekloader/sdk/include'),
                    str(ROOT / 'tests/wavetable_st_host.c'), str(ROOT / 'mods/wavetable-st/engine.c'), '-o', str(exe)],
                   check=True)
    for seed, mode in [(s, 0) for s in range(12)] + [(s, 1 + s % 2) for s in range(12, 20)]:
        waves, pcm, s, blocks = scenario(seed, mode)
        got = run_c(exe, out, waves, pcm, s, blocks, mode)
        want = run_python(waves, pcm, s, blocks, mode)
        if got != want:
            at = next(i for i in range(min(len(got), len(want))) if got[i] != want[i])
            raise AssertionError(f'seed {seed}: C and reference differ at byte {at} (block {at // 144})')
    # A plain sample's waves: 64 of 100 evenly spaced (first and last), all of 5, none under 2048.
    assert R.sample_starts(2048 * 100 + 77, False)[:2] == [0, 2048 * 2] and R.sample_starts(2048 * 100 + 77, False)[-1] == 99 * 2048
    assert R.sample_starts(2048 * 5 + 3, True) == [i * 4096 for i in range(5)] and R.sample_starts(1500, False) == []
    print('PASS: SY WAVETABLE engine equals its Python reference (12 table and 8 plain-sample scenarios x 400 blocks, '
          'mono and stereo samples, tables, pitch, levels); '
          'tables.h generated; descriptor; level, position, ANIM loop/one-shot timing')


if __name__ == '__main__':
    main()
