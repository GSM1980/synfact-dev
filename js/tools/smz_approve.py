"""Validate a candidate SMZ against an independently built reference and parser.

Writes a new catalog; does not certify provenance, compatibility or hardware.
"""
import argparse, hashlib, json, struct, subprocess
from pathlib import Path

def verify(reference,candidate):
    a,b=reference,candidate
    u=lambda x,n:struct.unpack_from('>I',x,n)[0]
    if len(a)!=len(b) or len(a)<80:raise ValueError('Different bank sizes')
    fields=(4,8,12,20,24,28,32,36,40,44,48,52,56,60,64,72,76)
    if any(u(a,i)!=u(b,i) for i in fields):raise ValueError('Bank headers differ')
    n=u(a,56);start=80+4*n;size=u(a,52);end=start+size
    if end>len(a) or n>size//2:raise ValueError('Invalid MAIN bounds')
    rels=[u(a,80+4*i) for i in range(n)]
    if rels!=[u(b,80+4*i) for i in range(n)]:raise ValueError('Runtime relocation tables differ')
    if a[48+u(a,72):]!=b[48+u(b,72):]:raise ValueError('AUDIO differs')
    ma,mb=bytearray(a[start:end]),bytearray(b[start:end]);changes=0
    for r in rels:
        if r+4>size:raise ValueError('Relocation outside MAIN')
        x,y=u(ma,r),u(mb,r)
        if x==y:continue
        def text(buf,at):
            if at>=len(buf):return None
            end=buf.find(0,at)
            if end<at:return None
            s=buf[at:end]
            return bytes(s) if all(32<=v<127 for v in s) else None
        if text(ma,x) is None or text(ma,x)!=text(mb,y):raise ValueError('Non-equivalent relocation at '+hex(r))
        ma[r:r+4]=mb[r:r+4]=b'\0'*4;changes+=1
    if ma!=mb:raise ValueError('MAIN differs outside equivalent string targets')
    return changes

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--catalog',type=Path,required=True);p.add_argument('--candidate',type=Path,required=True)
    p.add_argument('--reference',type=Path,required=True);p.add_argument('--parser',type=Path,required=True)
    p.add_argument('--ids',nargs='+',required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();catalog=json.loads(a.catalog.read_text(encoding='utf-8'))
    ids=[p['id'] for p in catalog['modules'] if p['id'] in a.ids]
    if not ids or len(ids)!=len(a.ids) or len(set(a.ids))!=len(a.ids):raise ValueError('Unknown or repeated selection')
    candidate=a.candidate.read_bytes();reference=a.reference.read_bytes()
    report=json.loads(Path(str(a.reference)+'.json').read_text())
    if report['sha256']!=hashlib.sha256(reference).hexdigest() or not report['instruction_audits'] or not report.get('object_sha256'):
        raise ValueError('Reference report does not verify')
    for path,digest in report['object_sha256'].items():
        if hashlib.sha256(Path(path).read_bytes()).hexdigest()!=digest:raise ValueError('Reference object changed: '+path)
    source_root=Path(next(path for path in report['source_sha256'] if path.replace('\\','/').endswith('/elekloader/sdk/include/syntakt/machines.h'))).resolve().parents[4]
    for path,digest in report['source_sha256'].items():
        relative=Path(path).resolve().relative_to(source_root).as_posix()
        if catalog['sourceSha256'].get(relative)!=digest:raise ValueError('Reference sources differ from catalog: '+relative)
    artifacts=[]
    for pin in catalog['modules']:
        if pin['id'] not in ids:continue
        raw=(a.catalog.parent/pin['file']).read_bytes()
        if hashlib.sha256(raw).hexdigest()!=pin['sha256']:raise ValueError('Artifact hash mismatch')
        item=json.loads(raw);artifacts.append(item)
        if item['id']!=pin['id'] or item['manifestSha256'] not in report['source_sha256'].values():
            raise ValueError('Reference does not include the selected source manifest')
    modules=[m for item in artifacts for m in item['modules']]
    if modules!=report['modules'] or sum(len(item['machines']) for item in artifacts)!=len(report['machines']):
        raise ValueError('Reference contains a different module or machine selection')
    changes=verify(reference,candidate)
    for file in (a.reference,a.candidate):subprocess.run([str(a.parser),str(file.resolve())],check=True)
    entry={'ids':ids,'sha256':hashlib.sha256(candidate).hexdigest(),
           'referenceSha256':report['sha256'],'equivalentStringRelocations':changes}
    if any(x['ids']==ids for x in catalog['approved']):raise ValueError('Selection already present')
    catalog['approved'].append(entry)
    raw=(json.dumps(catalog,indent=2)+'\n').encode()
    with a.out.open('xb') as f:f.write(raw)
    print('Catalog SHA-256:',hashlib.sha256(raw).hexdigest())

if __name__=='__main__':main()
