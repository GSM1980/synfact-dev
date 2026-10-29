"""Verify registered SYN IDs bypass only native editor's second translation.

python tests/test_st_syn_page_hook.py out/core-st-0.9.2-syn-page-editing
Static final-image validation, not ColdFire execution.
"""
import json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader.sdk.syntakt.audio import audit
from elekloader.isa import coldfire as cf
from elekloader import syx

def verify(folder):
    folder=Path(folder);r=json.loads((folder/'verification.json').read_text())
    image=(folder/'main.bin').read_bytes();s=r['symbols'];layout=r['link']
    def read(at,n):
        if layout['ddr'][0]<=at<layout['ddr'][1]:at=layout['run_load']+at-layout['ddr'][0]
        return image[at-0x40000400:at-0x40000400+n]
    def jump(at):return struct.pack('>HI',0x4ef9,at)
    def call(at,target):
        raw=read(at,6)
        if raw[:2]==b'\x4e\xb9':assert int.from_bytes(raw[2:6],'big')==target;return 6
        assert raw[:2] in (b'\x4e\xba',b'\x61\x00') and at+2+int.from_bytes(raw[2:4],'big',signed=True)==target
        return 4
    entry=s['core:core_machine_synth_parameter'];stock=s['core:core_stock_synth_parameter']
    assert read(0x400a6178,10)==jump(entry)+b'\x4e\x71'*2
    # No bank or native ID reaches the exact stock replay without a C query.
    fast=(struct.pack('>HIHh',0x4ab9,s['core:core_machine_active'],0x6700,stock-(entry+8))+
          struct.pack('>HHHIHh',0x202f,4,0x0c80,0x1f9,0x6500,stock-(entry+22)))
    assert len(fast)==24 and read(entry,24)==fast
    # Query 19 validates registration before the caller-specific change. Both
    # volatile address registers and the scratch result have the original frame.
    head=bytes.fromhex('4feffff42e882f490004486f00082f2f000441ef00182f0848780013')
    assert read(entry+24,len(head))==head
    at=entry+24+len(head);at+=call(at,s['core:core_machine_query'])
    assert read(at,6)==bytes.fromhex('4fef00104a80');at+=6
    miss=read(at,2);assert miss[0]==0x67;miss_target=at+2+int.from_bytes(miss[1:2],'big',signed=True);at+=2
    # A handled ID keeps SYN classification except at native resolver 40038dae.
    tail=bytes.fromhex('202f0008222f000c0c8140038dc2660270002057226f00044fef000c4e75')
    assert read(at,len(tail))==tail;at+=len(tail)
    assert miss_target==at and read(at,10)==bytes.fromhex('2057226f00044fef000c')
    assert read(stock,16)==bytes.fromhex('222f00040c81000001f9')+jump(0x400a6182)
    assert audit(read(entry,stock+16-entry),entry)>0
    # The unchanged native resolver calls the SYN classifier, branches on zero
    # straight to return(input-ID), and only nonzero enters source_layout(type).
    assert read(0x40038dba,14)==bytes.fromhex('2f0a4eb9400a6178588f4a80676e')
    assert read(0x40038df2,6)==bytes.fromhex('4eb94007b9ea')
    assert read(0x40038e36,12)==bytes.fromhex('200a4cee0c0cffc44e5e4e75')
    for pc,ins in cf.disassemble_lengths(read(entry,stock-entry),entry,entry,stock):
        if ins.flow in (cf.F_BRA,cf.F_BSR,cf.F_BCC) and ins.flow!=cf.F_BSR:
            assert entry<=ins.target<=stock
    built=syx.Syx(next(folder.glob('*.syx')).read_bytes());assert built.section(3)==image
    print('PASS: registered-ID gate, resolver-only override, native identity return, normal SYN classification/fallback, preserved frame and final SYX')

if __name__=='__main__':verify(sys.argv[1])
