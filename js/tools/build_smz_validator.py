"""Compile the actual core bank parser for the browser, without OS inputs.

Set ZIG to the portable Zig 0.13.0 executable. All outputs and build caches are
private local files under --out. No firmware code or generated OS bytes enter
this WebAssembly program: it contains only the source parser and its adapter.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
FLAGS=['cc','-target','wasm32-freestanding','-O3','-g0','-ffreestanding','-fno-builtin','-nostdlib',
       '-Wl,--no-entry','-Wl,--strip-all','-Wl,--export-memory','-Wl,--max-memory=0x1200000',
       '-Wl,--export=synfact_smz_api','-Wl,--export=synfact_smz_contract',
       '-Wl,--export=synfact_smz_failure','-Wl,--export=synfact_smz_input',
       '-Wl,--export=synfact_smz_validate']

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--native',action='store_true',help='also build the Windows x64 native comparison library with LLVM_MINGW_BIN')
    a=p.parse_args()
    zig=os.environ.get('ZIG')
    if not zig:raise SystemExit('Set ZIG to Zig 0.13.0 first')
    version=subprocess.check_output([zig,'version'],text=True).strip()
    if version!='0.13.0':raise SystemExit('This parser build is pinned to Zig 0.13.0')
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
    env=os.environ.copy()
    env['ZIG_GLOBAL_CACHE_DIR']=str(out/'zig-global-cache')
    env['ZIG_LOCAL_CACHE_DIR']=str(out/'zig-local-cache')
    wasm=out/'smz-validator.wasm'
    sources=['js/tools/smz_validator.c','mods/core-st/smz.c']
    command=[zig,*FLAGS,'-I',str(ROOT/'elekloader/sdk/include'),*[str(ROOT/s) for s in sources],'-o',str(wasm)]
    subprocess.run(command,cwd=ROOT,env=env,check=True)
    sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
    tracked=sources+['mods/core-st/smz.h','mods/core-st/storage.h','mods/core-st/cold.h','elekloader/sdk/include/syntakt/machines.h']
    manifest={'kind':'core-parser-wasm','api':1,'file':wasm.name,'sha256':sha(wasm),
              'contract':0x53594606,'coreVersion':json.loads((ROOT/'mods/core-st/mod.json').read_text())['version'],
              'sourceSha256':{name:sha(ROOT/name) for name in tracked},
              'compiler':{'name':'Zig','version':version,'flags':FLAGS}}
    (out/'validator.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
    if a.native:
        toolchain=os.environ.get('LLVM_MINGW_BIN')
        if not toolchain:raise SystemExit('Set LLVM_MINGW_BIN for the native Windows x64 parser')
        subprocess.run([str(Path(toolchain)/'x86_64-w64-mingw32-clang.exe'),'-shared','-O3',
                        '-ffreestanding','-fno-builtin','-I',str(ROOT/'elekloader/sdk/include'),
                        *[str(ROOT/s) for s in sources],'-o',str(out/'smz-validator.dll')],cwd=ROOT,check=True)
    print(json.dumps({'file':wasm.name,'bytes':wasm.stat().st_size,'sha256':manifest['sha256'],'core':manifest['coreVersion']}))

if __name__=='__main__':main()
