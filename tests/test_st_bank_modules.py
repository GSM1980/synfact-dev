"""Composition and callback checks reject unrelated or malformed modules."""
import copy
import json
from pathlib import Path
import struct
import unittest
from st_bank_modules import check_bank_modules

ROOT=Path(__file__).resolve().parents[1]
ALLOWED=(['fm_tone_module','recorder_module','poly_module','system_info_module'],
         ['fm_tone_module','recorder_module','lfo3_module','poly_module','system_info_module'])


def fixture(modules,browser=False):
    raw=bytearray(80+1024)
    struct.pack_into('>II',raw,32,0,len(modules))
    struct.pack_into('>I',raw,56,0)
    struct.pack_into('>4I',raw,80,256,128,320,3 if browser else 2)
    raw[80+256:80+256+18]=b'SP DISCRETE INPUT\0'
    struct.pack_into('>4I',raw,80+320,0x103,160,0x102,192)
    if browser:struct.pack_into('>2I',raw,80+336,0x302,224)
    report={'modules':modules,'main_bytes':1024,'source_sha256':{str(ROOT/'mods/samplers-st/input.c'):'fixture'}}
    if browser:report['source_sha256'][str(ROOT/'mods/samplers-st/browser.inc')]='fixture'
    return report,raw


class BankModules(unittest.TestCase):
    def test_known_legacy_and_sampler_compositions(self):
        for names in ALLOWED:
            for prefix in ([],['sp_sampler_input_module']):
                for browser in (False,True):
                    report,raw=fixture(prefix+names,browser)
                    check_bank_modules(report,raw,ALLOWED)

    def test_unknown_duplicate_or_reordered_modules_are_rejected(self):
        names=['sp_sampler_input_module',*ALLOWED[0]]
        for bad in [names+['unknown'],names+['sp_sampler_input_module'],
                    [names[1],names[0],*names[2:]],names[:-1]]:
            report,raw=fixture(bad)
            with self.assertRaises(AssertionError):check_bank_modules(report,raw,ALLOWED)

    def test_malformed_sampler_registration_is_rejected(self):
        report,raw=fixture(['sp_sampler_input_module',*ALLOWED[0]])
        for at,value in [(36,9),(80+4,129),(80+12,1),(80+320,3),(80+328,0x202),(80+324,1024)]:
            bad=bytearray(raw);struct.pack_into('>I',bad,at,value)
            with self.assertRaises(AssertionError):check_bank_modules(report,bad,ALLOWED)
        bad=copy.deepcopy(report);bad['source_sha256']={}
        with self.assertRaises(AssertionError):check_bank_modules(bad,raw,ALLOWED)
        report,raw=fixture(['sp_sampler_input_module',*ALLOWED[0]],True)
        struct.pack_into('>I',raw,80+12,2)
        with self.assertRaises(AssertionError):check_bank_modules(report,raw,ALLOWED)

    def test_real_sampler_bank(self):
        bank=ROOT/'out/discrete-encoder-steps-20261028/samplers-0.7-discrete-four-steps.smz4'
        if not bank.exists():self.skipTest('local validation bank not supplied')
        report=json.loads(bank.with_suffix('.smz4.json').read_text())
        check_bank_modules(report,bank.read_bytes(),([],))


if __name__=='__main__':unittest.main()
