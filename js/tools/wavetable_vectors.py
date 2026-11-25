# SPDX-License-Identifier: GPL-2.0-or-later
"""Writes js/test/wavetable-vectors.json: what elekloader/sdk/syntakt/wavetable.py gives for a set
of inputs, so test/wavetable.test.ts can check that src/wavetable.ts writes the same bytes and says
the same. Run from the repository root:

    python js/tools/wavetable_vectors.py
"""
import hashlib
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, ROOT)

from elekloader.sdk.syntakt import wavetable as W   # noqa: E402


def lcg(seed, n):
    """tools/vectors.py's generator (test/helpers.ts lcg)."""
    out = bytearray(n)
    s = seed & 0xFFFFFFFF
    for i in range(n):
        s = (s * 1664525 + 1013904223) & 0xFFFFFFFF
        out[i] = s >> 24
    return bytes(out)


def samples(seed, n):
    """n PCM16 values from the generator, as floats like read_wav_channels gives them."""
    raw = lcg(seed, 2 * n)
    return [int.from_bytes(raw[2 * i:2 * i + 2], 'big', signed=True) / 32768.0 for i in range(n)]


# name, seed, samples per channel, channels
CASES = [
    ('plain', 1, 2048 * 3, 1),
    ('pad_wt1024', 2, 1024 * 5 + 100, 2),
    ('strings_wt128r', 3, 128 * 70, 1),
    ('big_WT4096', 4, 4096 * 2, 1),
    ('tiny_wt64', 5, 64, 3),
    ('_wt256r', 6, 256 * 2, 1),
    ('bad_wt100', 7, 2048, 1),
    ('short', 8, 100, 1),
]


def main():
    out = []
    for stem, seed, n, count in CASES:
        channels = [samples(seed + 100 * c, n) for c in range(count)]
        case = dict(stem=stem, seed=seed, n=n, channels=count)
        try:
            name, pcm, notes = W.convert(channels, stem)
            case.update(name=name, notes=notes, bytes=len(pcm), sha256=hashlib.sha256(pcm).hexdigest())
        except ValueError as exc:
            case['error'] = str(exc)
        out.append(case)
    path = os.path.join(ROOT, 'js', 'test', 'wavetable-vectors.json')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        json.dump({'cases': out}, f, indent=1)
        f.write('\n')
    print('wrote', os.path.relpath(path, ROOT))


if __name__ == '__main__':
    main()
