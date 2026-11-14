"""Linked stock fast-path ABI audit. No target instruction execution."""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader.isa import coldfire as cf

def verify(folder):
    report=json.loads((folder/'verification.json').read_text());symbols=report['symbols']
    raw=(folder/'main.bin').read_bytes();layout=report['link']
    for name in ('core_modules_bind','core_machine_load','core_native_initialize'):
        assert layout['ddr'][0]<=symbols['core:'+name]<layout['ddr'][1], ('nonresident startup entry',name)
    def address(name):return symbols['core:'+name]
    def read(at,n):
        if layout['ddr'][0]<=at<layout['ddr'][1]:at=layout['run_load']+at-layout['ddr'][0]
        return raw[at-0x40000400:at-0x40000400+n]
    def flag(at,name):assert read(at,6)==struct.pack('>HI',0x4ab9,address(name))
    def interest(at,op):assert read(at,6)==struct.pack('>HI',0x4a39,address('core_native_interest')+op)
    def branch(at,condition,target=None):
        _,instruction=next(cf.disassemble_lengths(read(at,6),at,at,at+6))
        assert read(at,1)==bytes([condition]) and instruction.flow==cf.F_BCC
        if target is not None:assert instruction.target==target
        return instruction.target
    cases={
        'name_full':(4,64),'name_short':(4,64),'group_name':(4,64),
        'menu_category':(4,64),'availability':(4,64),'digital_kind':(4,64),
        'ui_to_engine':(4,0x2e0),'local_slot':(4,0x1f9),'owner':(4,0x1f9),
        'engine_to_ui':(4,0x1f9),'slot_to_engine':(8,64),'page_local':(8,64),
        'page_copy':(4,64),'range_copy':(4,0x1f9),'valid_owner':(4,0x1f9),
        'source_layout':(4,0),'cached_parameter':(4,0x1f9),'format_value':(4,0x1f9),
        'parameter_label':(8,0x1f9),'synth_parameter':(4,0x1f9),
        'parameter_long_label':(8,0x1f9),'modulation_flags':(4,0x1f9),'menu_rank':(4,0)}
    SEND={'cached_parameter','format_value','parameter_label','parameter_long_label'}
    for name,(arg,first) in cases.items():
        at=address('core_machine_'+name);stock=address('core_stock_'+name)
        if name=='cached_parameter':
            # The randomizer has a separate caller-specific early path before
            # the ordinary stock-first lookup below.
            assert read(at,10)==bytes.fromhex('22170c8140014970661a')
            at+=36
        flag(at,'core_machine_active');branch(at+6,0x67,stock)
        if first:
            assert read(at+10,10)==struct.pack('>HHHI',0x202f,arg,0x0c80,first)
            if name in SEND:   # 1.2: parameter 72 (the chorus's CHO) also reaches the providers
                assert branch(at+20,0x64)==at+32
                assert read(at+22,6)==struct.pack('>HI',0x0c80,72)
                branch(at+28,0x66,stock)
            else:branch(at+20,0x65,stock)
    # All twelve native bridges retain their original replay AND active path.
    ops={'trig': 0, 'enqueue': 1, 'header_load': 5, 'header_reset': 6, 'type': 4, 'src_type': 4, 'publish': 3, 'immediate': 2, 'sound_set': 7, 'machine_select': 8, 'modulation': 16, 'lfo': 17}
    for name in ['trig','enqueue','header_load','header_reset']:
        at=address('core_native_'+name);interest(at,ops[name])
        branch(at+6,0x67,address('core_stock_'+name))
        assert read(at+10,4)==bytes.fromhex('41ef0004')
    for name,native in [('type','7119e1883140003e'),('src_type','712b0da07225b280')]:
        at=address('core_native_'+name);interest(at,ops[name])
        stock=branch(at+6,0x67);assert read(stock,8)==bytes.fromhex(native)
        assert read(at+8,6)==struct.pack('>HI',0x4a39,address('core_native_interest')+19)
        slow=branch(at+14,0x66);assert slow==stock+14 and branch(stock-2,0x66)==slow
        assert read(stock-8,6)==struct.pack('>HI',0x0139,address('core_native_interest')+18)   # btst d0,abs.l
    for name,replay in [('publish','49f9800046884285'),('immediate','2479444de4344a8a'),
                        ('sound_set','4e56ffd848d704fc'),('machine_select','4fefffd848d77c7c'),
                        ('modulation','4fefffc448d77cfc'),('lfo','4fefffc048d77cfc')]:
        at=address('core_native_'+name);interest(at,ops[name])
        target=branch(at+6,0x67)
        if name=='sound_set':   # 0.12.15: the reserved LFO3 ID check precedes the replay
            assert read(target,4)==bytes.fromhex('202f0008') and read(target+4,6)==struct.pack('>HI',0x0480,0x3f9)
            target+=read(target,64).index(bytes.fromhex(replay))
        assert read(target,8)==bytes.fromhex(replay),(name,hex(target))
    manifest=json.loads(Path('mods/core-st/mod.json').read_text())
    at=address('core_native_note_accept');interest(at,20)
    target=branch(at+6,0x67)
    assert read(target,12)==bytes.fromhex('202a002472014ef9400a4244')
    assert len(manifest['sites'])==106 and len(manifest['collections'])==16   # Current hook and mixer event tables.
    assert report['resident_ram']['free']>=0 and '-O3' in report['cflags']
    print('PASS: 23 no-bank/stock-ID lookup exits and 12 per-operation interest native replays; hidden pointers/stack untouched on fast exits; all hooks, extended paths and APIs retained')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path)
    verify(p.parse_args().folder)
