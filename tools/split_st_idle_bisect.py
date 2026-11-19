"""Split a locally built R diagnostic without rebuilding or moving code.

Requires its original source/object hashes and ELF, not an arbitrary input
bank. T/U split LFO3/ARP from the other services. The lfo set separates
LFO3 from ARP and then its native callback from the installed global hooks.
All use the already authored no-op/zero-interest initializer.
"""
import argparse
import hashlib
import json
import shutil
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tools')]
from build_st_idle_bisect import disable,KEEP
from elekloader.sdk import elf

PAIR={'lfo3_module','arp_module'}
SCENARIOS={
    'T-sin-LFO3-ARP':('without-lfo3-arp',PAIR,{}),
    'U-solo-LFO3-ARP':('only-lfo3-arp',None,{}),
}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--reference-build',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--set',choices=('groups','lfo'),default='groups')
    a=p.parse_args()
    assert not a.out.exists(),'use a fresh output directory'
    reference=a.reference_build/'R-completo.smz4'
    report=json.loads(reference.with_suffix('.smz4.json').read_text())
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    assert sha(reference)==report['sha256']
    assert 'diagnostic' not in report,'reference must retain all modules'
    assert PAIR|KEEP<=set(report['modules'])
    for group in ('source_sha256','object_sha256'):
        for name,digest in report[group].items():
            assert sha(Path(name))==digest,name
    executable=reference.with_suffix('.work')/'main.elf'
    symbols={s.name:s.value for s in elf.Elf.load(executable).symbols if s.name and s.bind==1}
    raw=reference.read_bytes()
    a.out.mkdir(parents=True)
    results=[]
    scenarios=SCENARIOS
    if a.set=='lfo':
        other=set(report['modules'])-KEEP-{'lfo3_module'}
        scenarios={
            'V-solo-LFO3':('only-lfo3',other,{}),
            'W-LFO3-sin-callback-nativo':('lfo3-without-native-callback',other,{'lfo3_module':{11}}),
        }
    for name,(scenario,disabled,callbacks_only) in scenarios.items():
        if disabled is None:disabled=set(report['modules'])-KEEP-PAIR
        result,changes=disable(raw,report,symbols,disabled,callbacks_only)
        target=a.out/(name+'.smz4')
        target.write_bytes(result)
        diagnostic=dict(scenario=scenario,reference_sha256=report['sha256'],
                        disabled_modules=sorted(disabled),changes=changes,
                        disabled_callbacks={k:sorted(v) for k,v in callbacks_only.items()},
                        executable_bytes_unchanged=True,relocations_unchanged=True,
                        AUDIO_unchanged=True,stock_pattern_only=True)
        copied=dict(report,sha256=sha(target),diagnostic=diagnostic)
        target.with_suffix('.smz4.json').write_text(json.dumps(copied,indent=2)+'\n')
        target.with_suffix('.work').mkdir()
        shutil.copyfile(executable,target.with_suffix('.work')/'main.elf')
        results.append(dict(file=target.name,sha256=copied['sha256'],**diagnostic))
    summary=dict(ok=True,reference_sha256=report['sha256'],variants=results,
                 new_firmware_optimization=False,device_access=False)
    (a.out/'split.json').write_text(json.dumps(summary,indent=2)+'\n')
    print('Generated',a.set,'from unchanged, source-verified R:',report['sha256'])
    for result in results:print(result['file'],result['disabled_modules'])


if __name__=='__main__':main()
