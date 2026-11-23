"""Cross-check donor reconstruction against the original independent importer."""
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from elekloader.devices.monomachine import digipro_tables
from elekloader.sdk.syntakt import donor


class MonomachineDonorTests(unittest.TestCase):
    def test_unknown_file_is_refused(self):
        with self.assertRaisesRegex(ValueError,'official Monomachine'):digipro_tables(b'wrong OS')

    @unittest.skipUnless(os.environ.get('MONOMACHINE_OS'),'MONOMACHINE_OS not supplied')
    def test_all_tables_match_the_pinned_upstream_importer(self):
        folder=ROOT/'mods/digimono-st/dpro'
        sys.path.insert(0,str(folder))
        spec=importlib.util.spec_from_file_location('native_dpro_importer',folder/'import_tables.py')
        native=importlib.util.module_from_spec(spec);spec.loader.exec_module(native)
        text,_=native.generate(os.environ['MONOMACHINE_OS'])
        raw=Path(os.environ['MONOMACHINE_OS']).read_bytes()
        actual=digipro_tables(raw)
        for name,value in actual.items():
            body=re.search(r'dpt_'+name+r'\[\d+\]=\{(.*?)\};',text,re.S)[1]
            words=[int(v,0)&0xffffffff for v in body.replace('\n','').split(',') if v.strip()]
            self.assertEqual(value,struct.pack('>'+str(len(words))+'I',*words),name)
        recipe=json.loads((ROOT/'mods/digimono-st/monomachine.json').read_text())
        self.assertEqual(donor.regions(recipe,raw),actual)
        recipe['regions'][0]['sha256']='0'*64
        with self.assertRaisesRegex(donor.DonorError,'hash mismatch'):donor.regions(recipe,raw)

if __name__=='__main__':unittest.main()
