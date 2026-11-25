# SPDX-License-Identifier: GPL-2.0-or-later
"""Canonical wavetables in HZFS and the pool (core 2.8), and the converter.

The layout is the one in elekloader/sdk/include/syntakt/wavetable.h and
docs/SY-WAVETABLE.md, section 4: W waves (1..64) at nine band-limited
levels, level k holding waves of 2048 >> k samples, level-major. One wave
uses 4088 samples over all levels, so a table's frames are W * 4088.

The converter reads files the way the Tonverk does (its manual, 5.2.5):
waves of 2048 samples unless the file name ends in _wt<size> (64 to 4096),
an `r` after the size for no interpolation, more than 64 waves thinned to
64 evenly spaced ones. js/src/wavetable.ts is the same converter: for the
same input it writes the same bytes, so every float operation here is in a
fixed order and uses only +, -, *, / and sqrt (no libm: cos and sin are not
correctly rounded and differ between Python and JavaScript).
"""
import re
import struct

WAVETABLE = 2      # HZFS entry flag; with STEREO (1) also set, the entry is stereo
WAVE = 2048        # samples of a level 0 wave
LEVELS = 9         # 2048, 1024, ..., 8
FRAMES = 4088      # samples of one wave over all levels
MAX_WAVES = 64
POOL_KIND = 0x81   # the pool's per-slot kind byte for a mono wavetable
SIZES = (64, 128, 256, 512, 1024, 2048, 4096)
FOLDER = 'wavetables'
SUFFIX = re.compile(r'_wt([0-9]+)(r?)$', re.IGNORECASE)


def waves(flags, frames):
    """Waves of a table with these entry flags and frames (16-bit samples), or 0."""
    if flags & 3 != WAVETABLE or not frames or frames % FRAMES:
        return 0
    w = frames // FRAMES
    return w if w <= MAX_WAVES else 0


def offset(count, level, wave):
    """First sample of a wave at a level, from the start of the PCM."""
    return count * (2 * WAVE - (2 * WAVE >> level)) + wave * (WAVE >> level)


def parse_name(stem):
    """A file name without extension -> (stored name, wave size, raw).

    `pad_wt1024` -> ('pad', 1024, False); `strings_wt128r` -> ('strings', 128,
    True); no suffix -> (stem, 2048, False)."""
    m = SUFFIX.search(stem)
    if not m:
        return stem, WAVE, False
    size = int(m.group(1))
    if size not in SIZES:
        raise ValueError(f'wave size {size} in "{stem}": use one of {", ".join(map(str, SIZES))}')
    return stem[:m.start()] or stem, size, bool(m.group(2))


def has_suffix(stem):
    return bool(SUFFIX.search(stem))


# ---------------------------------------------------------------- the FFT
def _roots():
    """(cos, sin) of 2*pi/n for n = 2, 4, ..., 4096, by half angles."""
    out = {2: (-1.0, 0.0), 4: (0.0, 1.0)}
    c, s, n = 0.0, 1.0, 4
    while n < 4096:
        c2 = (0.5 + 0.5 * c) ** 0.5          # cos(t/2) = sqrt((1 + cos t) / 2)
        s = s / (2.0 * c2)                   # sin(t/2) = sin t / (2 cos(t/2))
        c, n = c2, 2 * n
        out[n] = (c, s)
    return out


ROOTS = _roots()


def fft(re_, im):
    """In-place radix-2 DFT, X[k] = sum x[n] e^(-2 pi i k n / N), N a power of two <= 4096."""
    n = len(re_)
    j = 0
    for i in range(1, n):                    # bit reversal
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            re_[i], re_[j] = re_[j], re_[i]
            im[i], im[j] = im[j], im[i]
    m = 2
    while m <= n:
        wc, ws = ROOTS[m]
        ws = -ws                             # e^(-2 pi i / m)
        half = m >> 1
        for start in range(0, n, m):
            ur, ui = 1.0, 0.0
            for k in range(half):
                a, b = start + k, start + k + half
                tr = ur * re_[b] - ui * im[b]
                ti = ur * im[b] + ui * re_[b]
                re_[b] = re_[a] - tr
                im[b] = im[a] - ti
                re_[a] = re_[a] + tr
                im[a] = im[a] + ti
                ur, ui = ur * wc - ui * ws, ur * ws + ui * wc
        m <<= 1


def _levels(wave, raw):
    """One wave (floats, a power-of-two length) -> its nine levels as floats."""
    size = len(wave)
    if raw and size < WAVE:                  # nearest neighbour: each sample repeated
        step = WAVE // size
        wave = [wave[i // step] for i in range(WAVE)]
        size = WAVE
    re_, im = list(wave), [0.0] * size
    fft(re_, im)
    top = size // 2 - 1                      # the highest harmonic kept (no Nyquist bin)
    out = []
    for level in range(LEVELS):
        n = WAVE >> level
        h = min(top, n // 2 - 1)
        yr, yi = [0.0] * n, [0.0] * n
        for k in range(1, h + 1):            # DC removed; conj for an inverse DFT
            yr[k], yi[k] = re_[k], -im[k]
            yr[n - k], yi[n - k] = re_[k], im[k]
        fft(yr, yi)
        out.append([v / size for v in yr])
    return out


def convert(channels, stem):
    """Audio channels (lists of floats, as long_samples.read_wav_channels gives)
    and the file name without extension -> (stored name, PCM16BE body, notes).

    The rate is not used: a wave is one cycle. Raises ValueError with the numbers
    when the file holds no whole wave."""
    name, size, raw = parse_name(stem)
    if len(channels) == 1:
        mono = channels[0]
    else:
        mono = [sum(frame) / len(channels) for frame in zip(*channels)]
    count, notes = len(mono) // size, []
    if not count:
        raise ValueError(f'{len(mono)} samples: less than one wave of {size}')
    if len(mono) % size:
        notes.append(f'{len(mono) % size} samples after the last whole wave left out')
    picks = list(range(count))
    if count > MAX_WAVES:
        picks = [(i * (count - 1) + 31) // 63 for i in range(MAX_WAVES)]
        notes.append(f'{count} waves: {MAX_WAVES} evenly spaced kept')
    tables = [_levels(mono[p * size:(p + 1) * size], raw) for p in picks]
    peak = 0.0
    for t in tables:
        for level in t:
            for v in level:
                a = -v if v < 0 else v
                if a > peak:
                    peak = a
    gain = 32767.0 / peak if peak > 0 else 0.0
    values = []
    for level in range(LEVELS):
        for t in tables:
            for v in t[level]:
                x = int((v * gain + 0.5) // 1)
                values.append(-32768 if x < -32768 else 32767 if x > 32767 else x)
    return name, struct.pack('>%dh' % len(values), *values), notes


def level0(pcm_be, count):
    """The level 0 waves of a canonical body's PCM (bytes), one after the other."""
    return pcm_be[:2 * WAVE * count]
