"""Build same-layout diagnostic banks from local sources, never an input SMZ.

Keeps every machine, AUDIO byte, executable byte, image offset and relocation.
Disabled modules retain their list entries, routed to the authored C-ABI no-op;
their initialization uses the diagnostic initializer (native interest zero).
These are stock-pattern diagnostics, not functional replacements for mods.
"""
import argparse
import hashlib
import json
import shutil
import struct
import sys
import zlib
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from elekloader.sdk import elf
from elekloader.sdk.syntakt.smz import build

KEEP={'system_info_module','modulation_skip_module','st_idle_diag_module'}


def words(raw,at,n):
    return struct.unpack_from('>'+str(n)+'I',raw,at)


def disable(raw,report,symbols,disabled,callbacks_only=None):
    """Only authored module directory/registered callback data may change."""
    result=bytearray(raw)
    size,nrel=words(raw,52,2)
    start=80+4*nrel
    directory,count=words(raw,32,2)
    rels=set(words(raw,80,nrel))
    assert count==len(report['modules'])
    changes=[]
    callbacks_only=callbacks_only or {}

    def change(at,value,reason):
        assert at in rels and 0<=value<size and not value&1
        before=words(result,start+at,1)[0]
        struct.pack_into('>I',result,start+at,value)
        changes.append(dict(offset=at,before=before,after=value,reason=reason))

    for i,name in enumerate(report['modules']):
        if name not in disabled and name not in callbacks_only:continue
        assert name not in KEEP, name
        at=directory+16*i
        _,init,subscriptions,n=words(raw,start+at,4)
        if name in disabled and init!=0xffffffff:
            change(at+4,symbols['st_idle_diag_initialize'],name+' init')
        for j in range(n):
            event=words(raw,start+subscriptions+8*j,1)[0]&255
            if name in disabled or event in callbacks_only.get(name,()):
                change(subscriptions+8*j+4,symbols['st_idle_diag_noop'],name+' callback '+str(j))
    allowed={16,17,18,19,68,69,70,71}
    for change_row in changes:
        allowed.update(range(start+change_row['offset'],start+change_row['offset']+4))
    body=result[80:start+size]
    struct.pack_into('>I',result,68,sum(words(body,0,len(body)//4))&0xffffffff)
    struct.pack_into('>I',result,16,zlib.adler32(result[20:])&0xffffffff)
    assert len(result)==len(raw)
    assert all(a==b or i in allowed for i,(a,b) in enumerate(zip(raw,result)))
    assert result[80:start]==raw[80:start], 'relocations moved'
    audio_at,audio_bytes=words(raw,72,2)
    assert result[48+audio_at:48+audio_at+audio_bytes]==raw[48+audio_at:48+audio_at+audio_bytes]
    return bytes(result),changes


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args()
    assert not a.out.exists(), 'use a fresh output directory'
    reference=a.out/'R-completo.smz4'
    report=build([ROOT/'mods/samplers-st',ROOT/'mods/fm-tone-st'],output=reference,
                 module_directories=[ROOT/'examples/idle-bisect-st',
                    *[ROOT/('mods/'+name) for name in ('recorder-st','lfo3-st','arp-st','poly-st',
                       'system-info-smz-st','sample-manager-st','modulation-skip-st')]])
    executable=reference.with_suffix('.work')/'main.elf'
    symbols={s.name:s.value for s in elf.Elf.load(executable).symbols if s.name and s.bind==1}
    raw=reference.read_bytes()
    disabled=set(report['modules'])-KEEP
    catalog,changes=disable(raw,report,symbols,disabled)
    path=a.out/'S-catalogo-sin-callbacks.smz4'
    path.write_bytes(catalog)
    copied=dict(report,sha256=hashlib.sha256(catalog).hexdigest(),diagnostic=dict(
        reference_sha256=report['sha256'],disabled_modules=sorted(disabled),
        changes=changes,executable_bytes_unchanged=True,relocations_unchanged=True,
        AUDIO_unchanged=True,stock_pattern_only=True))
    path.with_suffix('.smz4.json').write_text(json.dumps(copied,indent=2)+'\n')
    path.with_suffix('.work').mkdir()
    shutil.copyfile(executable,path.with_suffix('.work')/'main.elf')
    summary=dict(ok=True,reference=reference.name,catalog=path.name,
                 disabled_modules=sorted(disabled),metadata_changes=len(changes),
                 AUDIO_unchanged=True,same_linked_image_offsets=True,
                 remaining_noop_callback_overhead=True,device_access=False)
    (a.out/'bisect.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary,indent=2))


if __name__=='__main__':main()
