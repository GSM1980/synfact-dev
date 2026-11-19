"""Exhaustive zero-depth projection on analog-emu's physical EMAC model."""
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
sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
from test_st_stereo_main_current import bank_image
from elekloader.devices.syntakt import DEVICE
from elekloader.sdk.elf import Elf
BASE,HELPER=0x40400000,0x40500000


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('bank','cfbench','out'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    def tool(name):
        prefix=os.environ.get('ELEKLOADER_CROSS',DEVICE.toolchain['prefix'])
        return str(Path(os.environ['SGDK_BIN'])/(prefix+name+('.exe' if os.name=='nt' else '')))
    def run(cmd):subprocess.run([str(v) for v in cmd],check=True,cwd=ROOT,capture_output=True,text=True)
    co,so,ld,exe,binpath=[a.out/x for x in ('model.o','abi.o','model.ld','model.elf','model.bin')]
    run([tool('gcc'),*DEVICE.toolchain['cflags'],'-fno-jump-tables','-c','tests/st_modulation_skip_cfbench.c','-o',co])
    run([tool('as'),'-mcpu=54415','tests/st_stereo_cfbench.s','-o',so])
    ld.write_text('SECTIONS { . = 0x40500000; .image : { *(.text*) *(.rodata*) *(.data*) *(.bss*) *(COMMON) } }\n')
    run([tool('ld'),'--emit-relocs','-T',ld,'-o',exe,co,so])
    run([tool('objcopy'),'-O','binary','-j','.image',exe,binpath])
    data,symbols,_,_,_=bank_image(a.bank,BASE)
    hs={s.name:s.value for s in Elf.load(exe).symbols if s.name}
    image=bytearray(HELPER-BASE+len(binpath.read_bytes()));image[:len(data)]=data
    image[HELPER-BASE:]=binpath.read_bytes();path=a.out/'fixture.bin';path.write_bytes(image)
    def invoke(negative=False):
        r=subprocess.run([str(a.cfbench),str(path),f'{BASE:x}',
                          f"{hs['check_projection']:x}:{symbols['modulation_skip_project_all']:x}"],capture_output=True,text=True,check=True)
        matches=re.findall(r'-> d0 ([0-9a-f]+): (\d+) instructions',r.stdout)
        assert len(matches)==1,r.stdout
        result,n=int(matches[0][0],16),int(matches[0][1])
        assert bool(result)==negative,(result,r.stdout)
        return dict(result=result,instructions_including_fixture=n)
    normal=invoke()
    # Replace the helper entry with RTS: exhausting all 16-bit values must fail.
    at=symbols['modulation_skip_project_all']-BASE;image[at:at+2]=bytes.fromhex('4e75');path.write_bytes(image)
    negative=invoke(True)
    result=dict(ok=True,unsigned16_values=65536,exact_output_ABI_EMAC_and_canaries=True,
                normal=normal,missing_projection_negative_control=negative,
                bank_sha256=hashlib.sha256(a.bank.read_bytes()).hexdigest(),device_access=False)
    (a.out/'results.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))


if __name__=='__main__':main()
