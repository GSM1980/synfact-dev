# SPDX-License-Identifier: GPL-2.0-or-later
"""Transfer the single /mods file with native Data RPC and verified readback.

python -m elekloader.sdk.syntakt.mods put out/samplers.smz4 --yes-write-device
python -m elekloader.sdk.syntakt.mods get --out out/readback.smz4
"""
import argparse
from pathlib import Path
import struct

from .data_rpc import DataClient, MidiTransport
from .mods_object import MAX_BYTES


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',default='Syntakt')
    p.add_argument('--block',type=int,default=2048)
    sub=p.add_subparsers(dest='cmd',required=True)
    put=sub.add_parser('put');put.add_argument('file')
    put.add_argument('--yes-write-device',action='store_true')
    get=sub.add_parser('get');get.add_argument('--out',required=True)
    sub.add_parser('status')
    a=p.parse_args(argv)
    if not 64<=a.block<=8192:p.error('block must be 64..8192 bytes')
    if a.cmd=='put':
        if not a.yes_write_device:p.error('this replaces /mods in eMMC; pass --yes-write-device')
        source=Path(a.file)
        if source.stat().st_size>MAX_BYTES:p.error('/mods exceeds 16 MiB')
        body=source.read_bytes()
        if len(body)>MAX_BYTES:p.error('/mods exceeds 16 MiB')
    elif a.cmd=='get':
        destination=Path(a.out)
        if destination.exists():p.error('output already exists')
    transport=MidiTransport(a.port)
    try:
        client=DataClient(transport,block=a.block)
        if a.cmd=='put':
            client.write('/mods',body)
            if client.read('/mods')!=body:raise RuntimeError('/mods readback differs')
            print(f'/mods: {len(body)} bytes uploaded and readback verified. Restart to load the bank.')
        elif a.cmd=='get':
            body=client.read('/mods')
            with destination.open('xb') as f:f.write(body)
            print(f'{destination}: {len(body)} bytes read from /mods')
        else:
            body=client.read('/mod-status')
            if len(body) not in (64,128):raise RuntimeError('invalid loader status size')
            values=struct.unpack('>'+str(len(body)//4)+'I',body)
            if values[0]!=0x454c5331 or (values[1],len(body)) not in ((1,64),(2,128)):
                raise RuntimeError('unsupported loader status')
            labels={0:'loaded',1:'no committed file',2:'bank validation rejected',3:'bank read failed',
                    4:'not enough MAIN heap',5:'machine initialization rejected',6:'AUDIO ABI missing',
                    7:'sample-pool service missing',8:'metadata/card read failed',9:'startup hook not reached'}
            print(f'SMZ: {labels.get(values[2],"unknown")} (state {values[2]})')
            print(f'File: {values[3]} bytes; magic {values[4]:08X}; parser line {values[5]}')
            print(f'Active: {values[6]}; machines: {values[7]}; AUDIO pages: {values[8]}')
            print(f'eMMC ready: {values[9]}; sectors: {values[10]}')
            print(f'Registry mask: {values[11]:04X}; selector queries: {values[12]:04X}; track: {values[13]}')
            print(f'Selector limit: {values[14]}; loading-mods draws: {values[15]}')
            if values[1]==2:
                print(f'Available mask: {values[16]:04X}; final vector: {values[17]:04X}; rows: {values[18]}; builds: {values[19]}')
                print(f'Name queries: {values[20]:04X}; category queries: {values[21]:04X}')
    finally:transport.close()
    return 0


if __name__=='__main__':raise SystemExit(main())
