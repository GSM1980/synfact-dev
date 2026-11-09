"""Offline Syntakt common-event regressions; native host and static checks only.

Requires an already built core/SYX, matching SMZ, lint.json, and i686 clang.
No target instruction execution, MIDI, device access or firmware installation.
"""
import argparse,json,subprocess,sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def verify(folder,cc,synfact=None,previous=None,audio_window=None,sampler_audio_changed=False,unicorn_python=None):
    out=folder.resolve();checks=[];log=out/'regressions.log'
    log.write_text('Syntakt common-event native/static regressions.\n')
    def run(name,args):
        r=subprocess.run([str(a) for a in args],cwd=ROOT,text=True,
                         stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        with log.open('a') as f:f.write(name+'\n'+r.stdout+'\n')
        print(name+': '+('PASS' if not r.returncode else 'FAIL'),flush=True)
        if r.returncode:raise RuntimeError(name+'\n'+r.stdout)
        checks.append({'name':name,'ok':True})
    cases={
        'common-events':['tests/test_st_common_events.c'],
        'native-events':['tests/test_st_native.c'],
        'native-resident':['tests/test_st_native_resident.c'],
        'machine-services':['tests/test_st_machine_input.c'],
        'module-merge':['tests/test_st_modules.c'],
        'registry':['tests/test_core_st_machines.c'],
        'syn-pages':['tests/test_st_syn_pages.c','mods/fm-tone-st/main.c','mods/fm-tone-st/ui.c',
                     'mods/samplers-st/main.c','mods/samplers-st/ui.c'],
        'source-view':['tests/test_st_source_view.c','mods/fm-tone-st/main.c','mods/fm-tone-st/ui.c',
                       'mods/samplers-st/main.c','mods/samplers-st/ui.c'],
        'destination-labels':['tests/test_st_destination_labels.c','mods/samplers-st/main.c','mods/fm-tone-st/main.c'],
        'sampler-format':['tests/test_st_sampler_format.c','mods/core-st/machine-ui.c',
                          'mods/samplers-st/main.c','mods/samplers-st/ui.c'],
        'sampler-ui':['tests/test_st_sampler_ui.c'],
        'sampler-browser':['tests/test_st_sampler_browser.c'],
        'slice-allocate':['tests/test_st_sampler_allocate.c'],
        'project-pool':['tests/test_st_pool.c','mods/core-st/crc.c','mods/core-st/hzfs.c'],
        'sample-manager':['tests/test_st_manager.c','mods/core-st/crc.c'],
        'audio-pool':['tests/test_st_pool_receiver.c'],
        'parameter-transport':['tests/test_st_parameters.c'],
        'sampler-audio':['tests/test_st_samplers.c',*[str(p.relative_to(ROOT)) for p in
                         sorted((ROOT/'mods/samplers-st/audio').glob('*.c'))]],
        'smz-parser':['tests/test_st_smz_extensions.c','mods/core-st/smz.c']}
    cases['hzfs-storage']=['tests/test_core_st_storage.c','mods/core-st/high-storage.c','mods/core-st/crc.c']
    # Safe boot (core 1.7.1): slot storage alone, the boot orchestration, and the
    # production loader with the production storage end to end (both on the bank).
    cases['safe-storage']=['tests/test_core_st_safe_storage.c','mods/core-st/storage.c']
    cases['smz-boot']=['tests/test_core_st_smz.c','mods/core-st/smz.c']
    cases['trial-boot']=['tests/test_core_st_trial_boot.c','mods/core-st/smz.c','mods/core-st/storage.c']
    cases['trig-pages']=['tests/test_st_trig_pages.c']
    has_recorder='recorder_module' in json.loads((out/'synfact-machines.smz4.json').read_text())['modules']
    if 'poly_module' in json.loads((out/'synfact-machines.smz4.json').read_text())['modules']:cases['poly']=['tests/test_st_poly.c']
    if has_recorder:cases['recorder']=['tests/test_st_recorder.c','mods/core-st/hzfs.c','mods/core-st/crc.c']
    if 'arp_module' in json.loads((out/'synfact-machines.smz4.json').read_text())['modules']:
        cases['arp-clock']=['tests/test_st_arp_clock.c','mods/arp-st/clock.c']
        cases['arp-runtime']=['tests/test_st_arp_runtime.c','mods/arp-st/runtime.c','mods/arp-st/selector.c','mods/arp-st/clock.c','mods/arp-st/length.c']
        cases['arp-ui']=['tests/test_st_arp_ui.c','mods/arp-st/ui.c']
    for name,sources in cases.items():
        exe=out/(name+'.exe')
        run('compile '+name,[cc,'-O2','-Wl,--large-address-aware','-I','elekloader/sdk/include',*sources,'-o',exe])
        args=[out/'synfact-machines.smz4'] if name in ('smz-parser','smz-boot','trial-boot') else []
        legacy=ROOT/'out/smz-module-0.8-final/example.smz4'
        if name=='smz-parser' and legacy.exists():args.append(legacy)
        run(name,[exe,*args])
    run('boot preimage source',[sys.executable,'tools/generate_boot_preimages.py','--check'])
    boot_exe=out/'boot-preimages.exe'
    run('compile boot preimages',[cc,'-O2','-DCORE_ST_NATIVE_TEST',
        'tests/test_core_st_boot_preimages.c','mods/core-st/boot-preimages.c','-o',boot_exe])
    run('stock hook preimages',[boot_exe,out/'main.bin'])
    for name in ('common_hooks','destination_hooks','syn_page_hook','new_project_hook'):
        run(name,[sys.executable,'tests/test_st_'+name+'.py',out])
    run('stock/native fast paths',[sys.executable,'tests/test_st_fast_paths.py',out])
    skipped=[]
    if synfact:
        run('FM source/cache/UI/ratio',[sys.executable,'tests/test_st_fm.py','--bank',
            out/'synfact-machines.smz4','--synfact',synfact,'--cc',cc])
    else:skipped.append('FM reference comparison: supply --synfact')
    if has_recorder:
        run('recorder source/ABI',[sys.executable,'tests/test_st_recorder.py',out,
            *(['--synfact',synfact] if synfact else [])])
    if 'poly_module' in json.loads((out/'synfact-machines.smz4.json').read_text())['modules']:
        run('POLY source/ABI',[sys.executable,'tests/test_st_poly.py',out,*(['--synfact',synfact] if synfact else [])])
    if 'system_info_module' in json.loads((out/'synfact-machines.smz4.json').read_text())['modules']:
        run('System info redraw/cadence',[sys.executable,'tests/test_st_system_info.py','--cc',cc])
    if 'modulation_skip_module' in json.loads((out/'synfact-machines.smz4.json').read_text())['modules']:
        run('modulation skip host logic',[sys.executable,'tests/test_st_modulation_skip.py','--cc',cc,'--work',out/'modulation-skip-tests'])
    run('folder catalogue',[sys.executable,'tests/test_st_sample_folders.py'])
    run('folder C/Python round trip',[sys.executable,'tests/test_st_sample_folders.py','--cc',cc,'--out',out/'folder-tests'])
    run('units',[sys.executable,'tests/test_units.py'])
    run('instruction audit',[sys.executable,'tests/test_st_instruction_audit.py'])
    if previous:
        new,old=(out/'audio.bin').read_bytes(),(previous/'audio.bin').read_bytes()
        if audio_window:
            # Explicit: the core's AUDIO companion changed (machine-parameter
            # transport). Only its declared runtime window may differ; every
            # scheduler/boot patch and native byte stays identical, and the
            # parameter-transport fixture above exercises the new receiver.
            # Outside it, only the absolute targets of the three existing
            # companion calls (meter entry, block exit, boot) may move.
            base=0x40000400;lo,hi=(a-base for a in audio_window)
            assert len(new)==len(old) and new!=old
            operands=set()
            for site in (0x4000094a,0x40000b04,0x40001016):
                i=site-base;assert new[i:i+2]==old[i:i+2]==b'\x4e\xb9'
                operands.update(range(i+2,i+6))
            # The base-width filter's two low-pass lookups (core 1.7.2) call the
            # companion: jsr + nop now; a previous build has them stock or as a call.
            for site in (0x40002364,0x400023e8):
                i=site-base;assert new[i:i+2]==b'\x4e\xb9' and new[i+6:i+8]==b'\x4e\x71'
                if old[i:i+8]==bytes.fromhex('d081ec8041f00c00'):operands.update(range(i,i+8))
                else:
                    assert old[i:i+2]==b'\x4e\xb9' and old[i+6:i+8]==b'\x4e\x71'
                    operands.update(range(i+2,i+6))
            assert all(new[i]==old[i] or lo<=i<hi or i in operands for i in range(len(new)))
            checks.append({'name':'AUDIO identical to previous build outside companion window %#x..%#x '
                           '(except the companion jsr sites and targets)'%audio_window,'ok':True})
        else:
            assert new==old
            checks.append({'name':'AUDIO identical to previous build','ok':True})
        oldbank=previous/'synfact-machines.smz4'
        if oldbank.exists():
            import struct
            def audio_section(path):
                raw=path.read_bytes();at,n=struct.unpack_from('>II',raw,72);return raw[48+at:48+at+n]
            new_section=audio_section(out/'synfact-machines.smz4')
            old_section=audio_section(oldbank)
            if sampler_audio_changed:
                current=json.loads((out/'synfact-machines.smz4.json').read_text())
                baseline=json.loads((previous/'synfact-machines.smz4.json').read_text())
                def audio_objects(report):
                    return {Path(name).name:digest for name,digest in report['object_sha256'].items()
                            if Path(name).name.startswith('audio') and Path(name).suffix=='.o'}
                new_objects,old_objects=audio_objects(current),audio_objects(baseline)
                assert current['machines']==baseline['machines'] and current['modules']==baseline['modules']
                assert new_section!=old_section and new_objects.keys()==old_objects.keys()
                assert new_objects['audio0-0.o']!=old_objects['audio0-0.o']
                assert all(new_objects[name]==old_objects[name] for name in new_objects if name!='audio0-0.o')
                checks.append({'name':'sampler AUDIO changed; other AUDIO objects identical','ok':True})
            else:
                assert new_section==old_section
                checks.append({'name':'FM/SP AUDIO bank identical to previous build','ok':True})
    else:skipped.append('AUDIO comparison: supply --previous-build')
    # The linked core's own safe-boot record writes, executed (core 1.7.1): the
    # path core 1.7 crashed on at its first trial boot. Needs Unicorn.
    if unicorn_python:run('safe-boot record writes (Unicorn)',[unicorn_python,'-B','tests/test_st_safe_boot_emu.py',out])
    else:skipped.append('safe-boot emulation: supply --unicorn-python (and LIBUNICORN_PATH)')
    assert json.loads((out/'lint.json').read_text())['ok']
    checks.append({'name':'lint','ok':True})
    (out/'validation.json').write_text(json.dumps({'ok':True,'checks':checks,'skipped':skipped,
        'target_execution':False,'device_transfer':False},indent=2)+'\n')
    print('ALL PASS; '+str(len(skipped))+' optional comparisons skipped; no device access',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path)
    p.add_argument('--cc',required=True,type=Path);p.add_argument('--synfact',type=Path)
    p.add_argument('--previous-build',type=Path)
    p.add_argument('--unicorn-python',type=Path,help='a Python with unicorn: runs tests/test_st_safe_boot_emu.py')
    p.add_argument('--audio-companion-changed',action='store_true',
        help='the core AUDIO companion intentionally changed: compare outside its declared window only')
    p.add_argument('--sampler-audio-changed',action='store_true',
        help='sampler DSP changed: compare all other AUDIO objects byte for byte')
    a=p.parse_args()
    window=None
    if a.audio_companion_changed:
        sys.path.insert(0,str(ROOT))
        from elekloader.devices.syntakt import DEVICE
        window=DEVICE.secondary[7][3][-1]
        assert window==(0x40040860,0x40041050)
    verify(a.folder,a.cc,a.synfact,a.previous_build,window,a.sampler_audio_changed,a.unicorn_python)
