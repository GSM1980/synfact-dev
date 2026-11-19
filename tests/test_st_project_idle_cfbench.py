"""OS 1.42 project silence vs full native mixer; real output/USB/EMAC/ABI."""
import argparse
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

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build','cfbench','out'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    sgdk=Path(os.environ['SGDK_BIN'])
    prefix=os.environ.get('ELEKLOADER_CROSS',DEVICE.toolchain['prefix'])
    def tool(n):return sgdk/(prefix+n+('.exe' if os.name=='nt' else ''))
    def run(args):
        r=subprocess.run([str(x) for x in args],cwd=ROOT,text=True,capture_output=True)
        if r.returncode:raise RuntimeError(r.stdout+r.stderr)
        return r.stdout
    c,abi=a.out/'driver.o',a.out/'abi.o'
    run([tool('gcc'),*DEVICE.toolchain['cflags'],'-c','tests/st_project_idle_cfbench.c','-o',c])
    run([tool('as'),'-mcpu=54415','tests/st_stereo_cfbench.s','-o',abi])
    script=a.out/'driver.ld';executable=a.out/'driver.elf';binary=a.out/'driver.bin'
    script.write_text('SECTIONS { .image 0x40500000 : { *(.text*) *(.rodata*) *(.data*) *(.bss*) } }\n')
    run([tool('ld'),'--emit-relocs','-T',script,'-o',executable,c,abi])
    run([tool('objcopy'),'-O','binary','-j','.image',executable,binary])
    h={s.name:s.value for s in Elf.load(executable).symbols if s.name}
    r=json.loads((a.build/'verification.json').read_text());sym=r['symbols'];layout=r['link']
    data=bytearray((a.build/'main.bin').read_bytes())
    data.extend(bytes(0x40500000-0x40000400-len(data)));data.extend(binary.read_bytes())
    data.extend(bytes(0x405f0000-0x40000400-len(data)))
    config=[layout['run_load'],layout['ddr'][0],layout['ddr'][1]-layout['ddr'][0],
            layout['bss'][1]-layout['ddr'][0],*[sym['core:'+n] for n in
            ('core_audio_idle_begin','core_audio_idle_owner','core_audio_idle_active',
             'core_audio_idle_mix','core_native_lfo','core_native_modulation','core_main_mix_table','core_project_depth')]]
    data.extend(struct.pack('>12I',*config));path=a.out/'fixture.bin';path.write_bytes(data)
    def execute(*calls,fixture=path):
        result=run([a.cfbench,fixture,'40000400',f'{h["project_load"]:x}',*calls])
        rows=re.findall(r'-> d0 ([0-9a-fA-F]+): (\d+) instructions',result)
        assert rows,result
        return int(rows[-1][0],16),int(rows[-1][1])
    cases=[]
    negative=bytearray(data)
    start=layout['run_load']-0x40000400+sym['core:core_project_idle_mix']-layout['ddr'][0]
    clear=start+negative[start:start+80].index(bytes.fromhex('42985380'))
    # Advance over each dirty word without clearing it. The production image
    # remains unchanged; the differential must detect the resulting leak.
    negative[clear:clear+2]=bytes.fromhex('5888')
    negative_path=a.out/'negative-no-clear.bin';negative_path.write_bytes(negative)
    for mode,name in ((0,'no_recorder'),(1,'completed_after_render_in'),(9,'negative_control_missing_clear')):
        result,count=execute(f'{h["project_check"]:x}:{mode:x}',fixture=negative_path if mode==9 else path)
        assert (result!=0 if mode==9 else result==0),(name,hex(result))
        cases.append(dict(name=name,result=hex(result),instructions=count))
    costs={}
    for active,name in ((0,'normal'),(1,'project')):
        result,count=execute(f'{h["project_prepare"]:x}:{active:x}',f'{config[7]:x}:0,0,40620000')
        assert result==0x8000dbd0,hex(result)
        costs[name]=count
    assert costs['project']<costs['normal'],costs
    # Include the new IRQ code in the instruction audit, independently of
    # the builder's historical fixed source inventory.
    obj=Elf.load(ROOT/'mods/core-st/out/core.work/audio-idle.s.o')
    instructions=audit(obj.section('.run').data)
    report=dict(cases=cases,mixer_call_instructions=costs,irq_instructions_audited=instructions,
                backend='analog-emu physical EMAC model',hardware_tested=False,
                limits='Native mixer call in a silent fixture; not whole IRQ, hardware CPU percent or storage speed.')
    (a.out/'results.json').write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: no-recorder project mixer, six silent buses, USB, 256 native codec words, physical EMAC, callee saves, block completion and negative control;',costs)

if __name__=='__main__':main()
