# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the Syntakt core and its source-built AUDIO machine companion.

No machine code is included: machine banks are independent SMZ4 files in /mods.
Ordinary firmware mods remain normal SDK modules in the core's 64 KiB window.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from elekloader import formats,link,syx
from elekloader.sdk import build as sdk,elf
from .audio import build as audio_build,audit
from .pool_audio import build as pool_build

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stock',required=True)
    p.add_argument('--eft',required=True)
    p.add_argument('--out',default=str(ROOT/'out/core-st-machines'))
    p.add_argument('--system-info',action='store_true',help='build AUDIO telemetry; include system-info-smz-st for its UI')
    p.add_argument('--mod',action='append',default=[],help='additional ordinary SDK mod directory')
    a=p.parse_args();out=Path(a.out).resolve();out.mkdir(parents=True,exist_ok=True)
    if out.is_relative_to(ROOT):
        subprocess.run(['git','check-ignore',str(out/'artifact.bin')],cwd=ROOT,check=True,capture_output=True)
    stock,dev,release=formats.load(a.stock)
    if dev.key!='syntakt' or release.version!='1.41':raise ValueError('requires official Syntakt OS 1.41')
    mods=[]
    dirs=[ROOT/'mods/core-st',ROOT/'mods/machines-st']
    # --system-info retains AUDIO telemetry; its UI now lives in the SMZ.
    # An ordinary resident build remains available explicitly via --mod.
    dirs.extend(Path(m).resolve() for m in a.mod)
    pool_source,pool_report=pool_build(out/'pool-audio')
    for folder in dirs:
        extra={'sources':[str(pool_source)]} if folder==ROOT/'mods/machines-st' else None
        path,mod=sdk.build(str(folder),a.stock,extra=extra);print('BUILT',path,flush=True);mods.append(mod)
    audits={}
    for name in ('smz.c','machines.c','machine-hooks.s','machine-boot.c','machine-boot.s',
                 'machine-pages.c','machine-pages.s','machine-params.c',
                 'files.c','storage.c','hzfs.c','high-storage.c','high-storage.s',
                 'sample-pool.c','sample-manager.c','sample-hooks.s','sample-ui.s',
                 'machine-ui.c','machine-ui.s','sample-wave.c','crc.c','modules.c','modules.s',
                 'machine-host.c','native.c','native.s','view-draw.s'):
        obj=elf.Elf.load(ROOT/'mods/core-st/out/core.work'/(name+'.o'))
        section=obj.section('.text' if name.endswith('.c') else '.run')
        if not section:continue
        try:count=audit(section.data)
        except ValueError as exc:raise ValueError('MAIN '+name+': '+str(exc)) from exc
        audits[name]={'bytes':len(section.data),'instructions':count}
    linked=link.link(mods,stock.section(3))
    event_operands={}
    for name in ('tick','draw','key','enc','settings','render_in','render_out',
                 'fs_route','fs_write','fs_written','main_mix'):
        address=linked.map['core:core_'+name+'_table']
        if not linked.layout['ddr'][0]<=address<linked.layout['ddr'][1]-5:
            raise ValueError('SMZ event operand outside core RAM')
        at=linked.layout['run_load']-dev.main_load+address-linked.layout['ddr'][0]
        opcode,target=struct.unpack_from('>HI',linked.image,at)
        if opcode&0xf1ff!=0x41f9 or target!=linked.map['ev_'+name]:
            raise ValueError('SMZ event operand is not a core RAM absolute LEA')
        event_operands[name]={'instruction':hex(address),'list':hex(target)}
    audio,ainfo=audio_build(stock,dev,out/'audio',a.system_info)
    main_bin=out/'main.bin';audio_bin=out/'audio.bin'
    main_bin.write_bytes(linked.image);audio_bin.write_bytes(audio)
    output=out/('Syntakt_OS1.41_Elekloader_Core'+mods[0].version+('_SystemInfo' if a.system_info else '')+'_EXPERIMENTAL.syx')
    if output.exists():raise ValueError('output already exists; choose a new --out directory')
    run=subprocess.run([a.eft,'-i',str(Path(a.stock).resolve()),'-c','3',str(main_bin),
        '-c','7',str(audio_bin),'-l','1','-o',str(output)],capture_output=True,text=True)
    (out/'packaging.log').write_text(run.stdout+run.stderr,encoding='utf-8')
    if run.returncode:raise ValueError('compact packaging failed; see packaging.log')
    raw=output.read_bytes();facts=syx.verify(raw,stock,linked.image,dev,secondary={7:audio})
    if facts['main']['inplace_min_gap']<4096:raise ValueError('MAIN decompression margin is below 4096 bytes')
    built=syx.Syx(raw);protected=(1,2,4,5,6,8)
    if built.header!=stock.header:raise ValueError('version/container header changed')
    for sid in protected:
        if built.stored[sid]!=stock.stored[sid]:raise ValueError('protected section changed: '+str(sid))
    used=linked.layout['bss'][1]-linked.layout['ddr'][0]
    report={'stock_sha256':stock.sha256,'sha256':hashlib.sha256(raw).hexdigest(),'output':facts,
        'cflags':dev.toolchain['cflags'],'asflags':dev.toolchain['asflags'],
        'resident_ram':{'capacity':65536,'used':used,'free':65536-used},
        'link':linked.layout,'symbols':linked.map,'audio':ainfo,'pool_audio':pool_report,'instructions':audits,
        'smz_event_operands':event_operands,
        'protected_sections':{str(s):{'stock':True,'sha256':hashlib.sha256(built.stored[s]).hexdigest()} for s in protected},
        'machine_code_in_firmware':False,'emulator_run':False,'hardware_tested':False,'sent_to_device':False}
    (out/'verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('VERIFIED',output,flush=True)
    print('MAIN RAM:',used,'/ 65536; FREE:',65536-used,'bytes (%.2f KiB)'%((65536-used)/1024),'; AUDIO runtime:',ainfo['bytes'],'+ upper',ainfo.get('upper_bytes',0))
    print('Bootloader/bootstrap/updater sections and destinations unchanged.')

if __name__=='__main__':main()
