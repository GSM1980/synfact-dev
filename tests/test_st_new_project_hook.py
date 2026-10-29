# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify the real NEW PROJECT entry and wrapper in a built image, no emulation.

python tests/test_st_new_project_hook.py out/core-st-0.8.3-new-project-pool
"""
import json
from pathlib import Path
import struct
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader import syx
from elekloader.sdk.syntakt.audio import audit


def verify(folder):
    folder=Path(folder)
    report=json.loads((folder/'verification.json').read_text())
    image=(folder/'main.bin').read_bytes()
    symbols=report['symbols'];layout=report['link']
    def read(address,size):
        if layout['ddr'][0]<=address<layout['ddr'][1]:
            address=layout['run_load']+address-layout['ddr'][0]
        start=address-0x40000400
        assert start>=0 and start+size<=len(image)
        return image[start:start+size]
    def jump(name,opcode=0x4ef9):return struct.pack('>HI',opcode,symbols['core:'+name])
    def call(name):return jump(name,0x4eb9)
    assert read(0x4001d16a,8)==jump('core_pool_initialize')+bytes.fromhex('4e71')
    # Actual user command, load-handler initializer and current-project clear.
    for address in (0x40059f5e,0x4005bb98,0x400c19ac):
        assert read(address,6)==bytes.fromhex('4eb94001d16a')
    entry=symbols['core:core_pool_initialize']
    native_call=struct.pack('>Hh',0x4eba,symbols['core:core_stock_pool_initialize']-(entry+10))
    expected=(bytes.fromhex('2f2f00082f2f0008')+native_call+
              bytes.fromhex('508f2f00')+call('core_pool_created')+bytes.fromhex('201f4e75'))
    assert read(symbols['core:core_pool_initialize'],len(expected))==expected
    replay=bytes.fromhex('4e56ffa848d71c3c4ef94001d172')
    assert read(symbols['core:core_stock_pool_initialize'],len(replay))==replay
    assert audit(expected)>0 and audit(replay)>0
    built=syx.Syx(next(folder.glob('*.syx')).read_bytes())
    assert built.section(3)==image
    print('PASS: real NEW PROJECT caller/entry; native arguments, body-first order, saved return; exact prologue and final SYX image')


if __name__=='__main__':verify(sys.argv[1])
