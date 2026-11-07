# SPDX-License-Identifier: GPL-2.0-or-later
"""Build an SMZ4 machine/module bank from normal SDK sources.

python -m elekloader.sdk.syntakt.smz --preset samplers --out out/samplers.smz4
python -m elekloader.sdk.syntakt.smz MODDIR [MODDIR ...] --out out/bank.smz4
Each mod.json adds smz: {descriptor, audio: {sources, update, render}}.
No donor firmware code or arbitrary runtime patch plans enter the bank.
"""
import argparse
import json
import os
import re
from pathlib import Path
import struct
import subprocess
import zlib
from .. import build as sdk,elf
from .audio import audit,audit_functions
from ... import formats
from ...devices.syntakt import DEVICE
import hashlib

ABI=0x454c0001
CONTRACT=0x53594603  # exact resident core/SMZ contract, mods/core-st/smz.h
AUDIO=0x4204f750
AUDIO_MAX=0x42100000-AUDIO
MAIN_MAX=0x200000
MAX_PARAMS=32
STORED_PARAMS=28

def align(n,a=16):return (n+a-1)&-a
def be32(*words):return struct.pack('>'+str(len(words))+'I',*words)

def source_inputs(src,folder,inputs,binary=False):
    """Pin transitive C/assembler includes, including private nested resources
    and the files assembler sources .incbin."""
    src=Path(src).resolve()
    if str(src) in inputs:return
    raw=src.read_bytes();inputs[str(src)]=hashlib.sha256(raw).hexdigest()
    if binary:return
    pattern=r'^\s*(?:#\s*include\s*[<"]([^>"]+)[>"]|\.include\s*"([^"]+)"|\.incbin\s*"([^"]+)")'
    for match in re.finditer(pattern,raw.decode('utf-8-sig'),re.M):
        name=match[1] or match[2] or match[3]
        for base in (src.parent,Path(folder),Path(sdk.INCLUDE)):
            dependency=base/name
            if dependency.is_file():source_inputs(dependency,folder,inputs,binary=bool(match[3]));break

MOD_INFO_ID,MOD_INFO_VERSION,MOD_INFO_MAX=24,16,16   # feature bit 3: the mods' versions (SMZ4.md)

def mod_info_directory(records):
    """-> the bank's mod-info directory: (id, version) per mod as 40-byte records, NUL-padded,
    ended by a zero record. Each id and version is 1..23 and 1..15 printable ASCII characters."""
    if not records or len(records)>MOD_INFO_MAX:raise ValueError('a bank lists 1..%d mods'%MOD_INFO_MAX)
    out=b''
    for mod_id,version in records:
        for what,text,size in (('id',mod_id,MOD_INFO_ID),('version',version,MOD_INFO_VERSION)):
            if not 0<len(text)<size or not all(' '<=c<'' for c in text):
                raise ValueError('mod %s %r does not fit the bank directory'%(what,text))
            out+=text.encode('ascii').ljust(size,b'\x00')
    return out+bytes(40)

