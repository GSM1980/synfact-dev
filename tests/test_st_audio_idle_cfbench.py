"""Run the reduced mixer, real native output tail and EMAC on analog-emu.

Input is an already verified local core build. Firmware-derived images stay
in --out. No device access, timing percentages, or physical IO claims.
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

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from elekloader.devices.syntakt import DEVICE
from elekloader.sdk.elf import Elf
from elekloader.sdk.syntakt.audio import audit

BASE,HELPER,CONFIG=0x40000400,0x40500000,0x405f0000


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build',required=True,type=Path)
    p.add_argument('--cfbench',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    prefix=os.environ.get('ELEKLOADER_CROSS',DEVICE.toolchain['prefix'])
    sgdk=Path(os.environ['SGDK_BIN'])
    def tool(name):return sgdk/(prefix+name+('.exe' if os.name=='nt' else ''))
    def run(args):
        r=subprocess.run([str(x) for x in args],cwd=ROOT,text=True,capture_output=True)
        if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        return r.stdout
    c=a.out/'driver.o';s=a.out/'idle.o';abi=a.out/'abi.o'
    run([tool('gcc'),*DEVICE.toolchain['cflags'],'-fno-jump-tables','-I','elekloader/sdk/include',
         '-c','tests/st_audio_idle_cfbench.c','-o',c])
    run([tool('as'),'-mcpu=54415','mods/recorder-st/idle.s','-o',s])
    run([tool('as'),'-mcpu=54415','tests/st_stereo_cfbench.s','-o',abi])
    script=a.out/'driver.ld';executable=a.out/'driver.elf';binary=a.out/'driver.bin'
    script.write_text(f'SECTIONS {{ .image 0x{HELPER:x} : {{ *(.text*) *(.rodata*) *(.data*) *(.bss*) }} }}\n')
    run([tool('ld'),'--emit-relocs','-T',script,'-o',executable,c,s,abi])
    run([tool('objcopy'),'-O','binary','-j','.image',executable,binary])
    h={s.name:s.value for s in Elf.load(executable).symbols if s.name}
    instruction_audits={}
    for path,section in ((s,'.text'),
                         (ROOT/'mods/core-st/out/core.work/audio-idle.s.o','.run'),
                         (ROOT/'mods/core-st/out/core.work/audio-idle.c.o','.text')):
        instruction_audits[path.name]=audit(Elf.load(path).section(section).data)
    r=json.loads((a.build/'verification.json').read_text());sym=r['symbols'];layout=r['link']
    data=bytearray((a.build/'main.bin').read_bytes())
    assert len(data)<HELPER-BASE
    data.extend(bytes(HELPER-BASE-len(data)));data.extend(binary.read_bytes())
    data.extend(bytes(CONFIG-BASE-len(data)))
    config=[layout['run_load'],layout['ddr'][0],layout['ddr'][1]-layout['ddr'][0],
            layout['bss'][1]-layout['ddr'][0],*[sym['core:'+name] for name in
            ('core_audio_idle_begin','core_audio_idle_owner','core_audio_idle_active',
             'core_audio_idle_mix','core_native_lfo','core_native_modulation','core_main_mix_table')]]
    data.extend(struct.pack('>11I',*config));data.extend(bytes(0x100-44))
    results=[]
    def check(name,mode,vector,negative=False,fallback=False):
        image=bytearray(data);image.extend(struct.pack('>64i',*vector))
        if fallback:
            off=0x4008f1ca-BASE
            image[off:off+6]=struct.pack('>HI',0x4ef9,h['st_idle_fallback_probe'])
        path=a.out/(name+'.bin');path.write_bytes(image)
        fn=h['st_idle_fallback'] if fallback else h['st_idle_check']
        args='' if fallback else f':{mode:x}'
        output=run([a.cfbench,path,f'{BASE:x}',f'{h["st_idle_load"]:x}',f'{fn:x}{args}'])
        matches=re.findall(r'-> d0 ([0-9a-fA-F]+): (\d+) instructions',output)
        assert len(matches)==2,output
        result,count=int(matches[-1][0],16),int(matches[-1][1])
        assert (result!=0 if negative else result==0),(name,hex(result),output)
        results.append(dict(name=name,result=hex(result),instructions=count,negative_control=negative))
    mono=[(i//2-16)*65536 for i in range(64)]
    stereo=[((i//2-16)*65536 if not i%2 else (15-i//2)*32768) for i in range(64)]
    check('silence_clears_all_buses',0,[0]*64)
    check('mono_preview_native_usb_and_output',1,mono)
    check('stereo_preview_native_usb_and_output',2,stereo)
    extremes=[(-0x40000000 if i%2 else 0x40000000) for i in range(64)]
    check('preview_saturation_native_output',2,extremes)
    check('native_fallback_arguments_and_abi',0,[0]*64,fallback=True)
    check('negative_control_adc_leak',9,mono,negative=True)
    costs={}
    for active,label in ((0,'normal'),(1,'reduced')):
        image=bytearray(data);image.extend(struct.pack('>64i',*stereo))
        path=a.out/('cost-'+label+'.bin');path.write_bytes(image)
        output=run([a.cfbench,path,f'{BASE:x}',f'{h["st_idle_load"]:x}',
                    f'{h["st_idle_prepare"]:x}:2,{active:x}',f'{config[7]:x}:0,0,40620000'])
        matches=re.findall(r'-> d0 ([0-9a-fA-F]+): (\d+) instructions',output)
        assert int(matches[-1][0],16)==0x8000dbd0,output
        costs[label]=int(matches[-1][1])
    assert costs['reduced']<costs['normal'],costs
    report=dict(backend='analog-emu cfbench',cases=results,hardware_tested=False,
                new_code_instruction_audits=instruction_audits,
                instruction_count_scope='Whole fixture including setup, clears and assertions; not hardware CPU percentages.',
                mixer_call_instructions=costs,
                mixer_cost_scope='Isolated mixer calls, same controlled zero-input/FX-state fixture and preview callback; not full IRQ or hardware CPU load.',
                firmware_installed=False,cfbench_sha256=hashlib.sha256(a.cfbench.read_bytes()).hexdigest())
    (a.out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
