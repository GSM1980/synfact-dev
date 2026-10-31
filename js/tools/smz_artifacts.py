"""Export audited SMZ objects from one complete SDK build for browser linking.

This prepares local experimental artifacts, not a release or safety approval.
The directory order must be identical to the build. Never commit generated files.
"""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from elekloader.sdk.elf import Elf

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bank',type=Path,required=True)
    p.add_argument('--machine',action='append',default=[])
    p.add_argument('--module',action='append',default=[])
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    raw=a.bank.read_bytes()
    report=json.loads(Path(str(a.bank)+'.json').read_text())
    if report['sha256']!=hashlib.sha256(raw).hexdigest() or not report['instruction_audits']:
        raise ValueError('Bank report hash/audits do not match')
    if not report.get('object_sha256'):
        raise ValueError('Rebuild bank with object hashes before exporting')
    for name,digest in report['source_sha256'].items():
        if hashlib.sha256(Path(name).read_bytes()).hexdigest()!=digest:
            raise ValueError('A source changed since the audited build: '+name)
    work=a.bank.with_suffix('.work')
    folders=[(Path(p),False) for p in a.machine]+[(Path(p),True) for p in a.module]
    if not folders:raise ValueError('Supply the machine and module directories used in the build')
    source_root=folders[0][0].resolve().parents[1]
    if any(folder.resolve().parents[1]!=source_root for folder,_ in folders):raise ValueError('All modules must belong to one source tree')
    a.out.mkdir(parents=True,exist_ok=False)
    entries=[]
    for i,(folder,module) in enumerate(folders):
        path=folder.resolve()/'mod.json'
        if str(path) not in report['source_sha256']:raise ValueError('Directory was not part of the audited build')
        doc=json.loads(path.read_text(encoding='utf-8-sig'))
        cfg=doc['smz_module' if module else 'smz']; audio=cfg.get('audio',{})
        def convert(side,names):
            out=[]
            for idx,_ in enumerate(names):
                path=(work/f'{side}{i}-{idx}.o').resolve()
                if hashlib.sha256(path.read_bytes()).hexdigest()!=report['object_sha256'].get(str(path)):
                    raise ValueError('Object changed since the audited build: '+str(path))
                obj=Elf.load(path)
                sections=[{'name':s.name if s.flags&2 else '', 'align':max(1,s.align), 'size':s.size,
                           'flags':s.flags,'data':s.data.hex()} for s in obj.sections]
                symbols=[{'name':s.name,'value':s.value,'size':s.size,'bind':s.bind,'section':s.shndx} for s in obj.symbols]
                relocs=[{'section':sec,'offset':at,'type':typ,'symbol':sym.idx,'addend':add}
                        for sec,rs in obj.relocs.items() for at,typ,sym,add in rs]
                out.append({'sections':sections,'symbols':symbols,'relocations':relocs})
            return out
        machines=[] if module else cfg.get('machines') or [{'descriptor':cfg['descriptor'],'ui':cfg.get('ui'),
                       'update':audio['update'],'render':audio['render']}]
        item={'id':doc['id'],'version':doc['version'],'kind':'module' if module else 'machine',
              'manifestSha256':hashlib.sha256(path.read_bytes()).hexdigest(),
              'main':convert('main',doc.get('sources',[])),'audio':convert('audio',audio.get('sources',[])),
              'machines':machines,'modules':[cfg['descriptor']] if module else cfg.get('modules',[]),
              'samplePool':'sample-pool-v1' in cfg.get('features',[])}
        payload=(json.dumps(item,separators=(',',':'))+'\n').encode()
        name=f'{i:02d}-{doc["id"]}.json'
        if not re.fullmatch(r'[\w.-]+\.json',name):raise ValueError('Unsafe module id')
        (a.out/name).write_bytes(payload)
        entries.append({'id':doc['id'],'file':name,'sha256':hashlib.sha256(payload).hexdigest()})
    source_hashes={Path(path).resolve().relative_to(source_root).as_posix():digest for path,digest in report['source_sha256'].items()}
    manifest={'schema':1,'device':'syntakt','os':'1.41','modules':entries,'sourceBankSha256':report['sha256'],
              'sourceSha256':source_hashes,'approved':[]}
    payload=(json.dumps(manifest,indent=2)+'\n').encode();(a.out/'catalog.json').write_bytes(payload)
    print('Catalog SHA-256:',hashlib.sha256(payload).hexdigest())

if __name__=='__main__':main()
