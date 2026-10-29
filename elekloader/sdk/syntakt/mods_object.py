# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline Data RPC envelope for the opaque /mods file. No MIDI/USB access.

    python -m elekloader.sdk.syntakt.mods_object pack code.smz4 out/code.obj
    python -m elekloader.sdk.syntakt.mods_object unpack out/readback.obj out/code.smz4

Send/read the object with a Data* client using the exact path /mods.
The 31-byte header and 12-byte footer are native transport, not SMZ4 bytes.
The device stores only the unmodified file; no SAMP header or odd-byte pad.
"""
import argparse
from pathlib import Path
import struct
import zlib

MAX_BYTES=16*1024*1024

def crc(body):return zlib.crc32(body,0xffffffff)&0xffffffff

def pack(body):
    if len(body)>MAX_BYTES:raise ValueError('/mods file exceeds 16 MiB')
    header=(struct.pack('>IBHH4sIIII',0xac11d303,2,8,13,b'0094',8,0xffffffff,1,len(body))
            +bytes((0,12)))
    return header+body+struct.pack('>III',crc(body),len(body),0xaaa1daaa)

def unpack(obj):
    if len(obj)<43:raise ValueError('truncated Data object')
    h,body,f=obj[:31],obj[31:-12],obj[-12:]
    if int.from_bytes(h[:4],'big')!=0xac11d303 or h[4]!=2 or h[5:7]!=b'\x00\x08':
        raise ValueError('invalid Data object header')
    if int.from_bytes(h[13:17],'big')!=8 or h[29:]!=b'\x00\x0c':
        raise ValueError('wrong Data object type or compression')
    if len(body)>MAX_BYTES or int.from_bytes(h[25:29],'big')!=len(body):
        raise ValueError('invalid /mods body length')
    if struct.unpack('>III',f)!=(crc(body),len(body),0xaaa1daaa):
        raise ValueError('Data object footer/checksum differs')
    return body

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('action',choices=('pack','unpack'));p.add_argument('input');p.add_argument('output')
    a=p.parse_args();src=Path(a.input);dst=Path(a.output)
    if src.resolve()==dst.resolve():p.error('input and output must differ')
    if a.action=='pack' and src.stat().st_size>MAX_BYTES:p.error('/mods file exceeds 16 MiB')
    if dst.exists():p.error('output already exists')
    data=(pack if a.action=='pack' else unpack)(src.read_bytes());dst.write_bytes(data)
    print('%s: %d bytes; offline only'%(dst,len(data)))

if __name__=='__main__':main()
