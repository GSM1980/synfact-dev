# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline /mods transport objects; no ports or instruction emulation."""
import importlib.util
from pathlib import Path
import struct
import unittest
import zlib

path=Path(__file__).resolve().parents[1]/'elekloader/sdk/syntakt/mods_object.py'
spec=importlib.util.spec_from_file_location('mods_object',path)
objects=importlib.util.module_from_spec(spec);spec.loader.exec_module(objects)

class Objects(unittest.TestCase):
    def test_opaque_bytes_empty_and_odd_sizes_are_unchanged(self):
        for body in (b'',b'\x01',b'smz4\x80\xff\x00',bytes(range(256))):
            with self.subTest(size=len(body)):
                obj=objects.pack(body)
                self.assertEqual(obj[31:-12],body)
                self.assertEqual(len(obj),len(body)+43)
                self.assertEqual(struct.unpack('>I',obj[25:29])[0],len(body))
                self.assertEqual(obj[5:9],b'\x00\x08\x00\x0d')
                self.assertEqual(struct.unpack('>III',obj[-12:]),
                    (zlib.crc32(body,0xffffffff)&0xffffffff,len(body),0xaaa1daaa))
                self.assertEqual(objects.unpack(obj),body)

    def test_bad_envelopes_and_oversize_are_rejected(self):
        good=objects.pack(b'test')
        for offset in (0,4,5,13,25,29,30,len(good)-12,len(good)-1):
            bad=bytearray(good);bad[offset]^=1
            with self.subTest(offset=offset),self.assertRaises(ValueError):objects.unpack(bad)
        with self.assertRaises(ValueError):objects.unpack(good[:30])
        with self.assertRaisesRegex(ValueError,'16 MiB'):objects.pack(bytes(objects.MAX_BYTES+1))

if __name__=='__main__':unittest.main()
