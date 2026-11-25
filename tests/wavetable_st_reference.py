# SPDX-License-Identifier: GPL-2.0-or-later
"""SY WAVETABLE's oscillator in Python: the reference for mods/wavetable-st/engine.c.

Written from the design (docs/SY-WAVETABLE.md, section 3) with the engine's
integer steps: 32-bit wrap where C wraps, arithmetic shifts where C shifts a
signed value. tests/test_wavetable_st.py runs both on the same inputs and
requires the same samples.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'mods' / 'wavetable-st' / 'tools'))
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from gen_tables import TABLES  # noqa: E402
from elekloader.sdk.syntakt import wavetable as W  # noqa: E402

M32 = 0xffffffff
BLOCK = 32
SHAPES = 12
TEMPO_DEFAULT = 14400
INC, SHOT, LOOP, LIMIT = TABLES['inc'], TABLES['shot'], TABLES['loop'], TABLES['limit']


def increment(pitch8):
    p = max(0, min(135 * 256, pitch8))
    octave, r = divmod(p, 3072)
    i, f = r >> 4, r & 15
    v = INC[i] + (((INC[i + 1] - INC[i]) * f) >> 4)
    return (v << (octave - 8)) & M32 if octave >= 8 else v >> (8 - octave)


def lcg_samples(seed, n):
    """n signed 16-bit values from an LCG: a plain sample's PCM for the tests."""
    s, out = seed & 0xffffffff, []
    for _ in range(n):
        s = (s * 1664525 + 1013904223) & 0xffffffff
        out.append(((s >> 16) & 0xffff) - 32768)
    return out


def sample_starts(frames, stereo):
    """engine.c's wt_sample_starts: 2048-frame waves, at most 64 evenly spaced."""
    n = frames // 2048
    w = min(n, 64)
    return [((i * (n - 1) + 31) // 63 if n > 64 else i) * 2048 << (1 if stereo else 0) for i in range(w)]


def level(inc):
    for k in range(8):
        if inc <= LIMIT[k]:
            return k
    return 8


class Osc:
    def __init__(self, seed):
        self.phase = self.anim = 0
        self.seed = seed or 1
        self.fresh = 1
        self.pos = self.gain = self.held = self.frm = self.to = 0

    def rand(self):
        s = self.seed
        s ^= (s << 13) & M32
        s ^= s >> 17
        s ^= (s << 5) & M32
        self.seed = s
        return s

    def random_value(self):
        return max(-32767, (self.rand() >> 16) - 32768)

    def trig(self):
        self.phase = self.anim = 0
        self.fresh = 1
        self.held = self.random_value()
        self.frm = self.to = self.random_value()

    def anim_increment(self, p):
        spd = min(127, p['spd'] >> 8)
        if p['anim'] >> 8 < 5:      # one-shot shapes: a time
            return SHOT[spd]
        tempo = p['tempo'] if 3600 <= p['tempo'] <= 36000 else TEMPO_DEFAULT
        k = LOOP[spd * 17 >> 7]
        return (tempo * (k >> 8) + ((tempo * (k & 255)) >> 8)) & M32

    def anim_value(self, p, pcm, waves, wave):
        shape = min(SHAPES - 1, p['anim'] >> 8)
        inc = self.anim_increment(p)
        u = (self.anim + inc) & M32
        wrapped = 0
        if shape < 5:
            self.anim = M32 if u < self.anim else u
        else:
            wrapped = u < self.anim
            self.anim = u
        u = self.anim >> 17
        if shape == 0:
            v = 32767 - u
            v2 = (v * v) >> 15
            return (v2 * v2) >> 15
        if shape in (1, 5):
            return 32767 - u
        if shape in (2, 6):
            return u * 2 if u < 16384 else (32767 - u) * 2
        if shape in (3, 8):
            return u
        if shape == 4:
            v2 = (u * u) >> 15
            return (v2 * v2) >> 15
        if shape == 7:
            return 32767 if u < 16384 else 0
        if shape == 9:
            if wrapped:
                self.frm, self.to = self.to, self.random_value()
            return self.frm + (((self.to - self.frm) * u) >> 15)
        if shape == 10:
            if wrapped:
                self.held = self.random_value()
            return self.held
        if not waves:
            return 0
        if p.get('starts') is not None:
            return pcm[p['starts'][wave] + ((self.anim >> 21) << p['shift'])]
        return pcm[W.offset(waves, 3, wave) + (self.anim >> 24)]

    def block(self, p, pcm, waves, acc):
        inc = p['inc']
        if not waves:
            self.phase = (self.phase + inc * BLOCK) & M32
            self.fresh = 1
            return
        last = waves - 1
        wave = min(last, (self.pos & M32) >> 16)
        m = self.anim_value(p, pcm, waves, wave)
        clamp = lambda v: max(0, min(0x7f00, v))
        pp = clamp(p['pos'] + ((((p['apos'] - 0x4000) * 2) * m) >> 15))
        g = clamp(p['lev'] + ((((p['alev'] - 0x4000) * 2) * m) >> 15))
        w = pp * last
        pos = ((w // 32512) << 16) + ((w % 32512) * 65536 // 32512)
        gain = g
        if self.fresh:
            self.pos, self.gain, self.fresh = pos, gain, 0
        dpos, dgain = (pos - self.pos) >> 5, (gain - self.gain) >> 5
        pos, self.pos = self.pos, pos
        gain, self.gain = self.gain, gain
        if not gain and not dgain:
            self.phase = (self.phase + inc * BLOCK) & M32
            return
        starts = p.get('starts')
        if starts is not None:                        # a plain sample: 2048-frame waves, one level
            shift, size, base, sh = 21, 2048, 0, p['shift']
        else:
            k = level(inc)
            shift, size, base, sh = 21 + k, W.WAVE >> k, W.offset(waves, k, 0), 0
        ph = self.phase
        for i in range(BLOCK):
            pos += dpos
            gain += dgain
            if pos <= 0:
                wave, xf = 0, 0
            else:
                wave, xf = pos >> 16, (pos >> 1) & 0x7fff
                if wave >= last:
                    wave, xf = last, 0
            ph = (ph + inc) & M32
            idx, fr = ph >> shift, (ph >> (shift - 15)) & 0x7fff
            w0 = starts[wave] if starts is not None else base + wave * size
            a, b = pcm[w0 + (idx << sh)], pcm[w0 + (((idx + 1) & (size - 1)) << sh)]
            s0 = a + (((b - a) * fr) >> 15)
            if xf:
                w1 = starts[wave + 1] if starts is not None else w0 + size
                a, b = pcm[w1 + (idx << sh)], pcm[w1 + (((idx + 1) & (size - 1)) << sh)]
                s1 = a + (((b - a) * fr) >> 15)
                s0 += ((s1 - s0) * xf) >> 15
            acc[i] += s0 * max(0, gain)
        self.phase = ph
