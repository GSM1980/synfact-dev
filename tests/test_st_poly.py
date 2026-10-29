"""Source/SMZ/linked native POLY and telemetry migration audit; no target IO."""
import argparse,hashlib,json,re,struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from elekloader.sdk import elf
from elekloader.sdk.syntakt.audio import audit
from st_bank_modules import check_bank_modules

def kernel_layout(s):
    """Raw native relative branches require their original instruction offsets."""
    for start,labels in [
        ('poly_native_shared',{'poly_native_continue':34,'poly_native_next':0x304,'poly_random_bridge':0x326}),
        ('poly_mod_shared',{'poly_mod_first':0x28,'poly_mod_first_continue':0x32,'poly_mod_after_first':0x4e,
                            'poly_mod_second':0x62,'poly_mod_second_continue':0x6c,'poly_mod_return':0x114,
                            'poly_mod_helper_bridge':0x11e})]:
        for name,offset in labels.items():assert s[name]-s[start]==offset,(name,hex(s[name]-s[start]),hex(offset))

def verify(folder,synfact=None):
    folder=folder.resolve();mod=ROOT/'mods/poly-st'
    report=json.loads((folder/'synfact-machines.smz4.json').read_text())
    check_bank_modules(report,(folder/'synfact-machines.smz4').read_bytes(),(
        ['fm_tone_module','recorder_module','poly_module','system_info_module'],
        ['fm_tone_module','recorder_module','lfo3_module','poly_module','system_info_module']))
    assert report['machines']==[66,67,68,69,65] and report['optimization']=='-O3'
    for name,digest in report['source_sha256'].items():
        assert Path(name).is_relative_to(ROOT) and hashlib.sha256(Path(name).read_bytes()).hexdigest()==digest,name
    for name in ['poly_core.c','poly_ui.c','poly_sequence.c','poly_pattern.c','poly_event.c','publication.s','imported/kernels.s']:
        assert str((mod/name).resolve()) in report['source_sha256']
    assert not any('poly_fm_transport' in name for name in report['source_sha256'])
    for directory in [mod,ROOT/'mods/system-info-smz-st']:
        manifest=json.loads((directory/'mod.json').read_text())
        assert not any(manifest.get(x) for x in ['sites','subscribe','contribute','fixed','collections'])
    raw=(folder/'synfact-machines.smz4').read_bytes();image=80+4*struct.unpack_from('>I',raw,56)[0]
    at,n=struct.unpack_from('>II',raw,32);assert n==len(report['modules'])
    for module,expected in [('poly_module',[5,0x101,0x103,0x302,0,11]),('system_info_module',[4,0,1])]:
        index=report['modules'].index(module)
        name,init,subs,count=struct.unpack_from('>4I',raw,image+at+16*index)
        assert count==len(expected) and not init&1 and init<report['main_bytes']
        entries=[struct.unpack_from('>II',raw,image+subs+8*j) for j in range(count)]
        assert [e for e,fn in entries]==expected
        assert all(not fn&1 and fn<report['main_bytes'] for e,fn in entries)
    r=json.loads((folder/'verification.json').read_text());symbols=r['symbols'];layout=r['link']
    main=(folder/'main.bin').read_bytes()
    def address(n):return symbols['core:'+n]
    def read(at,n):
        if layout['ddr'][0]<=at<layout['ddr'][1]:at=layout['run_load']+at-layout['ddr'][0]
        return main[at-0x40000400:at-0x40000400+n]
    assert struct.unpack('>I',read(address('core_machine_ui_api'),4))[0]==7
    names=['core_hzfs_native_io','core_hzfs_open','core_hzfs_close','core_crc_update',
           'core_pool_read','core_pool_write','core_pool_ready','st_pool_live_sync',
           'st_pool_preview_stop','core_event_view','core_clock_snapshot','core_additem',
           'core_pool_busy','core_pool_query_owner','core_native_original','core_machine_telemetry',
           'core_native_profile','core_native_interest_set','core_native_page_view_set']
    names+=['core_parameter_provider_set','core_native']
    assert read(address('core_machine_host_api'),88)==struct.pack('>22I',8,*[address(n) for n in names])
    assert not any(n.startswith('system-info-st:') for n in symbols)
    for name in ['native.c','native.s','view-draw.s']:
        assert r['instructions'][name]['instructions']>0
    assert r['instructions']['machine-host.c']=={'bytes':0,'instructions':0} # read-only pointer table
    manifest=json.loads((ROOT/'mods/core-st/mod.json').read_text())
    native_sites=[s for s in manifest['sites'] if s['target'].startswith('core_native_')]
    assert len(native_sites)==12 and len(manifest['sites'])==96 and len(manifest['collections'])==12
    for site in native_sites:
        assert read(int(site['addr'],16),6)==struct.pack('>HI',0x4eb9 if site['op']=='jsr' else 0x4ef9,address(site['target']))
    # Complete stock entry prefixes/replays retain the native register masks.
    for name,head,target in [
        ('trig','4fefffc848d77cfc',0x4008b88a),
        ('header_load','4feffff448d70c04',0x400a79d4),
        ('header_reset','206f00044a88',0x400b36d4)]:
        assert read(address('core_stock_'+name),len(bytes.fromhex(head))+6)==bytes.fromhex(head)+struct.pack('>HI',0x4ef9,target)
    for name,prefix,target in [
        ('sound_set','4e56ffd848d704fc',0x4001330c),
        ('machine_select','4fefffd848d77c7c',0x4002559c)]:
        at=address('core_native_'+name);body=read(at,110)   # 0.12.15 LFO3 store check precedes
        assert bytes.fromhex(prefix)+struct.pack('>HI',0x4ef9,target) in body
    # Both shared native kernels use the exact imported guard logic, while
    # their external PC-relative dependencies are assembled as local bridges.
    imported=json.loads((mod/'imported/provenance.json').read_text())
    assert imported['stock_sha256']=='8e2488f462c4a5656396a895f113bcd415e9900fa8709340dccf45d4cb9ed19e'
    assert hashlib.sha256((mod/'imported/kernels.s').read_bytes()).hexdigest()==imported['kernel_sha256']
    work=folder/'synfact-machines.work';e=elf.Elf.load(work/'main.elf');section=e.section('.image')
    sy={s.name:s for s in e.symbols if s.name}
    linked={n:s.value for n,s in sy.items()}
    kernel_layout(linked)
    # Negative witnesses: GAS's former 4-byte local JMP shortened these
    # replacement blocks by 2 bytes. Every such layout must be rejected.
    for label in ['poly_native_continue','poly_mod_first_continue','poly_mod_second_continue']:
        broken=dict(linked);broken[label]-=2
        try:kernel_layout(broken)
        except AssertionError:pass
        else:raise AssertionError('accepted relaxed native kernel: '+label)
    def bank_read(name,size):return section.data[sy[name].value:sy[name].value+size]
    # Compare every imported instruction byte against stock's preserved body,
    # replacing only the three guards and external PC-relative dependencies.
    native=bytearray(read(0x40119266,0x326));native[:8]=bytes.fromhex('4fefffc048d77cfc')
    native[26:34]=struct.pack('>HIH',0x4ef9,linked['poly_native_lfo_guard'],0x4e71)
    native[0x240:0x244]=struct.pack('>Hh',0x4eba,linked['poly_random_bridge']-(linked['poly_native_shared']+0x242))
    assert bank_read('poly_native_shared',len(native))==native
    modulation=bytearray(read(0x40097afc,0x11e));modulation[:8]=bytes.fromhex('4fefffc448d77cfc')
    for off,target in [(0x28,'poly_mod_first_guard'),(0x62,'poly_mod_second_guard')]:
        modulation[off:off+10]=struct.pack('>HIHH',0x4ef9,linked[target],0x4e71,0x4e71)
    for off in [0x24,0x32]:
        modulation[off+2:off+4]=struct.pack('>h',linked['poly_mod_helper_bridge']-(linked['poly_mod_shared']+off+2))
    assert bank_read('poly_mod_shared',len(modulation))==modulation
    for name in ['poly_native_shared','poly_mod_shared','poly_native_lfo_guard','poly_fast_publish','si_tick','si_draw']:
        assert sy[name].shndx==section.idx and sy[name].value<report['main_bytes']
    for name in ['main3-1.o','main3-2.o']:
        obj=elf.Elf.load(work/name);assert audit(obj.section('.text').data)>0
    if synfact:
        p=json.loads((mod/'provenance.json').read_text())
        for name,digest in p['source_sha256'].items():assert hashlib.sha256((synfact/name).read_bytes()).hexdigest()==digest,name
        # Git worktrees may use CRLF; assembled instructions must stay identical.
        assert (mod/'publication.s').read_text()==(synfact/'src/features/lfo3/poly/publication.s').read_text()
        guard=(synfact/'src/features/lfo3/poly/guards.s').read_text(encoding='utf-8')
        fragment=guard[guard.index('poly_mod_first_guard:'):guard.index('poly_native_dispatch:')]+guard[guard.index('poly_native_lfo_guard:'):]
        assert fragment.replace('poly_fast_owners','poly_lfo_map') in (mod/'imported/kernels.s').read_text(encoding='utf-8')
    print('PASS: POLY source provenance; 6 existing/new shared subscriptions; 12 core-owned native bridges; exact replay ABI; host API 2; telemetry moved to SMZ; imported optimized guards/publication; -O3')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);p.add_argument('--synfact',type=Path)
    a=p.parse_args();verify(a.folder,a.synfact)
