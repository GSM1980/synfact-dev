# SPDX-License-Identifier: GPL-2.0-or-later
"""The /mods CLI uses explicit transfers and checks readback; no MIDI ports."""
from contextlib import redirect_stdout,redirect_stderr
from io import StringIO
from pathlib import Path
import sys
import struct
import tempfile
import unittest
from unittest.mock import patch,MagicMock

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader.sdk.syntakt import mods


class ModsCLI(unittest.TestCase):
    def test_status_is_read_only_and_explains_rejection(self):
        client=MagicMock();transport=MagicMock();output=StringIO()
        client.read.return_value=struct.pack('>16I',0x454c5331,1,2,7952,0x53414645,89,0,0,0,1,0x3a4000,0,0,0,0,0)
        with patch.object(mods,'MidiTransport',return_value=transport), \
             patch.object(mods,'DataClient',return_value=client),redirect_stdout(output):
            self.assertEqual(mods.main(['status']),0)
        client.read.assert_called_once_with('/mod-status');client.write.assert_not_called()
        self.assertIn('bank validation rejected',output.getvalue());self.assertIn('parser line 89',output.getvalue())
        transport.close.assert_called_once()

    def test_extended_status_reports_final_vector(self):
        client=MagicMock();transport=MagicMock();output=StringIO()
        values=[0x454c5331,2,0,7952,0x53414645,0,1,4,3,1,3817472,0x3c,0xffff,10,80,4,
                0x3c,0x3c,26,1,0x3c,0x3c]+[0]*10
        client.read.return_value=struct.pack('>32I',*values)
        with patch.object(mods,'MidiTransport',return_value=transport), \
             patch.object(mods,'DataClient',return_value=client),redirect_stdout(output):
            self.assertEqual(mods.main(['status']),0)
        self.assertIn('final vector: 003C; rows: 26',output.getvalue());client.write.assert_not_called()

    def test_upload_and_failed_readback_close_port(self):
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'bank.smz4'
            bank=bytearray(80);bank[:4]=b'SAFE'
            struct.pack_into('>I',bank,24,16)
            struct.pack_into('>I',bank,64,mods.CONTRACT)
            source.write_bytes(bank)
            for readback in (source.read_bytes(),b'wrong'):
                transport=MagicMock();client=MagicMock();client.read.return_value=readback
                status=bytearray(128);struct.pack_into('>I',status,0,0x454c5331)
                struct.pack_into('>I',status,88,mods.CONTRACT)
                client.read.side_effect=[bytes(status),readback]
                with patch.object(mods,'MidiTransport',return_value=transport) as opened, \
                     patch.object(mods,'DataClient',return_value=client),redirect_stdout(StringIO()):
                    args=['--port','Syntakt test','put',str(source),'--yes-write-device']
                    if readback==source.read_bytes():self.assertEqual(mods.main(args),0)
                    else:
                        with self.assertRaisesRegex(RuntimeError,'readback differs'):mods.main(args)
                    opened.assert_called_once_with('Syntakt test')
                    client.write.assert_called_once_with('/mods-stage',source.read_bytes())
                    self.assertEqual(client.read.call_args_list[0].args,('/mod-status',))
                    self.assertEqual(client.read.call_args_list[1].args,('/mods-stage',))
                    transport.close.assert_called_once()

    def test_rejected_requests_never_open_port(self):
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'bank.smz4';source.write_bytes(b'bank')
            with patch.object(mods,'MidiTransport') as opened,redirect_stderr(StringIO()):
                for args in (['put',str(source)],['get','--out',str(source)],
                             ['--block','1','put',str(source),'--yes-write-device']):
                    with self.assertRaises(SystemExit):mods.main(args)
                with patch.object(mods,'SLOT_CAP',3):
                    with self.assertRaises(SystemExit):
                        mods.main(['put',str(source),'--yes-write-device'])
                opened.assert_not_called()

    def test_read_downloads_raw_bytes(self):
        with tempfile.TemporaryDirectory() as tmp:
            destination=Path(tmp)/'bank.smz4';transport=MagicMock();client=MagicMock()
            client.read.return_value=b'bank\x80\xff'
            with patch.object(mods,'MidiTransport',return_value=transport), \
                 patch.object(mods,'DataClient',return_value=client),redirect_stdout(StringIO()):
                self.assertEqual(mods.main(['get','--out',str(destination)]),0)
            self.assertEqual(destination.read_bytes(),client.read.return_value)
            client.write.assert_not_called();transport.close.assert_called_once()


if __name__=='__main__':unittest.main()
