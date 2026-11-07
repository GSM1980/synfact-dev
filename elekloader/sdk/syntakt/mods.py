# SPDX-License-Identifier: GPL-2.0-or-later
"""Stage and confirm a safe SMZ4 bank with native Data RPC and verified readback.

python -m elekloader.sdk.syntakt.mods put out/samplers.smz4 --yes-write-device
python -m elekloader.sdk.syntakt.mods get --out out/readback.smz4
"""
import argparse
from pathlib import Path
import struct

from .data_rpc import DataClient, MidiTransport
from .smz import CONTRACT
SLOT_CAP=8*1024*1024-1024


def main(argv=None):
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--port',default='Syntakt')
    p.add_argument('--block',type=int,default=2048)
    sub=p.add_subparsers(dest='cmd',required=True)
    put=sub.add_parser('put');put.add_argument('file')
    put.add_argument('--yes-write-device',action='store_true')
    get=sub.add_parser('get');get.add_argument('--out',required=True)
    sub.add_parser('confirm')
    sub.add_parser('status')
    a=p.parse_args(argv)
    if not 64<=a.block<=8192:p.error('block must be 64..8192 bytes')
    if a.cmd=='put':
        if not a.yes_write_device:p.error('this stages a new bank in eMMC; pass --yes-write-device')
        source=Path(a.file)
        if source.stat().st_size>SLOT_CAP:p.error('bank exceeds the 8 MiB slot')
        body=source.read_bytes()
        if len(body)>SLOT_CAP:p.error('bank exceeds the 8 MiB slot')
    elif a.cmd=='get':
        destination=Path(a.out)
        if destination.exists():p.error('output already exists')
    transport=MidiTransport(a.port)
    try:
        client=DataClient(transport,block=a.block)
        if a.cmd=='put':
            if len(body)<68 or body[:4]!=b'SAFE' or not (struct.unpack_from('>I',body,24)[0]&16) or struct.unpack_from('>I',body,64)[0]!=CONTRACT:
                raise RuntimeError('bank has no matching core contract')
            status=client.read('/mod-status')
            if len(status)!=128 or struct.unpack_from('>I',status)[0]!=0x454c5331 or struct.unpack_from('>I',status,88)[0]!=CONTRACT:
                raise RuntimeError('installed core cannot safely stage this bank; update the core first')
            client.write('/mods-stage',body)
            if client.read('/mods-stage')!=body:raise RuntimeError('/mods-stage readback differs')
            print(f'/mods-stage: {len(body)} bytes uploaded and readback verified. Restart to try the bank; previous bank retained.')
        elif a.cmd=='get':
            body=client.read('/mods')
            with destination.open('xb') as f:f.write(body)
            print(f'{destination}: {len(body)} bytes read from /mods')
        elif a.cmd=='confirm':
            status=client.read('/mod-status')
            if len(status)!=128:raise RuntimeError('safe mods status unavailable')
            values=struct.unpack('>32I',status)
            if values[22]!=CONTRACT or values[2]!=0 or values[23]!=1 or values[24]>1 or not values[25]:
                raise RuntimeError('no booted mods trial is ready to confirm')
            slot,sequence=values[24:26]
            client.write('/mods-confirm',struct.pack('>4I',0x4d434631,sequence,slot,
                                                  sequence^slot^0xa55a5aa5))
            after=struct.unpack('>32I',client.read('/mod-status'))
            if after[23]!=0 or after[2]!=0:raise RuntimeError('mods confirmation was not recorded')
            print('Mods trial confirmed. This bank remains active after restart.')
        else:
            body=client.read('/mod-status')
            if len(body) not in (64,128):raise RuntimeError('invalid loader status size')
            values=struct.unpack('>'+str(len(body)//4)+'I',body)
            if values[0]!=0x454c5331 or (values[1],len(body)) not in ((1,64),(2,128)):
                raise RuntimeError('unsupported loader status')
            labels={0:'loaded',1:'no committed file',2:'bank validation rejected',3:'bank read failed',
                    4:'not enough MAIN heap',5:'machine initialization rejected',6:'AUDIO ABI missing',
                    7:'sample-pool service missing',8:'metadata/card read failed',9:'startup hook not reached',
                    10:'core/SMZ contract missing or incompatible',11:'resident hook preimage mismatch'}
            print(f'SMZ: {labels.get(values[2],"unknown")} (state {values[2]})')
            print(f'File: {values[3]} bytes; magic {values[4]:08X}; parser line {values[5]}')
            print(f'Active: {values[6]}; machines: {values[7]}; AUDIO pages: {values[8]}')
            print(f'eMMC ready: {values[9]}; sectors: {values[10]}')
            print(f'Registry mask: {values[11]:04X}; selector queries: {values[12]:04X}; track: {values[13]}')
            print(f'Selector limit: {values[14]}; loading-mods draws: {values[15]}')
            if values[1]==2:
                print(f'Available mask: {values[16]:04X}; final vector: {values[17]:04X}; rows: {values[18]}; builds: {values[19]}')
                print(f'Name queries: {values[20]:04X}; category queries: {values[21]:04X}')
                if len(values)>22:print(f'Core contract: {values[22]:08X}')
                if len(values)>25 and values[23]:print(f'Trial pending confirmation: slot {values[24]}, generation {values[25]}')
                if len(values)>26 and values[26]:print('Previous candidate was rolled back; the previous bank or stock machines are active.')
    finally:transport.close()
    return 0


if __name__=='__main__':raise SystemExit(main())
