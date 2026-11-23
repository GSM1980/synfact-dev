"""SDK reference from hash-verified, freshly audited compiler objects.

Only compilation of identical sources with identical commands is cached.
The normal SDK builder, GNU links, executable audits, checksums and reports run
for every selection. No browser linker code participates in the reference.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import traceback
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from elekloader.sdk.syntakt import smz
from elekloader.sdk import build as sdk, elf
from elekloader.sdk.syntakt import audio as audit_module
from elekloader.devices.syntakt import DEVICE


def digest(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def publish(temporary,destination):
    """Windows readers can briefly hold a cache file open across replace.

    Competing workers may have already published the identical result. Only
    accept that exact content; otherwise retry the atomic rename or fail.
    """
    pin=digest(temporary)
    for attempt in range(20):
        try:temporary.replace(destination);return
        except PermissionError:
            if destination.exists() and digest(destination)==pin:
                temporary.unlink(missing_ok=True);return
            if attempt==19:raise
            time.sleep(.05)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--audited-bank',type=Path,required=True)
    p.add_argument('--config',type=Path,required=True)
    p.add_argument('--source-root',type=Path,required=True)
    p.add_argument('--out',type=Path)
    p.add_argument('--serve',action='store_true')
    p.add_argument('--ids',nargs='*',default=[]);a=p.parse_args()
    bank=a.audited_bank.resolve();report=json.loads(Path(str(bank)+'.json').read_text())
    if digest(bank)!=report['sha256'] or not report['instruction_audits']:raise ValueError('Invalid audited reference')
    for path,pin in {**report['source_sha256'],**report['object_sha256']}.items():
        if digest(path)!=pin:raise ValueError('Audited input changed: '+path)
    if report['cflags']!=DEVICE.toolchain['cflags']:raise ValueError('Compiler flags differ from audited build')
    config=json.loads(a.config.read_text())['modules']
    if set(a.ids)-{m['id'] for m in config} or len(a.ids)!=len(set(a.ids)):raise ValueError('Unknown or repeated selection')
    root=a.source_root.resolve();prefix=os.environ.get('ELEKLOADER_CROSS',DEVICE.toolchain['prefix'])
    def canonical(cmd):
        out=[str(v) for v in cmd]
        for flag in ('-o','-MF'):
            if flag in out:out[out.index(flag)+1]='<output>'
        return tuple(out)
    cache={}
    ordered=[m for m in config if m['kind']=='machine']+[m for m in config if m['kind']=='module']
    for i,m in enumerate(ordered):
        folder=(root/m['path']).resolve();manifest_path=folder/'mod.json'
        if report['source_sha256'].get(str(manifest_path))!=digest(manifest_path):raise ValueError('Manifest not in audited build')
        manifest=json.loads(manifest_path.read_text(encoding='utf-8'))
        cfg=manifest['smz' if m['kind']=='machine' else 'smz_module']
        for side,names in [('main',manifest.get('sources',[])),('audio',cfg.get('audio',{}).get('sources',[]))]:
            for k,name in enumerate(names):
                source=(folder/name).resolve();obj=bank.with_suffix('.work')/f'{side}{i}-{k}.o'
                if digest(obj)!=report['object_sha256'].get(str(obj)):raise ValueError('Cache object not audited')
                if source.suffix=='.c':
                    command=[prefix+'gcc',*DEVICE.toolchain['cflags'],'-fno-jump-tables','-I',sdk.INCLUDE,'-I',str(folder),'-MMD','-MF','<output>','-c',str(source),'-o','<output>']
                else:
                    command=[prefix+'as',*DEVICE.toolchain['asflags'],'-I',str(folder),'-I',sdk.INCLUDE,'-o','<output>',str(source)]
                cache[canonical(command)]=obj
    # Executable-section audits are pure functions of their bytes. Reuse only
    # the exact result recorded by the freshly compiled full reference.
    section_audits={}
    for path in report['object_sha256']:
        obj=elf.Elf.load(path)
        for section in obj.sections:
            label=path+':'+section.name
            if label in report['instruction_audits']:
                section_audits[hashlib.sha256(section.data).hexdigest()]=report['instruction_audits'][label]
    native_audit=smz.audit
    def object_audit(data):
        key=hashlib.sha256(data).hexdigest()
        return section_audits[key] if key in section_audits else native_audit(data)
    smz.audit=object_audit
    # AUDIO has only sixteen distinct images across this catalog. Cache exact
    # linked-ELF audits by ELF bytes, section, address and auditor source hash.
    native_functions=smz.audit_functions
    audit_source=Path(audit_module.__file__).read_bytes()
    audit_cache=bank.parent/'verified-audit-cache';audit_cache.mkdir(exist_ok=True)
    def linked_audit(image,section,address):
        key=hashlib.sha256(image.raw+section.encode()+str(address).encode()+audit_source).hexdigest()
        path=audit_cache/(key+'.json')
        if path.exists():
            record=json.loads(path.read_text())
            payload=json.dumps(record['audit'],sort_keys=True).encode()
            if record['key']!=key or hashlib.sha256(payload).hexdigest()!=record['sha256']:raise ValueError('Audit cache integrity mismatch')
            return record['audit']
        result=native_functions(image,section,address)
        payload=json.dumps(result,sort_keys=True).encode()
        temporary=path.with_suffix('.'+str(os.getpid())+'.tmp')
        temporary.write_text(json.dumps({'key':key,'sha256':hashlib.sha256(payload).hexdigest(),'audit':result}))
        publish(temporary,path)
        return result
    smz.audit_functions=linked_audit
    # Cache dependency scans; the SDK still re-hashes every pinned source at
    # the end of each build, so a source change refuses the reference.
    native_inputs=smz.source_inputs;dependency_cache={};scan_depth=0
    def source_inputs(source,folder,inputs,binary=False):
        nonlocal scan_depth
        if scan_depth:return native_inputs(source,folder,inputs,binary)
        key=(str(Path(source).resolve()),str(Path(folder).resolve()),binary)
        if key[0] in inputs:return
        if key not in dependency_cache:
            collected={};scan_depth+=1
            try:native_inputs(source,folder,collected,binary)
            finally:scan_depth-=1
            dependency_cache[key]=collected
        inputs.update(dependency_cache[key])
    smz.source_inputs=source_inputs
    # Repeated AUDIO links and generated zero-directory objects have exactly
    # identical flags, script bytes and ordered object inputs. Their native
    # outputs can be reused without substituting the TypeScript linker.
    native_cache=bank.parent/'verified-native-cache';native_cache.mkdir(exist_ok=True)
    object_pins={}
    native_run=sdk.run;hits=0
    def cached_run(cmd):
        nonlocal hits
        obj=cache.get(canonical(cmd))
        if obj is not None:
            pin=report['object_sha256'][str(obj)]
            if digest(obj)!=pin:raise ValueError('Audited compiler object changed')
            target=Path(cmd[cmd.index('-o')+1]);os.link(obj,target)
            object_pins[str(target.resolve())]=pin;hits+=1
            return ''
        target=Path(cmd[cmd.index('-o')+1] if '-o' in cmd else cmd[-1])
        if target.name not in ('audio.elf','audio.bin','directory.o'):return native_run(cmd)
        normalized=[]
        for index,value in enumerate(cmd):
            value=str(value)
            if value==str(target):normalized.append('<output>');continue
            path=Path(value)
            if index and path.is_file():
                pin=object_pins.get(str(path.resolve())) or digest(path)
                normalized.append('sha256:'+pin)
            else:normalized.append(value)
        key=hashlib.sha256(json.dumps(normalized).encode()).hexdigest()
        cached=native_cache/(key+target.suffix);metadata=native_cache/(key+'.json')
        if cached.exists() and metadata.exists():
            record=json.loads(metadata.read_text())
            if record['command']!=normalized or digest(cached)!=record['sha256']:raise ValueError('Native tool cache integrity mismatch')
            os.link(cached,target);return ''
        result=native_run(cmd)
        temporary=cached.with_suffix(cached.suffix+'.'+str(os.getpid())+'.tmp')
        os.link(target,temporary);publish(temporary,cached)
        tempmeta=metadata.with_suffix('.'+str(os.getpid())+'.tmp')
        tempmeta.write_text(json.dumps({'command':normalized,'sha256':digest(target)}));publish(tempmeta,metadata)
        return result
    sdk.run=cached_run
    def build(output,ids):
        nonlocal hits
        if not ids or set(ids)-{m['id'] for m in config} or len(ids)!=len(set(ids)):raise ValueError('Unknown or repeated selection')
        before=hits;selected=[m for m in config if m['id'] in ids]
        smz.build([root/m['path'] for m in selected if m['kind']=='machine'],output=output,
                  module_directories=[root/m['path'] for m in selected if m['kind']=='module'])
        if hits==before:raise ValueError('No audited compiler objects reused')
    if a.serve:
        print('READY',flush=True)
        for line in sys.stdin:
            try:
                request=json.loads(line);build(Path(request['out']),request['ids'])
                print(json.dumps({'ok':True}),flush=True)
            except Exception:
                print(json.dumps({'ok':False,'error':traceback.format_exc()}),flush=True)
    else:
        if not a.out:p.error('--out is required without --serve')
        build(a.out,a.ids)
        print('Native SDK reference: re-linked and audited;',hits,'verified compiler objects reused')

if __name__=='__main__':main()
