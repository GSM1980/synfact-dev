# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic secondary-image guards. No firmware, hardware or emulator."""
from dataclasses import replace
import hashlib
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader import syx
from elekloader.codec import transport
from elekloader.devices.syntakt import DEVICE


class SecondaryTests(unittest.TestCase):
    def test_runtime_windows_are_exact_and_exclude_boot_vectors(self):
        windows=DEVICE.secondary[7][3]
        self.assertEqual(windows,((0x4000094a,0x40000950),(0x40000b04,0x40000b0a),
            (0x40001016,0x40001020),(0x400043b2,0x400043b6),(0x400043fe,0x40004400),
            (0x40004404,0x40004406),(0x40004408,0x4000440c),(0x40004460,0x40004462),
            (0x400044ac,0x400044b0),(0x40040860,0x40041050)))
        self.assertFalse(any(lo<=DEVICE.main_load<hi for lo,hi in windows))

    def setUp(self):
        self.main=b'synthetic main '*30
        packed=syx.pack_main(self.main); self.audio=bytes([0xa5])*64
        cont=bytearray(0x80);cont[:4]=b'ELE3';cont[0x14:0x18]=b'1.00'
        struct.pack_into('>I',cont,0x1c,3)
        for row,(sid,data,dest) in enumerate([(2,b'protected boot!!',0x4000000),
                                             (3,packed,DEVICE.main_load),(7,self.audio,0)]):
            struct.pack_into('>IIII',cont,0x20+16*row,sid,len(cont),len(data),dest)
            cont+=data;cont+=bytes(-len(cont)%16)
        frame=bytes.fromhex('f000203c16007f010500017200000000f7')[:15]+b'\xf7'
        end=bytearray(frame);end[7]=2
        raw=transport.encode_syx(struct.pack('>II',len(cont),transport.content_checksum(cont))+cont,
                                 0x16,frame,bytes(end))
        self.stock=syx.Syx(raw)
        self.dev=replace(DEVICE,terminal_zero_packet=False,
            secondary={7:(hashlib.sha256(self.audio).hexdigest(),64,0x40000400,
                          ((0x40000410,0x40000420),))})
        self.changed=bytearray(self.audio);self.changed[16]=1
        self.packed=packed

    def test_explicit_companion_and_implicit_refusal(self):
        out=syx.write(self.stock,self.packed,self.dev,secondary={7:self.changed})
        facts=syx.verify(out,self.stock,self.main,self.dev,secondary={7:self.changed})
        self.assertTrue(facts['sections']['2']['stock'])
        self.assertFalse(facts['sections']['7']['stock'])
        with self.assertRaisesRegex(syx.SyxError,'section 7 is not stock'):
            syx.verify(out,self.stock,self.main,self.dev)
        wrong=bytearray(self.changed);wrong[17]=2
        with self.assertRaisesRegex(syx.SyxError,'requested secondary'):
            syx.verify(out,self.stock,self.main,self.dev,secondary={7:wrong})

    def test_size_pin_windows_and_protected_sections(self):
        for image in (self.changed[:-1],self.changed+bytes(1)):
            with self.assertRaisesRegex(syx.SyxError,'length or stock hash'):
                syx.write(self.stock,self.packed,self.dev,secondary={7:image})
        outside=bytearray(self.changed);outside[32]=1
        with self.assertRaisesRegex(syx.SyxError,'outside secondary patch ranges'):
            syx.write(self.stock,self.packed,self.dev,secondary={7:outside})
        wrong=replace(self.dev,secondary={7:('0'*64,64,0x40000400,())})
        with self.assertRaisesRegex(syx.SyxError,'length or stock hash'):
            syx.write(self.stock,self.packed,wrong,secondary={7:self.changed})
        for sid in (2,4,6,3,8):
            with self.assertRaisesRegex(syx.SyxError,'not an allowed secondary'):
                syx.write(self.stock,self.packed,self.dev,secondary={sid:bytes(64)})


if __name__=='__main__':unittest.main()
