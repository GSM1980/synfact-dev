# SPDX-License-Identifier: AGPL-3.0-or-later
"""Physical-register EMAC model versus independent split integer FM+ backend."""
import argparse,hashlib,json,os,re,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader.devices.syntakt import DEVICE
from elekloader.sdk.elf import Elf


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cfbench',type=Path,required=True)
    p.add_argument('--sgdk-bin',type=Path,default=os.environ.get('SGDK_BIN'))
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--hq',action='store_true',help='test the retained 2x reference instead of the shipped balanced backend');a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=True);assert a.sgdk_bin
    flags=DEVICE.toolchain['cflags']+['-fno-jump-tables','-I','mods/digimono-st']
    if a.hq:flags.append('-DDM_FM_HQ')
    def run(args):return subprocess.run([str(x) for x in args],check=True,capture_output=True,text=True)
    objects=[]
    for name,src,extra in (
        ('target','mods/digimono-st/fmplus.c',[]),
        ('portable','mods/digimono-st/fmplus.c',['-DDM_FM_SPLIT',
          '-Ddm_fm_init=portable_init','-Ddm_fm_trig=portable_trig',
          '-Ddm_fm_half=portable_half','-Ddm_fm_render=portable_render']),
        ('driver','tests/st_fmplus_cfbench.c',[])):
        obj=a.out/(name+'.o');run([a.sgdk_bin/'gcc.exe',*flags,*extra,'-c',src,'-o',obj]);objects.append(obj)
    script=a.out/'image.ld';script.write_text('SECTIONS { .image 0x40001000 : { *(.text*) *(.rodata*) *(.data*) BYTE(0) . = ALIGN(4); *(.bss*) *(COMMON) . = ALIGN(4); } /DISCARD/ : { *(.comment) *(.note*) *(.gnu.attributes) } }\n')
    exe=a.out/'image.elf';binary=a.out/'image.bin'
    run([a.sgdk_bin/'ld.exe','-T',script,*objects,'-o',exe])
    run([a.sgdk_bin/'objcopy.exe','-O','binary','-j','.image',exe,binary])
    syms={s.name:s.value for s in Elf.load(exe).symbols if s.name}
    result=run([a.cfbench,binary,'40001000',f'{syms["check_fm"]:x}:20,65815630'])
    values=re.findall(r'-> d0 ([0-9a-f]{8}): (\d+) instructions',result.stdout)
    assert len(values)==1 and int(values[0][0],16)==0,result.stdout
    (a.out/'results.json').write_text(json.dumps(dict(ok=True,backend='hq' if a.hq else 'balanced',blocks=768,
        instructions=int(values[0][1]),physical_emac_model=True,hardware_cycles=False,
        all_accumulators_extensions_and_macsr=True,
        source_sha256=hashlib.sha256(Path('mods/digimono-st/fmplus.c').read_bytes()).hexdigest(),
        tables_sha256=hashlib.sha256(Path('mods/digimono-st/fmplus/tables.h').read_bytes()).hexdigest()),indent=2)+'\n')
    print('PASS: 768 EMAC/split PCM and full voice/EMAC-state blocks on cfbench; no device access')


if __name__=='__main__':main()
