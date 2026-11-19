"""Measure and compare real mono/stereo MAIN recorder callbacks on analog-emu.

Both banks are relocated independently. Compare PCM, peak/state and full EMAC
context; measure callback-only instructions, excluding setup and comparison.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from elekloader.devices.syntakt import DEVICE
from elekloader.sdk.elf import Elf

BASE,HELPER,CONFIG,OLD,NEW=0x40400000,0x40500000,0x405f0000,0x40400000,0x40800000
FIELDS=('sam_clock','sam_enabled','sam_source','sam_state','sam_post','sam_mon','sam_peak',
        'sam_stereo','sam_take_stereo','sam_frames','sam_take_post','sam_pcm','sam_take_source',
        'sam_limit','sam_trigger','sam_pcm_peak','sam_ret_clock','sam_ret','sam_last_source',
        'sam_report','sam_peak_r','sam_play_length','sam_view','sam_manager','sam_play_pos')


def bank(path,base):
    raw=path.read_bytes();n,nr=struct.unpack_from('>II',raw,52)
    data=bytearray(raw[80+4*nr:80+4*nr+n])
    for off in struct.unpack_from('>%dI'%nr,raw,80):
        struct.pack_into('>I',data,off,struct.unpack_from('>I',data,off)[0]+base)
    symbols={s.name:base+s.value for s in Elf.load(path.with_suffix('.work')/'main.elf').symbols if s.name}
    return data,symbols


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--reference-bank',required=True,type=Path);p.add_argument('--bank',required=True,type=Path)
    p.add_argument('--cfbench',required=True,type=Path);p.add_argument('--out',required=True,type=Path)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    prefix=os.environ.get('ELEKLOADER_CROSS',DEVICE.toolchain['prefix']);sgdk=Path(os.environ['SGDK_BIN'])
    def tool(n):return sgdk/(prefix+n+('.exe' if os.name=='nt' else ''))
    def run(args):
        r=subprocess.run([str(x) for x in args],cwd=ROOT,text=True,capture_output=True)
        if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        return r.stdout
    co,so,exe,ld,raw=[a.out/n for n in ('driver.o','abi.o','driver.elf','driver.ld','driver.bin')]
    run([tool('gcc'),*DEVICE.toolchain['cflags'],'-fno-jump-tables','-c','tests/st_recorder_capture_cfbench.c','-o',co])
    run([tool('as'),'-mcpu=54415','tests/st_stereo_cfbench.s','-o',so])
    ld.write_text(f'SECTIONS {{ .image 0x{HELPER:x} : {{ *(.text*) *(.rodata*) *(.data*) *(.bss*) }} }}\n')
    run([tool('ld'),'--emit-relocs','-T',ld,'-o',exe,co,so]);run([tool('objcopy'),'-O','binary','-j','.image',exe,raw])
    h={s.name:s.value for s in Elf.load(exe).symbols if s.name}
    old,osym=bank(a.reference_bank,OLD);new,nsym=bank(a.bank,NEW)
    data=bytearray(old)
    def put(at,b):
        off=at-BASE
        if len(data)<off+len(b):data.extend(bytes(off+len(b)-len(data)))
        data[off:off+len(b)]=b
    put(HELPER,raw.read_bytes());put(NEW,new)
    values=[osym['recorder_audio'],nsym['recorder_audio'],*[osym[k] for k in FIELDS],*[nsym[k] for k in FIELDS],
            nsym['recorder_return_bank'],nsym['recorder_stereo_pcm']]
    put(CONFIG,struct.pack('>%dI'%len(values),*values))
    mac=[0xb65,0x12345678,0xfedcba98,0x76543210,0x89abcdef,0xa5c33c5a,0x55aa33cc,0xfffff00f]
    put(0x40600000,struct.pack('>8I',*mac))
    image=a.out/'callbacks.bin';image.write_bytes(data)
    def execute(steps):
        output=run([a.cfbench,image,f'{BASE:x}',*steps])
        rows=re.findall(r'-> d0 ([0-9a-fA-F]+): (\d+) instructions',output)
        return [(int(r,16),int(n)) for r,n in rows],output
    comparisons=[];costs={}
    rows,output=execute([f'{h["st_capture_kernels"]:x}:200,0'])
    assert rows[-1][0]==0,('kernels',output)
    rows,output=execute([f'{h["st_capture_kernels"]:x}:200,1'])
    assert rows[-1][0]!=0,('kernel negative control',output)
    names=('POST_no_gains','POST_active_returns','POST_quiet_returns','PRE_active_returns')
    scenes=[(n,name) for n,name in enumerate(names)]+[(256+n,'mono_'+name) for n,name in enumerate(names)]
    for scene,name in scenes:
        rows,output=execute([f'{h["st_capture_compare"]:x}:{scene:x},20'])
        assert rows[-1][0]==0,(name,output)
        comparisons.append(name)
        costs[name]={}
        for side,label in ((0,'reference'),(1,'current')):
            fn=values[side]
            rows,output=execute([f'{h["st_capture_setup"]:x}:{side:x},{scene:x}',*[f'{fn:x}']*4])
            costs[name][label]=[n for _,n in rows[2:]] # steady blocks; discard setup + first block
    rows,output=execute([f'{h["st_capture_compare"]:x}:9,20'])
    assert rows[-1][0]!=0,output
    for scene in (16,19,272,275):
        rows,output=execute([f'{h["st_capture_compare"]:x}:{scene:x},200'])
        assert rows[-1][0]==0,('randomized blocks',scene,output)
    for scene in (33,35,65,67,193,195,289,291,321,323,449,451):
        rows,output=execute([f'{h["st_capture_compare"]:x}:{scene:x},20'])
        assert rows[-1][0]==0,('partial tail/ARMED',scene,output)
    report=dict(backend='analog-emu cfbench',source='MAIN LR stereo and mono',comparisons=comparisons,
                blocks_per_comparison=32,randomized_blocks=2048,kernel_vectors=512,
                fallback_cases=['47-frame PRE/POST tails','PRE/POST threshold transition','PRE/POST ARMED silence'],
                full_emac_preserved=True,negative_control_detected=True,
                callback_instructions=costs,hardware_cpu_measured=False,
                cfbench_sha256=hashlib.sha256(a.cfbench.read_bytes()).hexdigest(),
                reference_bank_sha256=hashlib.sha256(a.reference_bank.read_bytes()).hexdigest(),
                bank_sha256=hashlib.sha256(a.bank.read_bytes()).hexdigest())
    (a.out/'validation.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))


if __name__=='__main__':main()
