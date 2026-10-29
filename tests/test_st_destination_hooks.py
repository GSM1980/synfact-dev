"""Final-image checks for four inline destination-label paths; no emulation.

python tests/test_st_destination_hooks.py out/core-st-0.9.1-destination-labels
"""
import json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader import syx
from elekloader.sdk.syntakt.audio import audit
from elekloader.isa import coldfire as cf

SITES=(
 ('lfo',0x4007c272,0x4007c28e,'0c82000001f955c041f94022d59c7100c48070344c0028002f302830','0c82000001f9','2f02'),
 ('mod',0x4007c3c0,0x4007c3dc,'0c82000001f955c041f94022d59c7100c48070344c0028002f302830','0c82000001f9','2f02'),
 ('mod_row',0x400dffda,0x400e0002,'bbfc000001f86204200d6002428072344c01080041f94022d59c2a3c00007f00207008302f480030','bbfc000001f8','2f0d'),
 ('mod_pair',0x400de5e0,0x400de5fc,'0c84000001f955c041f94022d59c7100c88070344c0048002f304830','0c84000001f9','2f04'))

def verify(folder):
    folder=Path(folder);r=json.loads((folder/'verification.json').read_text())
    image=(folder/'main.bin').read_bytes();sym=r['symbols'];layout=r['link']
    def read(at,n):
        if layout['ddr'][0]<=at<layout['ddr'][1]:at=layout['run_load']+at-layout['ddr'][0]
        return image[at-0x40000400:at-0x40000400+n]
    def jump(at):return struct.pack('>HI',0x4ef9,at)
    def call_target(at):
        raw=read(at,6);op=raw[:2]
        if op==b'\x4e\xb9':return struct.unpack('>I',raw[2:6])[0],6
        if op in (b'\x4e\xba',b'\x61\x00'):return at+2+struct.unpack('>h',raw[2:4])[0],4
        raise AssertionError(('unexpected call',hex(at),raw.hex()))
    for name,site,end,before,prefix,arg in SITES:
        entry=sym['core:core_machine_'+name+'_label'];native=sym['core:core_stock_'+name+'_label']
        expected=jump(entry)+b'\x4e\x71'*((end-site-6)//2)
        assert read(site,end-site)==expected
        replay=bytes.fromhex(before)+jump(end);assert read(native,len(replay))==replay
        # Internal row branches are wholly contained and keep their offsets.
        for pc,ins in cf.disassemble_lengths(bytes.fromhex(before),site,site,end):
            if ins.flow in (cf.F_BRA,cf.F_BSR,cf.F_BCC):assert site<=ins.target<end
        head=bytes.fromhex(prefix);assert read(entry,6)==head
        branch=read(entry+6,2);assert branch[0]==(0x63 if name=='mod_row' else 0x65)
        assert entry+8+int.from_bytes(branch[1:2],'big',signed=True)==native
        assert read(entry+8,4)==bytes.fromhex(arg+'42a7') # ID, zero context
        target,size=call_target(entry+12);assert target==sym['core:core_machine_parameter_label']
        at=entry+12+size;assert read(at,2)==b'\x50\x8f';at+=2
        tail=bytes.fromhex('20402a3c00007f002f480030') if name=='mod_row' else b'\x2f\x00'
        assert read(at,len(tail)+6)==tail+jump(end)
        assert audit(read(entry,native-entry),entry)>0 and audit(replay,native)>0
    built=syx.Syx(next(folder.glob('*.syx')).read_bytes());assert built.section(3)==image
    assert '-O3' in r['cflags'] and r['resident_ram']['free']>=0
    print('PASS: four final SYX JMPs, every stock replay/internal branch, registered short-label calls, argument cleanup, pushed label/row frame48 and D5; -O3 and budget')

if __name__=='__main__':verify(sys.argv[1])
