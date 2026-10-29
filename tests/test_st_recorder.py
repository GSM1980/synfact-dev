"""Recorder source/ABI/linked-module checks. No target execution or device IO."""
import argparse,hashlib,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader.sdk import elf
from elekloader.sdk.syntakt.audio import audit
from st_bank_modules import check_bank_modules

ROOT=Path(__file__).resolve().parents[1]

def verify(folder,synfact=None):
    folder=folder.resolve();mod=ROOT/'mods/recorder-st'
    bank=folder/'synfact-machines.smz4';raw=bank.read_bytes()
    report=json.loads(bank.with_suffix('.smz4.json').read_text())
    check_bank_modules(report,raw,(
        ['fm_tone_module','recorder_module'],
        ['fm_tone_module','recorder_module','poly_module','system_info_module'],
        ['fm_tone_module','recorder_module','lfo3_module','poly_module','system_info_module']))
    assert report['machines']==[66,67,68,69,65] and not report['firmware_required']
    for name,digest in report['source_sha256'].items():
        assert Path(name).is_relative_to(ROOT) and hashlib.sha256(Path(name).read_bytes()).hexdigest()==digest,name
    for name in ('main.c','recorder.c','save.c','trim.c','capture.c','normalize.c','native.h','irq.s'):
        assert str((mod/name).resolve()) in report['source_sha256']
    for name in ('host.h','mix.h'):
        assert str((ROOT/'elekloader/sdk/include/syntakt'/name).resolve()) in report['source_sha256']
    manifest=json.loads((mod/'mod.json').read_text())
    assert not any(manifest.get(x) for x in ('sites','subscribe','contribute','collections','fixed'))
    image=80+4*struct.unpack_from('>I',raw,56)[0]
    directory,count=struct.unpack_from('>II',raw,32);assert count==len(report['modules'])
    recorder_index=report['modules'].index('recorder_module')
    name,init,subs,n=struct.unpack_from('>4I',raw,image+directory+16*recorder_index)
    assert raw[image+name:raw.index(0,image+name)]==b'AUDIO RECORDER' and n==7
    rows=[struct.unpack_from('>II',raw,image+subs+8*i) for i in range(n)]
    assert [x[0] for x in rows]==[2,3,0,0x101,10,8,9]
    assert all(not(fn&1) and fn<report['main_bytes'] for event,fn in rows)
    obj=elf.Elf.load(folder/'synfact-machines.work/main2-1.o');code=obj.section('.text').data
    assert code==bytes.fromhex('700040c046fc27004e75202f000446c04e75') and audit(code)>0
    assert all(flag in report['cflags'] for flag in ('-O3','-mcpu=54415','-mtune=cfv4'))
    # The new resident service table refers to existing operations only.
    r=json.loads((folder/'verification.json').read_text());s=r['symbols'];layout=r['link']
    main=(folder/'main.bin').read_bytes()
    def read(at,n):
        if layout['ddr'][0]<=at<layout['ddr'][1]:at=layout['run_load']+at-layout['ddr'][0]
        return main[at-0x40000400:at-0x40000400+n]
    host=s['core:core_machine_host_api'];ui=s['core:core_machine_ui_api']
    assert struct.unpack('>I',read(ui,4))[0]==7 and struct.unpack('>I',read(ui+40,4))[0]==host
    names=('core_hzfs_native_io','core_hzfs_open','core_hzfs_close','core_crc_update',
           'core_pool_read','core_pool_write','core_pool_ready','st_pool_live_sync','st_pool_preview_stop','core_event_view')
    assert read(host,44)==struct.pack('>11I',8,*[s['core:'+name] for name in names])   # host API 8, prefix retained
    assert read(s['core:core_high_written'],2)==bytes.fromhex('4e75')
    # The existing native close notification is conditional on a valid
    # written capture. The high-zone descriptor now supplies that no-op.
    assert read(0x4010b886,4)==bytes.fromhex('4aaa0098')
    if synfact:
        provenance=json.loads((mod/'provenance.json').read_text())
        for name,digest in provenance['source_sha256'].items():
            assert hashlib.sha256((synfact/name).read_bytes()).hexdigest()==digest
        assert (mod/'trim.c').read_text()==(synfact/'src/current/pool/sampling_trim.c').read_text()
        original=(synfact/'src/current/pool/sampling.c').read_text()
        port=(mod/'recorder.c').read_text()
        part=lambda t:t[t.index('void sampler_draw('):t.index('void sampler_open(')]
        assert part(original)==part(port) # same layout, strings, values and native drawing calls
        capture=(synfact/'src/features/ui-performance/headroom/runtime.c').read_text()
        assert (mod/'capture.c').read_text()==capture.replace('#define DATA __attribute__((section(".data")))\n','')
        original=(synfact/'src/features/lfo3/recorder_normalize/runtime.c').read_text()
        at=original.index('        __asm__ volatile(');end=original.index('        if(lo<sam_detail_low',at)
        assert original[at:end] in (mod/'normalize.c').read_text()
    print('PASS: source-only recorder; 7 subscriptions to existing events; exact host API bindings; unchanged Synfact draw/trim/optimized capture and ColdFire normalization loop; IRQ SR ABI; -O3; no new hooks')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);p.add_argument('--synfact',type=Path)
    a=p.parse_args();verify(a.folder,a.synfact)
