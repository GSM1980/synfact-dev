"""Exact known bank compositions, including the sampler-owned input module."""
import struct
import hashlib
from pathlib import Path


def check_bank_machines(report, raw, baseline=(66,67,68,69,65)):
    """Require the normal machines and explicitly supported optional additions.

    Verify the binary directory too: adding a synth must not allow a missing
    sampler, duplicate type, reordered baseline or invented report-only machine.
    """
    expected=list(baseline)
    digimono=[Path(p) for p in report['source_sha256']
              if Path(p).as_posix().endswith('/mods/digimono-st/mod.json')]
    if digimono:
        assert len(digimono)==1
        path=digimono[0]
        assert hashlib.sha256(path.read_bytes()).hexdigest()==report['source_sha256'][str(path)]
        expected.append(71)
    assert report['machines']==expected, ('unexpected machine composition/order',report['machines'],expected)
    image=80+4*struct.unpack_from('>I',raw,56)[0]
    directory,count=struct.unpack_from('>II',raw,40)
    assert count==len(expected)
    assert [struct.unpack_from('>I',raw,image+directory+40*i)[0] for i in range(count)]==expected


def object_for_symbol(report, symbol):
    """Resolve a pinned input object by its definition, independent of folder order."""
    from elekloader.sdk import elf
    found=[]
    for name,digest in report['object_sha256'].items():
        path=Path(name)
        if not path.name.startswith('main') or path.suffix!='.o':continue
        obj=elf.Elf.load(path)
        if any(s.name==symbol and s.shndx for s in obj.symbols):
            assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
            found.append(obj)
    assert len(found)==1,(symbol,len(found))
    return found[0]


def check_bank_modules(report, raw, allowed):
    """Keep order/count checks; accept the new sampler prefix and legacy banks.

    A present sampler module must carry its real init and guarded ENC/KEY
    subscriptions in the file. Other module contents retain their own audits.
    """
    modules=report['modules']
    expected=[list(names) for names in allowed]
    expected += [['sp_sampler_input_module',*names] for names in allowed]
    # samplers-st 1.3: its stereo-track module follows the input module
    expected += [['sp_sampler_input_module','st_stereo_module',*names] for names in allowed]
    # Digi Mono 0.1.2: its UI-only destination provider follows FM's module.
    expected += [names[:names.index('fm_tone_module')+1]+['dm_destination_module']+
                 names[names.index('fm_tone_module')+1:] for names in list(expected) if 'fm_tone_module' in names]
    expected += [names+['sample_manager_module'] for names in list(expected)]
    # optional modules after them, in bank order: the chorus, then modulation skip
    # (docs/SYNTAKT-BUILD.md puts modulation skip last in every bank)
    expected += [names+['chorus_module'] for names in list(expected)]
    expected += [names+['modulation_skip_module'] for names in list(expected)]
    assert modules in expected, ('unexpected module composition/order',modules)
    directory,count=struct.unpack_from('>II',raw,32)
    assert count==len(modules), 'module directory/report count differs'
    if 'dm_destination_module' in modules:
        assert any(Path(name).as_posix().endswith('/mods/digimono-st/main.c')
                   for name in report['source_sha256']), 'Digi Mono provider source not pinned'
        image=80+4*struct.unpack_from('>I',raw,56)[0]
        at=image+directory+16*modules.index('dm_destination_module')
        name,init,subs,n=struct.unpack_from('>4I',raw,at)
        assert raw[image+name:raw.index(0,image+name)]==b'MONO DESTINATIONS'
        assert init<report['main_bytes'] and not init&1 and subs==0xffffffff and n==0
    if 'sp_sampler_input_module' not in modules:return
    assert any(Path(name).as_posix().endswith('/mods/samplers-st/input.c')
               for name in report['source_sha256']), 'sampler input source not pinned'
    image=80+4*struct.unpack_from('>I',raw,56)[0]
    name,init,subs,n=struct.unpack_from('>4I',raw,image+directory)
    assert name<report['main_bytes'] and init<report['main_bytes'] and not init&1
    assert raw[image+name:raw.index(0,image+name)] in (b'SP PLAY INPUT',b'SP DISCRETE INPUT')
    browser=any(Path(path).as_posix().endswith('/mods/samplers-st/browser.inc')
                for path in report['source_sha256'])
    expected_events=[0x103,0x102,0x302] if browser else [0x103,0x102]
    assert n==len(expected_events) and subs+8*n<=report['main_bytes']
    rows=[struct.unpack_from('>II',raw,image+subs+8*i) for i in range(n)]
    assert [event for event,fn in rows]==expected_events, 'sampler view ENC/KEY registrations differ from its source profile'
    assert all(not fn&1 and fn<report['main_bytes'] for event,fn in rows)
