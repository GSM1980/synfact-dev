# SPDX-License-Identifier: GPL-2.0-or-later
"""The wavetable converter (elekloader/sdk/syntakt/wavetable.py, core 2.8) and its
long_samples put/get. Offline: an in-memory device, never MIDI."""
from pathlib import Path
import cmath
import math
import struct
import sys
import tempfile
import unittest
import wave
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from elekloader.sdk.syntakt import data_rpc as R, long_samples as H, wavetable as W  # noqa: E402
from elekloader.sdk.syntakt.sample_catalog import enable_folders  # noqa: E402
from test_st_storage import Device  # noqa: E402


def pcm(body):
    return struct.unpack('>%dh' % (len(body) // 2), body)


def harmonics(wave_, top):
    """|X[h]| / (N/2) for h = 0..top of one wave (naive DFT, the test's own)."""
    n = len(wave_)
    return [abs(sum(v * cmath.exp(-2j * math.pi * h * i / n) for i, v in enumerate(wave_))) / (n / 2)
            for h in range(top + 1)]


def write_wav(path, values, channels=1, rate=44100):
    with wave.open(str(path), 'wb') as f:
        f.setparams((channels, 2, rate, 0, 'NONE', 'not compressed'))
        f.writeframes(struct.pack('<%dh' % len(values), *values))


class Converter(unittest.TestCase):
    def test_names(self):
        self.assertEqual(W.parse_name('pad_wt1024'), ('pad', 1024, False))
        self.assertEqual(W.parse_name('strings_wt128r'), ('strings', 128, True))
        self.assertEqual(W.parse_name('Lead_WT64R'), ('Lead', 64, True))
        self.assertEqual(W.parse_name('_wt256'), ('_wt256', 256, False))     # nothing left: keep the stem
        self.assertEqual(W.parse_name('plain'), ('plain', 2048, False))
        self.assertEqual(W.parse_name('wt1024'), ('wt1024', 2048, False))   # no underscore: no suffix
        with self.assertRaises(ValueError) as e:
            W.parse_name('x_wt100')
        self.assertEqual(str(e.exception), 'wave size 100 in "x_wt100": use one of 64, 128, 256, 512, 1024, 2048, 4096')

    def test_layout_and_band_limits(self):
        # Two saw waves of 2048 (harmonics 1/h up to 1023): every level keeps
        # exactly the harmonics below its own half length, DC goes, and the
        # loudest sample of the table is full scale.
        saw = [2 * (i / 2048) - 1 for i in range(2048)]
        name, body, notes = W.convert([saw + [0.5 * v for v in saw]], 'saw')
        self.assertEqual((name, notes, len(body)), ('saw', [], 2 * 2 * W.FRAMES))
        v = pcm(body)
        self.assertEqual(max(map(abs, v)), 32767)
        for level in (3, 6, 8):
            n = W.WAVE >> level
            a = v[W.offset(2, level, 0):W.offset(2, level, 0) + n]
            b = v[W.offset(2, level, 1):W.offset(2, level, 1) + n]
            ha, hb = harmonics(a, n // 2), harmonics(b, n // 2)
            self.assertLess(ha[0], 1e-3 * ha[1])                          # no DC
            self.assertLess(ha[n // 2], 1e-3 * ha[1])                     # no Nyquist bin
            for h in range(1, n // 2):                                     # 1/h, the second wave half as loud
                self.assertAlmostEqual(ha[h] / ha[1], 1 / h, delta=2e-3)
                self.assertAlmostEqual(hb[h] / ha[h], 0.5, delta=2e-3 * h)

    def test_short_waves_and_raw(self):
        # A 64-sample square: interpolated (band-limited, harmonics < 32) or
        # raw (each sample repeated 32 times, so level 0 keeps the steps).
        square = [1.0 if i < 32 else -1.0 for i in range(64)]
        _, smooth, _ = W.convert([square], 'sq_wt64')
        _, raw, _ = W.convert([square], 'sq_wt64r')
        s, r = pcm(smooth)[:2048], pcm(raw)[:2048]
        hs, hr = harmonics(s, 40), harmonics(r, 40)
        self.assertLess(max(hs[32:]), 1e-3 * hs[1])                        # nothing from 32 up
        self.assertGreater(hr[33], 0.01 * hr[1])                           # raw keeps the steps' harmonics
        self.assertNotEqual(s, r)

    def test_more_than_64_waves_and_remainders(self):
        # 100 waves, each a constant-amplitude sine scaled by its index: 64
        # evenly spaced ones are kept, the first and the last included.
        table = [math.sin(2 * math.pi * i / 2048) * (w + 1) / 100 for w in range(100) for i in range(2048)]
        _, body, notes = W.convert([table + [0.0] * 7], 'many')
        self.assertEqual(notes, ['7 samples after the last whole wave left out', '100 waves: 64 evenly spaced kept'])
        v = pcm(body)
        peaks = [max(v[w * 2048:(w + 1) * 2048]) for w in range(64)]
        self.assertEqual(peaks[-1], 32767)
        expected = [((i * 99 + 31) // 63 + 1) / 100 for i in range(64)]
        for p, e in zip(peaks, expected):
            self.assertAlmostEqual(p / 32767, e, delta=2e-4)
        with self.assertRaises(ValueError) as e:
            W.convert([[0.0] * 100], 'short')
        self.assertEqual(str(e.exception), '100 samples: less than one wave of 2048')

    def test_channels_mix_and_silence(self):
        a = [math.sin(2 * math.pi * i / 2048) for i in range(2048)]
        _, mixed, _ = W.convert([a, [-x for x in a]], 'cancel')             # L + R = 0: silence
        self.assertEqual(set(pcm(mixed)), {0})
        _, one, _ = W.convert([a], 'one')
        _, two, _ = W.convert([a, a], 'two')
        self.assertEqual(one, two)


class Transfer(unittest.TestCase):
    def setUp(self):
        self.dev = Device()
        self.store = H.Store(R.DataClient(self.dev, block=8192), log=lambda *_: None)

    def test_put_get_and_folder(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            src = tmp / 'lead_wt1024.wav'
            write_wav(src, [int(20000 * math.sin(2 * math.pi * 3 * i / 1024)) for i in range(1024 * 3)])
            # A wavetable needs the folder `wavetables`, so catalogue v2.
            with self.assertRaises(ValueError) as e:
                H.cmd_put(self.store, src)
            self.assertEqual(str(e.exception), 'Enable folders first: wavetables go in the folder wavetables')
            enable_folders(self.store, tmp / 'backup', firmware_confirmed=True)
            e1 = H.cmd_put(self.store, src)
            index, _ = self.store.current()
            self.assertEqual((e1['name'], e1['flags'], e1['frames']), ('lead', W.WAVETABLE, 3 * W.FRAMES))
            self.assertEqual(index.path(e1['folder']), 'wavetables')
            self.assertEqual((H.kind(e1), W.waves(e1['flags'], e1['frames'])), ('wt', 3))
            # A second one reuses the folder; --wavetable on a plain name; a plain sample stays plain.
            plain = tmp / 'serum.wav'
            write_wav(plain, [int(10000 * f(2 * math.pi * i / 2048)) for i in range(2048) for f in (math.sin, math.cos)],
                      channels=2)
            e2 = H.cmd_put(self.store, plain, table=True)
            e3 = H.cmd_put(self.store, plain, name='plain')
            index, _ = self.store.current()
            self.assertEqual([d['name'] for d in index.folders], ['wavetables'])
            self.assertEqual((e2['folder'], e2['frames'], e2['name']), (e1['folder'], W.FRAMES, 'serum'))
            self.assertEqual((e3['flags'], e3.get('folder', 0)), (H.STEREO, 0))
            self.assertEqual(H.cmd_fsck(self.store, True), [])
            # get writes the 2048-sample waves; --raw the whole stored body.
            back = tmp / 'back.wav'
            body = H.cmd_get(self.store, str(e1['id']), back)
            with wave.open(str(back), 'rb') as f:
                self.assertEqual((f.getnchannels(), f.getnframes()), (1, 3 * W.WAVE))
                self.assertEqual(f.readframes(3 * W.WAVE), struct.pack('<%dh' % (3 * W.WAVE), *pcm(body[64:64 + 6 * W.WAVE])))
            self.assertEqual(H.u32(body, 8), 3 * W.FRAMES)
            # Equal to the converter on the file's own samples.
            _, channels = H.read_wav_channels(src)
            self.assertEqual(body[64:], W.convert(channels, 'lead_wt1024')[1])


if __name__ == '__main__':
    unittest.main()
