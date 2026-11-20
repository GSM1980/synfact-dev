"""Exact known bank compositions, including the sampler-owned input module."""
import struct
from pathlib import Path


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
    expected += [names+['sample_manager_module'] for names in list(expected)]
    # Optional modules in bank order: Chorus, Playhead, then Modulation Skip.
    # (docs/SYNTAKT-BUILD.md puts modulation skip last in every bank)
    expected += [names+['chorus_module'] for names in list(expected)]
    expected += [names+['comp_module'] for names in list(expected)]
    expected += [names+['playhead_module'] for names in list(expected)]
    # mix-fast-st and icache-lock-st (experimental, no UI): before modulation skip.
    expected += [names+['mix_fast_module'] for names in list(expected)]
    expected += [names+['icache_lock_module'] for names in list(expected)]
    expected += [names+['modulation_skip_module'] for names in list(expected)]
    # Playhead 1.2's modal must receive global KEY/ENC before the other UI
    # modules. Machine-owned view callbacks retain their original prefix.
    prefix={'sp_sampler_input_module','st_stereo_module','fm_tone_module'}
    for names in list(expected):
        if 'playhead_module' not in names:continue
        reordered=[name for name in names if name!='playhead_module']
        at=next((i for i,name in enumerate(reordered) if name not in prefix),len(reordered))
        reordered.insert(at,'playhead_module')
        expected.append(reordered)
    assert modules in expected, ('unexpected module composition/order',modules)
    directory,count=struct.unpack_from('>II',raw,32)
    assert count==len(modules), 'module directory/report count differs'
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
