"""Independently compile and validate a catalog's permitted SMZ combinations.

Runs the SDK reference builder and core parser for each selection, then compares
the browser candidate's linked images to the reference. Requires the same
toolchain environment as docs/SYNTAKT-BUILD.md. Outputs are local/private.
"""
import argparse, concurrent.futures, hashlib, itertools, json, os, subprocess, sys, shutil, threading, re
from pathlib import Path
from smz_approve import verify

ROOT=Path(__file__).resolve().parents[2]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--catalog',type=Path,required=True)
    p.add_argument('--config',type=Path,required=True,help='JSON modules with id, path, kind, requires, conflicts, always')
    p.add_argument('--source-root',type=Path,required=True)
    p.add_argument('--parser',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True,help='new local directory')
    p.add_argument('--jobs',type=int,default=2)
    p.add_argument('--donor',type=Path,action='append',default=[],help="the OS file a module's donor recipe reads (FM TONE: the Digitone OS 1.44)")
    p.add_argument('--audited-bank',type=Path,help='reuse only verified identical compiler objects; every selection is freshly linked/audited by the SDK')
    p.add_argument('--discard-successful-builds',action='store_true',help='retain approval metadata instead of gigabytes of private successful firmware')
    p.add_argument('--resume',type=Path,help='resume approvals only from a run with exactly the same catalog and config hashes')
    p.add_argument('--only-id',action='append',default=[],
                   help='validate only combinations containing this module (for an incremental run); repeat for several')
    a=p.parse_args()
    if not 1<=a.jobs<=8:raise ValueError('jobs must be 1..8')
    catalog=json.loads(a.catalog.read_text(encoding='utf-8'));config=json.loads(a.config.read_text(encoding='utf-8'))
    mods=config['modules'];ids=[m['id'] for m in mods]
    if ids!=[m['id'] for m in catalog['modules']] or len(ids)>16 or len(set(ids))!=len(ids):
        raise ValueError('Config must list the catalog IDs in order, at most 16')
    if not set(a.only_id)<=set(ids):raise ValueError('Unknown --only-id')
    source=a.source_root.resolve()
    for m in mods:
        if m['kind'] not in ('machine','module') or not set(m.get('requires',[])+m.get('conflicts',[]))<=set(ids)                 or not isinstance(m.get('always',False),bool):
            raise ValueError('Invalid module configuration')
        path=(source/m['path']).resolve()
        if not path.is_relative_to(source) or not (path/'mod.json').is_file():raise ValueError('Invalid source path')
    always={m['id'] for m in mods if m.get('always')}
    allowed=[]
    for bits in itertools.product((False,True),repeat=len(mods)):
        chosen=[m for m,b in zip(mods,bits) if b];names={m['id'] for m in chosen}
        if chosen and always<=names and (not a.only_id or set(a.only_id)&names) and all(set(m.get('requires',[]))<=names and not set(m.get('conflicts',[]))&names for m in chosen):allowed.append(chosen)
    a.out.mkdir(parents=True,exist_ok=False)
    parser=a.parser.resolve(); catalog_path=a.catalog.resolve();tool=ROOT/'js/tools/smz_candidate.ts'
    donor=[x for f in a.donor for x in ('--donor',str(f.resolve()))]
    provenance={'catalogSha256':hashlib.sha256(a.catalog.read_bytes()).hexdigest(),'configSha256':hashlib.sha256(a.config.read_bytes()).hexdigest()}
    (a.out/'run.json').write_text(json.dumps(provenance,indent=2)+'\n')
    resumed=[]
    if a.resume:
        if json.loads((a.resume/'run.json').read_text())!=provenance:raise ValueError('Resume inputs differ')
        permitted={tuple(m['id'] for m in chosen) for chosen in allowed};seen=set()
        for path in sorted(a.resume.glob('*/approval.json')):
            try:entry=json.loads(path.read_text())
            except (ValueError,OSError):continue  # an interrupted write is not an approval
            ids=tuple(entry.get('ids',[]))
            if ids not in permitted or ids in seen or any(not re.fullmatch(r'[0-9a-f]{64}',entry.get(k,'')) for k in ('sha256','referenceSha256')):raise ValueError('Invalid resume approval')
            seen.add(ids);resumed.append(entry)
        allowed=[chosen for chosen in allowed if tuple(m['id'] for m in chosen) not in seen]
    total=len(allowed)+len(resumed)
    local=threading.local();services=[];service_lock=threading.Lock()
    def persistent(stage,cmd):
        attr='reference' if stage=='reference-build' else 'candidate'
        service=getattr(local,attr,None)
        if service is None:
            if attr=='reference':
                command=[sys.executable,str(ROOT/'js/tools/smz_cached_reference.py'),'--audited-bank',str(a.audited_bank.resolve()),'--config',str(a.config.resolve()),'--source-root',str(source),'--serve']
            else:command=['node',str(ROOT/'js/tools/smz_candidate_server.ts'),str(catalog_path),*donor]
            log=(a.out/(attr+'-'+str(threading.get_ident())+'.log')).open('w',encoding='utf-8')
            service=subprocess.Popen(command,cwd=ROOT,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,text=True,encoding='utf-8',bufsize=1)
            if service.stdout.readline().strip()!='READY':raise ValueError('Matrix worker did not start: '+attr)
            setattr(local,attr,service)
            with service_lock:services.append((service,log))
        output=cmd[cmd.index('--out')+1] if attr=='reference' else cmd[3]
        selected=cmd[cmd.index('--ids')+1:] if attr=='reference' else local.current_ids
        service.stdin.write(json.dumps({'out':output,'ids':selected})+'\n');service.stdin.flush()
        line=service.stdout.readline()
        if not line:raise ValueError('Matrix worker terminated: '+attr)
        reply=json.loads(line)
        return subprocess.CompletedProcess(cmd,0 if reply['ok'] else 1,reply.get('error','Verified persistent '+attr))
    def run_one(pair):
        index,selected=pair;names=[m['id'] for m in selected]
        folder=a.out.resolve()/f'{index:05d}';folder.mkdir()
        ref=folder/'reference.smz4';candidate=folder/'candidate.smz4'
        command=[sys.executable,'-m','elekloader.sdk.syntakt.smz']+[str(source/m['path']) for m in selected if m['kind']=='machine']
        for m in selected:
            if m['kind']=='module':command+=['--module',str(source/m['path'])]
        command+=['--out',str(ref)]
        if a.audited_bank:
            command=[sys.executable,str(ROOT/'js/tools/smz_cached_reference.py'),'--audited-bank',str(a.audited_bank.resolve()),'--config',str(a.config.resolve()),'--source-root',str(source),'--out',str(ref),'--ids',*names]
        logs=[];local.current_ids=names
        for stage,cmd,cwd in [('reference-build',command,source),('reference-parser',[str(parser),str(ref)],ROOT),
                              ('candidate-build',['node',str(tool),str(catalog_path),str(candidate),*donor,*names],ROOT),
                              ('candidate-parser',[str(parser),str(candidate)],ROOT)]:
            result=persistent(stage,cmd) if a.audited_bank and stage in ('reference-build','candidate-build') else subprocess.run(cmd,cwd=cwd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
            logs.append(result.stdout)
            if result.returncode:
                (folder/'failure.log').write_text('\n'.join(logs),encoding='utf-8')
                return {'ids':names,'stage':stage,'log':str(folder / 'failure.log')}
        reference=ref.read_bytes();out=candidate.read_bytes();report=json.loads(Path(str(ref)+'.json').read_text())
        if hashlib.sha256(reference).hexdigest()!=report['sha256'] or not report['instruction_audits'] or not report.get('object_sha256'):
            raise ValueError(f'{names}: reference report invalid')
        for path,digest in report['object_sha256'].items():
            if hashlib.sha256(Path(path).read_bytes()).hexdigest()!=digest:raise ValueError(f'{names}: object changed: {path}')
        for path,digest in report['source_sha256'].items():
            rel=Path(path).resolve().relative_to(source).as_posix()
            if catalog['sourceSha256'].get(rel)!=digest:raise ValueError(f'{names}: source changed: {rel}')
        try: differences=verify(reference,out)
        except ValueError as exc:
            (folder/'failure.log').write_text(str(exc),encoding='utf-8')
            return {'ids':names,'stage':'comparison','log':str(folder/'failure.log')}
        entry={'ids':names,'sha256':hashlib.sha256(out).hexdigest(),'referenceSha256':report['sha256'],
                'equivalentStringRelocations':differences}
        if a.discard_successful_builds:
            # This folder was created above for this selection, under a new matrix root.
            # Keep rejected build directories intact for investigation.
            resolved=folder.resolve();matrix_root=a.out.resolve()
            if resolved.parent!=matrix_root or not resolved.is_relative_to(matrix_root):raise ValueError('Unsafe matrix cleanup path')
            shutil.rmtree(resolved)
            resolved.mkdir();(resolved/'approval.json').write_text(json.dumps(entry,indent=2)+'\n',encoding='utf-8')
        return entry
    print(f'Validating {total} selections; {len(resumed)} resumed',flush=True)
    approved=list(resumed);rejected=[]
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        for entry in pool.map(run_one,enumerate(allowed)):
            (rejected if 'stage' in entry else approved).append(entry)
            done=len(approved)+len(rejected)
            if done%10==0 or done==total:print(f'{done}/{total} checked; {len(approved)} validated, {len(rejected)} rejected',flush=True)
    for service,log in services:
        service.stdin.close();service.wait(timeout=30);log.close()
    catalog['approved']=approved
    (a.out/'rejected.json').write_text(json.dumps(rejected,indent=2)+'\n',encoding='utf-8')
    result=(json.dumps(catalog,indent=2)+'\n').encode()
    # Place catalog beside its pinned artifacts; use a new name and never overwrite.
    target=a.catalog.parent/'catalog-validated.json'
    with target.open('xb') as f:f.write(result)
    print('Validated catalog:',target,'SHA-256:',hashlib.sha256(result).hexdigest(),flush=True)
    if rejected:
        print('Some selections were rejected; inspect rejected.json and failure logs.',file=sys.stderr)
        sys.exit(1)

if __name__=='__main__':main()
