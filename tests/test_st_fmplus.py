# SPDX-License-Identifier: AGPL-3.0-or-later
"""FM+ fixed-point port versus pinned De-Pattern's real float32 DSL.

No OS is needed for these source/port tests; --os also reproduces all tables.
Chaotic self-feedback is compared by bounded output/level, never called exact.
This is not a physical CPU measurement or a full original-chain comparison.
"""
import argparse
import ctypes
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
FM=ROOT/'mods/digimono-st/fmplus'
sys.path.insert(0,str(FM))
from reference import Voice


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',type=Path,required=True)
    p.add_argument('--out',type=Path,default=Path('out/fmplus-tests'));p.add_argument('--os',type=Path)
    p.add_argument('--arithmetic-only',action='store_true',help='run split/native-wide and table/source checks; omit the separately reported DSL sweep')
    a=p.parse_args();out=a.out.resolve();out.mkdir(parents=True,exist_ok=True)
    if a.os:
        from import_tables import generate
        text,_=generate(a.os);assert text==(FM/'tables.h').read_text(),'table extraction differs'
    cc=str(a.cc);src='mods/digimono-st/fmplus.c'
    source_sha=hashlib.sha256(Path(src).read_bytes()).hexdigest()
    table_sha=hashlib.sha256((FM/'tables.h').read_bytes()).hexdigest()
    run=lambda args:subprocess.run([str(x) for x in args],cwd=ROOT,check=True)
    flags=['-O2','-Wall','-Werror','-I','mods/digimono-st']
    run([cc,*flags,'-c',src,'-o',out/'split.o'])
    run([cc,*flags,'-DDM_FM_NATIVE_WIDE','-Ddm_fm_init=native_init','-Ddm_fm_trig=native_trig',
         '-Ddm_fm_half=native_half','-Ddm_fm_render=native_render','-c',src,'-o',out/'wide.o'])
    run([cc,*flags,'tests/test_st_fmplus.c',out/'split.o',out/'wide.o','-o',out/'arithmetic.exe'])
    run([out/'arithmetic.exe'])
    if a.arithmetic_only:
        Voice(None,9)  # Includes source hashes and the committed table hash.
        print('PASS: pinned FM+ source/table hashes; DSL sweep omitted explicitly')
        return
    host=cc.replace('i686-w64','x86_64-w64')
    wrapper=out/'size.c';wrapper.write_text('#include "fmplus/voice.h"\nunsigned voice_bytes(void){return sizeof(struct dm_fm_voice);}\n')
    run([host,*flags,'-shared',src,wrapper,'-o',out/'engine.dll'])
    lib=ctypes.CDLL(str(out/'engine.dll'));lib.voice_bytes.restype=ctypes.c_uint
    lib.dm_fm_half.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_int32,ctypes.c_void_p]
    defaults={9:[60,64,80,0,80,64,98],10:[60,64,80,64,102,80,98],11:[64,64,64,64,74,80,0]}
    rows=[]
    for kind in range(9,12):
        cases=[('base',defaults[kind],60*64)]
        for note in (24,48,69,96):cases.append((f'note-{note}',defaults[kind],note*64))
        for k in range(7):
            for value in (0,32,64,96,127):
                patch=list(defaults[kind]);patch[k]=value;cases.append((f'p{k}-{value}',patch,60*64))
        for label,patch,pitch in cases:
            ref=Voice(a.os,kind);state=ctypes.create_string_buffer(lib.voice_bytes());lib.dm_fm_init(state)
            controls=(ctypes.c_uint16*8)(*[x<<8 for x in patch],64<<8)
            lib.dm_fm_trig(state,kind,controls);pcm=(ctypes.c_int32*16)()
            error=power=ours=0.0
            for block in range(32):
                expected=ref.block(pitch,list(controls),block==0)
                lib.dm_fm_half(state,kind,controls,pitch,pcm)
                for x,y in zip(pcm,expected):
                    sample=x/2**29;assert math.isfinite(y) and abs(sample)<.51
                    error+=(sample-y)**2;power+=y*y;ours+=sample*sample
            snr=10*math.log10(max(power,1e-30)/max(error,1e-30))
            chaotic=(kind==9 and patch[3]>=32) or (kind==11 and patch[6]>=32)
            level=10*math.log10(max(ours,1e-30)/max(power,1e-30))
            # Source-level chaotic feedback can diverge on a one-LSB change.
            if chaotic:assert abs(level)<2.5,(kind,label,level)
            else:assert snr>=60 or error/512<2**-40,(kind,label,snr)
            rows.append(dict(type=kind,case=label,snr_db=snr,level_db=level,chaotic=chaotic))
        print('PASS: De-Pattern float32 DSL comparisons, TYPE',kind,flush=True)
    assert source_sha==hashlib.sha256(Path(src).read_bytes()).hexdigest(),'source changed during test'
    assert table_sha==hashlib.sha256((FM/'tables.h').read_bytes()).hexdigest(),'tables changed during test'
    result=dict(ok=True,cases=len(rows),rows=rows,source_sha256=source_sha,tables_sha256=table_sha,
                tables_reproduced=bool(a.os),hardware_tested=False)
    (out/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS:',len(rows),'FM+ DSL cases; >=60 dB clean cases, chaotic self-feedback level within 2.5 dB; no device access')


if __name__=='__main__':main()
