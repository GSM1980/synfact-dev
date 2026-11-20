"""Numerical checks for the reference comparator; no firmware needed."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

import numpy as np
from scipy.io import wavfile

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('mnm_compare', ROOT/'tools/monomachine-reference/compare.py')
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)


class Metrics(unittest.TestCase):
    def test_frequency_harmonic_gain_and_dc(self):
        sr, f = 48000, 440.17
        t = np.arange(sr)/sr
        y = .5*np.sin(2*np.pi*f*t+.7)+.005*np.sin(4*np.pi*f*t)+.001
        fit = compare.fit_tone(y, sr, 440)
        self.assertAlmostEqual(fit['frequency_hz'], f, delta=.002)
        self.assertAlmostEqual(fit['fundamental_peak'], .5, delta=.001)
        self.assertAlmostEqual(fit['harmonics_dbc'][1], -40, delta=.1)
        self.assertAlmostEqual(fit['dc'], .001, delta=.0001)

    def test_native_sample_rates_give_same_physical_pitch(self):
        fits = []
        for sr in (44100, 48000):
            t = np.arange(sr)/sr
            fits.append(compare.fit_tone(.4*np.sin(2*np.pi*65.4*t), sr, 65.4))
        cents = 1200*np.log2(fits[0]['frequency_hz']/fits[1]['frequency_hz'])
        self.assertLess(abs(cents), .01)

    def test_phase_continuity_and_reset_are_distinguished(self):
        sr, f, event = 48000, 261.625565, .32
        t = np.arange(sr)/sr
        y = .4*np.sin(2*np.pi*f*t+.3)
        self.assertLess(abs(compare.phase_jump(y, sr, f, event)), .001)
        reset = y.copy();reset[round(event*sr):] = .4*np.sin(2*np.pi*f*(t[round(event*sr):]-event))
        expected = np.degrees(np.angle(np.exp(-1j*(2*np.pi*f*event+.3))))
        self.assertAlmostEqual(compare.phase_jump(reset, sr, f, event), expected, delta=.01)

    def test_silence_cannot_be_scored_as_a_matching_tone(self):
        with self.assertRaises(ValueError):
            compare.fit_tone(np.zeros(48000), 48000, 440)

    def test_integer_and_float_wav_scaling(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'input.wav'
            wavfile.write(path, 44100, np.array([-32768, 0, 16384], dtype=np.int16))
            sr, y = compare.load_wav(path)
            self.assertEqual(sr, 44100)
            np.testing.assert_array_equal(y[:, 0], [-1, 0, .5])
            wavfile.write(path, 48000, np.array([[.25, -.5]], dtype=np.float32))
            _, y = compare.load_wav(path)
            np.testing.assert_array_equal(y, [[.25, -.5]])

    def test_event_times_are_representable_in_both_native_blocks(self):
        for case in compare.cases():
            for time, _ in case['events']:
                self.assertEqual(round(time*44100)%16, 0)
                self.assertEqual(round(time*48000)%32, 0)


if __name__ == '__main__':
    unittest.main()
