"""Complex-machine benchmark metrics/fixtures, using known signals; no OS."""
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/monomachine-reference'))
import complex_compare as bench
from complex_cases import FAMILIES,cases


class ComplexMetrics(unittest.TestCase):
    def test_spectral_distribution_has_explicit_limits_and_ignores_gain(self):
        self.assertAlmostEqual(bench.spectral_distance([1,0],[0,1]),1)
        self.assertAlmostEqual(bench.spectral_distance([1,2],[7,14]),0)
        self.assertIsNone(bench.spectral_distance([0,0],[1,0]))

    def test_same_tone_at_two_sample_rates_is_close_without_phase_alignment(self):
        measurements=[]
        for sr,phase in ((44100,.8),(48000,2.1)):
            t=np.arange(2*sr)/sr
            y=.3*np.sin(2*np.pi*440*t+phase)
            metric,_=bench.audio_metrics(y,sr,.25,1.9)
            measurements.append(metric)
        distance=bench.spectral_distance(*(m['normalized_bands'] for m in measurements))
        self.assertLess(distance,.03)
        self.assertAlmostEqual(measurements[0]['ac_rms_dbfs'],measurements[1]['ac_rms_dbfs'],delta=.01)

    def test_new_harmonics_change_the_spectral_diagnostic(self):
        sr=48000;t=np.arange(2*sr)/sr
        sine=.2*np.sin(2*np.pi*440*t)
        rich=sine+.2*np.sin(2*np.pi*1320*t)
        a,_=bench.audio_metrics(sine,sr,.25,1.9)
        b,_=bench.audio_metrics(rich,sr,.25,1.9)
        self.assertGreater(bench.spectral_distance(a['normalized_bands'],b['normalized_bands']),.3)
        self.assertGreater(b['centroid_hz'],a['centroid_hz']*1.5)

    def test_antiphase_stereo_is_not_lost_from_timbre_analysis(self):
        sr=48000;t=np.arange(2*sr)/sr;y=.3*np.sin(2*np.pi*440*t)
        mono,_=bench.audio_metrics(y,sr,.25,1.9)
        stereo,_=bench.audio_metrics(np.column_stack((y,-y)),sr,.25,1.9)
        self.assertAlmostEqual(stereo['stereo']['correlation'],-1)
        self.assertLess(bench.spectral_distance(mono['normalized_bands'],stereo['normalized_bands']),1e-6)

    def test_dc_alone_is_not_an_audible_oscillator(self):
        metric,_=bench.audio_metrics(np.full(96000,.3),48000,.25,1.9)
        self.assertFalse(metric['audible'])
        self.assertIsNone(metric['centroid_hz'])
        self.assertEqual(metric['peaks'],[])
        self.assertGreater(metric['rms_dbfs'],-11)

    def test_relative_gain_response_cancels_fixed_chain_gain(self):
        def measurement(level):return {'audible':True,'ac_rms_dbfs':level}
        anchor={'reference':measurement(-20),'raw':measurement(-3)}
        same={'reference':measurement(-14),'raw':measurement(3)}
        different={'reference':measurement(-14),'raw':measurement(6)}
        self.assertAlmostEqual(bench.relative_gain_change(same,anchor),0)
        self.assertAlmostEqual(bench.relative_gain_change(different,anchor),3)

    def test_listening_keeps_original_transient_when_steady_part_is_silent(self):
        sr=44100;t=np.arange(sr)/sr
        original=.2*np.sin(2*np.pi*440*t);original[round(.08*sr):]=0
        original=np.column_stack((original,original))
        t=np.arange(48000)/48000;raw=.2*np.sin(2*np.pi*440*t)
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'ab.wav'
            mode=bench.listen_ab(original,raw,path,[.25,.9])
            self.assertEqual(mode,'whole_clip_ac_rms')
            rate,audio=bench.common.load_wav(path)
            self.assertEqual(rate,48000)
            self.assertGreater(np.std(audio[:3000]),.02)
            self.assertLess(np.std(audio[10000:40000]),1e-6)

    def test_fixtures_keep_machine_identity_controls_and_timing(self):
        fixtures=cases();names=set()
        self.assertEqual(set(c['family'] for c in fixtures),set(FAMILIES))
        for case in fixtures:
            self.assertNotIn(case['name'],names);names.add(case['name'])
            self.assertEqual((case['bank_type'],case['original_machine']),FAMILIES[case['family']])
            self.assertEqual(len(case['params']),7)
            self.assertTrue(all(0<=p<=127 for p in case['params']))
            for time,_ in case['events']:
                self.assertEqual(round(time*44100)%16,0)
                self.assertEqual(round(time*48000)%32,0)
                self.assertLess(time,case['duration_s'])
        for family in FAMILIES:
            self.assertTrue(next(c for c in fixtures if c['family']==family)['name'].endswith('_base'))

    def test_raw_capture_retains_both_ens_channels_and_other_types_mono(self):
        class Driver:
            def dm_compare_bytes(self):return 1
            def dm_compare_init(self,state):pass
            def dm_compare_block(self,*args):
                for i in range(32):args[-1][i]=.25
            def dm_compare_stereo_block(self,*args):
                for i in range(32):args[-2][i]=.25;args[-1][i]=-.5
        case=dict(duration_s=32/48000,params=[0]*7,events=[(0,48)],note=48,tune=64,bank_type=4)
        stereo=bench.raw_render(Driver(),case)
        self.assertEqual(stereo.shape,(32,2))
        np.testing.assert_array_equal(stereo[:,0],.25)
        np.testing.assert_array_equal(stereo[:,1],-.5)
        case['bank_type']=3
        mono=bench.raw_render(Driver(),case)
        self.assertEqual(mono.shape,(32,))
        np.testing.assert_array_equal(mono,.25)


if __name__=='__main__':unittest.main()
