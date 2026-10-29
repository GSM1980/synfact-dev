# SPDX-License-Identifier: GPL-2.0-or-later
"""Static build checks; no emulation or device access."""
from pathlib import Path
import sys
import unittest
from types import SimpleNamespace

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader.sdk.syntakt.audio import audit,audit_functions
from elekloader.sdk import elf


class InstructionAudit(unittest.TestCase):
    def test_integer_emac_cache_and_control_remain_allowed(self):
        for code in ('70014e75','203c123456784e75','a9c0','f468','4e7b0002'):
            with self.subTest(code=code):self.assertGreater(audit(bytes.fromhex(code)),0)

    def test_fpu_linef_and_legacy_instructions_are_rejected(self):
        for code in ('f2000022','f2000000','f310','ffff','51c8fffc','01080000','4afc'):
            with self.subTest(code=code),self.assertRaisesRegex(ValueError,'unsupported'):
                audit(bytes.fromhex(code),0x4000)

    def test_every_incomplete_prefix_is_rejected(self):
        # Immediate payloads need not be read by the decoder to determine size.
        for code in ('203c12345678','008012345678','4e560000','60000004','4eb940000400'):
            raw=bytes.fromhex(code)
            self.assertGreater(audit(raw),0)
            for size in range(1,len(raw)):
                with self.subTest(code=code,size=size),self.assertRaises(ValueError):audit(raw[:size])
        with self.assertRaisesRegex(ValueError,'aligned'):audit(b'\x4e\x75',1)

    def test_linked_functions_exclude_data_and_check_bounds(self):
        section=SimpleNamespace(idx=1,data=bytes.fromhex('ffff4e75ffff'))
        function=SimpleNamespace(shndx=1,type=elf.STT_FUNC,size=2,value=0x4002,name='entry')
        image=SimpleNamespace(section=lambda name:section,symbols=[function])
        self.assertEqual(audit_functions(image,'.image',0x4000),{'entry':{'bytes':2,'instructions':1}})
        function.value=0x4006
        with self.assertRaisesRegex(ValueError,'exceeds'):audit_functions(image,'.image',0x4000)
        function.value=0x4000
        with self.assertRaisesRegex(ValueError,'unsupported'):audit_functions(image,'.image',0x4000)


if __name__=='__main__':unittest.main()