def pack(image,relocations,descriptors,section,features=0,ui=0,modules=0,module_count=0,info=0):
    """The builder's inputs are generated from source, never a previous SMZ."""
    rels=sorted(relocations)
    if (not descriptors and not module_count) or len(descriptors)>16:raise ValueError('bank needs machines or modules; maximum 16 machines')
    if len(rels)!=len(set(rels)) or any(a+4>b for a,b in zip(rels,rels[1:])):
        raise ValueError('overlapping relocations')
    if len(image)%4:raise ValueError('MAIN image must be longword aligned')
    body=be32(*rels)+image
    if 80+len(body)>MAIN_MAX:raise ValueError('MAIN bank allocation exceeds 2 MiB')
    if len(section)>AUDIO_MAX:raise ValueError('AUDIO bank exceeds its owned reserve')
    if section[:4]!=b'SMA3' or len(section)%4:raise ValueError('bad AUDIO section')
    section=bytearray(section)
    section[12:16]=be32(zlib.adler32(section[16:])&0xffffffff)
    image_sum=sum(struct.unpack('>'+str(len(body)//4)+'I',body))&0xffffffff
    audio_at=align(80+len(body))
    zone=b'SMZ4'+be32(len(image),len(rels),info,CONTRACT if features&16 else 0xffffffff,image_sum,audio_at-48,len(section))
    if features&~31:raise ValueError('unknown machine-bank feature')
    if bool(features&8)!=bool(info):raise ValueError('feature 8 needs the mod-info directory, and only it')
    data=bytearray(b'SAFE'+be32(2,ABI,0,0,48,features,ui,modules,module_count,0,len(descriptors))+zone+body)
    data+=bytes(audio_at-len(data))+section+bytes(align(len(section),2048)-len(section))
    data[12:16]=be32(len(data));data[16:20]=be32(zlib.adler32(data[20:])&0xffffffff)
    return bytes(data)

def build(directories,stock_path=None,output=None,module_directories=()):
    if output is None:raise ValueError('output is required')
    output=Path(output).resolve()
    if output.exists():raise ValueError('output exists; use a new destination')
    root=Path(__file__).resolve().parents[3]
    output.parent.mkdir(parents=True,exist_ok=True)
    if output.is_relative_to(root):
        subprocess.run(['git','check-ignore',str(output)],cwd=root,check=True,capture_output=True)
    work=output.parent/(output.stem+'.work');work.mkdir(parents=True,exist_ok=True)
    dev=DEVICE
    if stock_path:
        stock,dev,release=formats.load(stock_path)
        if dev.key!='syntakt' or release.version!='1.41':raise ValueError('requires Syntakt OS 1.41')
    prefix=os.environ.get('ELEKLOADER_CROSS',dev.toolchain['prefix'])
    descriptors=[];uis=[];modules=[];main_objs=[];audio_objs=[];audio_exports=[];types=set();features=0;inputs={};audits={};mod_records=[]
    api_header=Path(sdk.INCLUDE)/'syntakt/machines.h'
    inputs[str(api_header)]=hashlib.sha256(api_header.read_bytes()).hexdigest()
    modules_header=Path(sdk.INCLUDE)/'syntakt/modules.h'
    inputs[str(modules_header)]=hashlib.sha256(modules_header.read_bytes()).hexdigest()
    folders=[(d,False) for d in directories]+[(d,True) for d in module_directories]
    for k,(directory,is_module) in enumerate(folders):
        folder=Path(directory).resolve()
        manifest=json.loads((folder/'mod.json').read_text(encoding='utf-8-sig'))
        mod_records.append((str(manifest['id']),str(manifest['version'])))
        # Machine source compilation needs the target profile, not an OS image.
        if any(manifest.get(f) for f in ('sites','subscribe','contribute','collections','fixed','storage','machines','dsp','weak')):
            raise ValueError('SMZ banks accept machine sources only, without resident hooks/resources')
        if manifest.get('device')!='syntakt' or str(manifest.get('os'))!='1.41':
            raise ValueError('machine sources must target Syntakt OS 1.41')
        config=manifest['smz_module'] if is_module else manifest['smz'];audio=config.get('audio',{})
        requested=config.get('features',[])
        if any(f not in ('sample-pool-v1','machine-ui-v1') for f in requested):raise ValueError('unknown SMZ feature')
        if 'sample-pool-v1' in requested:features|=1
        if 'machine-ui-v1' in requested:features|=2
        if is_module:modules.append(config['descriptor']);features|=4;entries=[]
        else:
            entries=config.get('machines') or [dict(descriptor=config['descriptor'],update=audio['update'],render=audio['render'])]
            modules.extend(config.get('modules',[]))
            if config.get('modules'):features|=4
        descriptors.extend(m['descriptor'] for m in entries)
        uis.extend(m.get('ui') for m in entries)
        audio_exports.extend((m['update'],m['render']) for m in entries)
        inputs[str(folder/'mod.json')]=hashlib.sha256((folder/'mod.json').read_bytes()).hexdigest()
        for side,names in (('main',manifest.get('sources',[])),('audio',audio.get('sources',[]))):
          for index,name in enumerate(names):
            src=(folder/name).resolve();obj=work/('%s%d-%d.o'%(side,k,index))
            if not src.is_file():raise ValueError('missing source: '+str(src))
            source_inputs(src,folder,inputs)
            if src.suffix=='.c':
                sdk.run([prefix+'gcc',*dev.toolchain['cflags'],'-fno-jump-tables','-I',sdk.INCLUDE,
                         '-I',str(folder),'-MMD','-MF',str(obj.with_suffix('.d')),
                         '-c',str(src),'-o',str(obj)])
            elif src.suffix in ('.s','.asm'):
                sdk.run([prefix+'as',*dev.toolchain['asflags'],'-I',str(folder),'-I',sdk.INCLUDE,'-o',str(obj),str(src)])
            else:raise ValueError('machine sources must be C or assembler')
            e=elf.Elf.load(obj)
            for sec in e.sections:
                if sec.flags&4 and sec.size:
                    if sec.name not in ('.text','.run'):raise ValueError('unexpected executable machine section')
                    audits[str(obj)+':'+sec.name]=audit(sec.data)
            (main_objs if side=='main' else audio_objs).append(str(obj))
    n=len(descriptors)
    if not 0<=n<=16 or len(modules)>16 or (not n and not modules):raise ValueError('provide machines or modules, up to 16 of each')
    if bool(features&2)!=(bool(uis) and all(uis)) or (any(uis) and not all(uis)):
        raise ValueError('machine-ui-v1 requires a UI descriptor for every machine')
    module_at=(40+(32 if features&2 else 0))*n
    # The source-defined descriptors are copied to a contiguous, relocatable
    # directory in the file image. No runtime registration code is generated.
    header=work/'directory.s';header.write_text('.section .smz.directory,"aw"\n.space %d\n'%(module_at+16*len(modules)))
    head_obj=work/'directory.o';sdk.run([prefix+'as',*dev.toolchain['asflags'],'-o',str(head_obj),str(header)])
    script=work/'main.ld';script.write_text('SECTIONS { .image 0 : { *(.smz.directory) *(.run) *(.text*) '
        '* (.rodata*) *(.data*) BYTE(0) . = ALIGN(4); *(.bss*) *(COMMON) . = ALIGN(4); } '
        '/DISCARD/ : { *(.comment) *(.note*) *(.gnu.attributes) } }\n')
    exe=work/'main.elf';raw=work/'main.bin'
    sdk.run([prefix+'ld','--emit-relocs','-T',str(script),'-o',str(exe),str(head_obj),*main_objs])
    sdk.run([prefix+'objcopy','-O','binary','-j','.image',str(exe),str(raw)])
    e=elf.Elf.load(exe);sec=e.section('.image');image=bytearray(raw.read_bytes())
    audits['linked_MAIN']=audit_functions(e,'.image',0)
    syms={s.name:s for s in e.symbols if s.name};rels=[]
    for at,kind,symbol,add in e.relocs.get(sec.idx,[]):
        if kind==elf.R_68K_32 and symbol.shndx==sec.idx:rels.append(at)
        elif kind not in (elf.R_68K_32,elf.R_68K_PC32,elf.R_68K_PC16,elf.R_68K_NONE):
            raise ValueError('unsupported MAIN machine relocation')
    machine_types=[]
    for i,name in enumerate(descriptors):
        symbol=syms[name];at=symbol.value
        if symbol.shndx!=sec.idx or symbol.size!=40:raise ValueError('descriptor must be a 40-byte core_machine_def')
        image[40*i:40*(i+1)]=image[at:at+40]
        rels += [40*i+(r-at) for r in list(rels) if at<=r<at+40]
        typ,params=struct.unpack_from('>2I',image,40*i)
        if typ<64 or typ>=80 or typ in types:raise ValueError('distinct machine types 64..79 required')
        if not 1<=params<=STORED_PARAMS:raise ValueError('32 positions reserved; this storage ABI supports 1..28 controls')
        types.add(typ);machine_types.append(typ)
    if features&2:
        for i,name in enumerate(uis):
            symbol=syms[name];at=symbol.value;dst=40*n+32*i
            if symbol.shndx!=sec.idx or symbol.size!=32:raise ValueError('UI descriptor must be a 32-byte st_machine_ui')
            image[dst:dst+32]=image[at:at+32]
            rels += [dst+(r-at) for r in list(rels) if at<=r<at+32]
    for i,name in enumerate(modules):
        symbol=syms[name];at=symbol.value;dst=module_at+16*i
        if symbol.shndx!=sec.idx or symbol.size!=16:raise ValueError('module descriptor must be a 16-byte st_smz_module')
        image[dst:dst+16]=image[at:at+16]
        rels += [dst+(r-at) for r in list(rels) if at<=r<at+16]
    audio_script=work/'audio.ld';code_start=AUDIO+16+16*n+384
    audio_script.write_text('SECTIONS { .image 0x%x : { *(.text) *(.rodata*) *(.data*) '
        'BYTE(0) . = ALIGN(4); *(.bss*) *(COMMON) . = ALIGN(16); } '
        '/DISCARD/ : { *(.comment) *(.note*) *(.gnu.attributes) } }\n'%code_start)
    audio_exe=work/'audio.elf';audio_raw=work/'audio.bin'
    code=b'';asym={}
    if audio_objs:
        sdk.run([prefix+'ld','-T',str(audio_script),'-o',str(audio_exe),*audio_objs])
        sdk.run([prefix+'objcopy','-O','binary','-j','.image',str(audio_exe),str(audio_raw)])
        ae=elf.Elf.load(audio_exe);asym={s.name:s.value for s in ae.symbols if s.name}
        audits['linked_AUDIO']=audit_functions(ae,'.image',code_start)
        code=audio_raw.read_bytes()
    size=code_start-AUDIO+len(code)
    entries=b''.join(be32(t,asym[u],asym[r],0) for t,(u,r) in zip(machine_types,audio_exports))
    section=b'SMA3'+be32(n,size,0)+entries+bytes(384)+code
    # Feature 8: the mods' versions, a zero-terminated directory in the MAIN image (SMZ4.md).
    image+=bytes(-len(image)%4);info=len(image);image+=mod_info_directory(mod_records);features|=8|16
    data=pack(bytes(image),rels,descriptors,section,features,40*n if features&2 else 0,
              module_at if modules else 0,len(modules),info)
    if any(hashlib.sha256(Path(path).read_bytes()).hexdigest()!=digest for path,digest in inputs.items()):
        raise ValueError('machine sources changed during compilation')
    output.write_bytes(data)
    report={'abi':hex(ABI),'machines':machine_types,'bytes':len(data),'main_bytes':len(image),
            'audio_bytes':size,'relocations':len(rels),'control_positions':32,'stored_controls':28,
            'features':features,'modules':modules,'optimization':'-O3','cpu':'54415','cflags':dev.toolchain['cflags'],
            'source_sha256':inputs,'instruction_audits':audits,
            'firmware_required':False,'sha256':hashlib.sha256(data).hexdigest()}
    report['object_sha256']={str(Path(path).resolve()):hashlib.sha256(Path(path).read_bytes()).hexdigest()
                             for path in main_objs+audio_objs}
    output.with_suffix(output.suffix+'.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('mods',nargs='*');p.add_argument('--preset',choices=('samplers',))
    p.add_argument('--module',action='append',default=[],help='optional SMZ module source directory (repeatable)')
    p.add_argument('--stock',help='optional verification of the target Syntakt OS; no donor firmware is used')
    p.add_argument('--out',required=True)
    a=p.parse_args()
    if (a.mods and a.preset) or not (a.mods or a.preset or a.module):p.error('provide machine sources, a preset or --module')
    folders=[Path(__file__).resolve().parents[3]/'mods/samplers-st'] if a.preset else a.mods
    print(json.dumps(build(folders,a.stock,a.out,a.module),indent=2))
if __name__=='__main__':main()
