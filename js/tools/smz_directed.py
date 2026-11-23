"""Directed native comparisons for a runtime-parser catalog.

Exercises every optional group alone, every pair with dependency closure,
the full bank, full-minus-one cases and specified high-interaction triples.
Each candidate executes the pinned wasm parser. A separately compiled native
copy of the C parser checks both files, then the native/browser bytes compare.
This does not prove hardware behavior or every higher-order interaction.
"""
import argparse
import concurrent.futures
import ctypes
import hashlib
import itertools
import json
from pathlib import Path
import subprocess
import sys
import threading
import time
from smz_approve import verify,verify_selection

ROOT=Path(__file__).resolve().parents[2]
sha=lambda raw:hashlib.sha256(raw).hexdigest()

def cases_for(modules):
 order=[m['id'] for m in modules];rules={m['id']:m for m in modules}
 always={m['id'] for m in modules if m.get('always')};optional=[i for i in order if i not in always]
 cases={}
 def add(ids,label):
  selected=set(ids)|always
  changed=True
  while changed:
   before=len(selected)
   for i in list(selected):selected.update(rules[i].get('requires',[]))
   changed=len(selected)!=before
  if any(set(rules[i].get('conflicts',[]))&selected for i in selected):return
  key=tuple(i for i in order if i in selected)
  cases.setdefault(key,[]).append(label)
 add([], 'internal modules only');add(optional,'full bank')
 for i in optional:add([i],'individual:'+i)
 for left,right in itertools.combinations(optional,2):add([left,right],'pair:'+left+'+'+right)
 for removed in optional:
  selected=set(optional)-{removed}
  while True:
   next_ids={i for i in selected if set(rules[i].get('requires',[]))<=selected|always}
   if next_ids==selected:break
   selected=next_ids
  add(selected,'full-minus:'+removed)
 for ids in [('samplers-st','recorder-st','sample-manager-st'),('md-st','digimono-st','comp-st'),
             ('chorus-st','comp-st','lfo3-st'),('arp-st','poly-st','playhead-st')]:
  if set(ids)<=set(order):add(ids,'interaction:'+'+'.join(ids))
 return cases

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--catalog',type=Path,required=True)
 p.add_argument('--config',type=Path,required=True)
 p.add_argument('--audited-bank',type=Path,required=True)
 p.add_argument('--native-parser',type=Path,required=True)
 p.add_argument('--donor',type=Path,action='append',default=[])
 p.add_argument('--jobs',type=int,default=4)
 p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 if not 1<=a.jobs<=8:raise ValueError('jobs must be 1..8')
 started=time.monotonic();catalog=json.loads(a.catalog.read_text(encoding='utf-8'))
 config=json.loads(a.config.read_text(encoding='utf-8'))['modules']
 if catalog['schema']!=4 or [m['id'] for m in config]!=[m['id'] for m in catalog['modules']]:raise ValueError('Runtime catalog/config required')
 def check_sources():
  for name,pin in catalog['sourceSha256'].items():
   if sha((ROOT/name).read_bytes())!=pin:raise ValueError('Source changed: '+name)
 check_sources()
 artifacts={}
 for pin in catalog['modules']:
  raw=(a.catalog.parent/pin['file']).read_bytes()
  if sha(raw)!=pin['sha256']:raise ValueError('Artifact changed: '+pin['id'])
  artifacts[pin['id']]=json.loads(raw)
 parser=ctypes.CDLL(str(a.native_parser.resolve()))
 parser.synfact_smz_validate.argtypes=[ctypes.c_void_p,ctypes.c_uint32]
 parser.synfact_smz_validate.restype=ctypes.c_int
 parser.synfact_smz_contract.restype=ctypes.c_uint32
 if parser.synfact_smz_api()!=1 or parser.synfact_smz_contract()!=catalog['validation']['contract']:raise ValueError('Wrong native core parser')
 parser_lock=threading.Lock()
 def validate(raw):
  data=ctypes.create_string_buffer(raw)
  with parser_lock:
   if parser.synfact_smz_validate(data,len(raw))!=1:raise ValueError('Native core parser rejected a bank')
 a.out.mkdir(parents=True,exist_ok=False)
 local=threading.local();services=[];service_lock=threading.Lock()
 donor_args=[x for path in a.donor for x in ('--donor',str(path.resolve()))]
 def request(kind,out,ids):
  service=getattr(local,kind,None)
  if service is None:
   command=([sys.executable,str(ROOT/'js/tools/smz_cached_reference.py'),'--audited-bank',str(a.audited_bank.resolve()),
             '--config',str(a.config.resolve()),'--source-root',str(ROOT),'--serve'] if kind=='native' else
            ['node',str(ROOT/'js/tools/smz_candidate_server.ts'),str(a.catalog.resolve()),*donor_args])
   log=(a.out/(kind+'-'+str(threading.get_ident())+'.log')).open('w',encoding='utf-8')
   service=subprocess.Popen(command,cwd=ROOT,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log,text=True,encoding='utf-8',bufsize=1)
   if service.stdout.readline().strip()!='READY':raise ValueError(kind+' worker did not start')
   setattr(local,kind,service)
   with service_lock:services.append((service,log))
  service.stdin.write(json.dumps({'out':str(out.resolve()),'ids':list(ids)})+'\n');service.stdin.flush()
  line=service.stdout.readline()
  if not line:raise ValueError(kind+' worker terminated')
  reply=json.loads(line)
  if not reply['ok']:raise ValueError(reply.get('error',kind+' failed'))
 cases=cases_for(config)
 print('Directed validation:',len(cases),'distinct banks; all singles/pairs and full-bank boundaries',flush=True)
 def run_one(pair):
  index,(ids,labels)=pair;folder=a.out/f'{index:03d}';folder.mkdir()
  ref=folder/'reference.smz4';candidate=folder/'candidate.smz4'
  request('native',ref,ids);request('browser',candidate,ids)
  reference=ref.read_bytes();built=candidate.read_bytes();report=json.loads(Path(str(ref)+'.json').read_text())
  if sha(reference)!=report['sha256'] or not report['instruction_audits']:raise ValueError('Native report does not verify')
  for path,pin in report['object_sha256'].items():
   if sha(Path(path).read_bytes())!=pin:raise ValueError('Native object changed')
  for path,pin in report['source_sha256'].items():
   if catalog['sourceSha256'].get(Path(path).resolve().relative_to(ROOT).as_posix())!=pin:raise ValueError('Native sources differ')
  verify_selection([artifacts[i] for i in ids],report)
  validate(reference);validate(built);differences=verify(reference,built)
  entry={'ids':list(ids),'sha256':sha(built),'referenceSha256':sha(reference),'equivalentStringRelocations':differences}
  (folder/'approval.json').write_text(json.dumps(dict(entry,coverage=labels),indent=2)+'\n',encoding='utf-8')
  return entry
 results=[]
 try:
  with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
   for entry in pool.map(run_one,enumerate(cases.items())):
    results.append(entry)
    if len(results)%10==0 or len(results)==len(cases):print(f'{len(results)}/{len(cases)} native comparisons passed',flush=True)
 finally:
  for service,log in services:
   service.stdin.close();service.wait(timeout=30);log.close()
 check_sources()
 catalog['approved']=results
 target=a.catalog.parent/'catalog-directed.json'
 with target.open('x',encoding='utf-8',newline='\n') as f:f.write(json.dumps(catalog,indent=2)+'\n')
 report={'ok':True,'nativeComparisons':len(results),'optionalGroups':sum(not m.get('always',False) for m in config),
         'coverage':{','.join(ids):labels for ids,labels in cases.items()},'catalogSha256':sha(target.read_bytes()),
         'elapsedSeconds':round(time.monotonic()-started,2),'deviceAccess':False,
         'limits':['Not exhaustive across all higher-order combinations.','Parser validation is structural and does not establish musical behavior, CPU capacity or hardware safety.']}
 (a.out/'summary.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
 print(json.dumps({k:v for k,v in report.items() if k!='coverage'}),flush=True)

if __name__=='__main__':main()
