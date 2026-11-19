"""Cut void audio callbacks in a source-verified production bank, without linking.

Reuses System Info's existing authored empty void callback (exact RTS), so no
diagnostic code, initializer, event, native mask or image offset is added.
X cuts POLY render-in sync; Y cuts stereo's four audio events and Recorder's
main mix. These variants are for stock-pattern measurements only.
The split set cuts stereo (Z1) and Recorder (Z2) independently.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from elekloader.sdk import elf

CUTS={
    'X-sin-frame-POLY':{'poly_module':{5}},
    'Y-sin-audio-STEREO-RECORDER':{'st_stereo_module':{5,10,13,14},'recorder_module':{10}},
}
SPLIT_CUTS={
    'Z1-sin-audio-STEREO':{'st_stereo_module':{5,10,13,14}},
    'Z2-sin-audio-RECORDER':{'recorder_module':{10}},
}


def words(raw,at,n):return struct.unpack_from('>'+str(n)+'I',raw,at)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--reference-build',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--set',choices=('xy','split'),default='xy',dest='cut_set')
    a=p.parse_args()
    assert not a.out.exists(),'use a fresh output directory'
    bank=a.reference_build/'synfact-machines.smz4'
    report=json.loads(bank.with_suffix('.smz4.json').read_text())
    assert sha(bank)==report['sha256'] and 'diagnostic' not in report
    for group in ('source_sha256','object_sha256'):
        for name,digest in report[group].items():assert sha(Path(name))==digest,name
    executable=bank.with_suffix('.work')/'main.elf'
    linked=elf.Elf.load(executable)
    section=next(s for s in linked.sections if s.name=='.image')
    matches=[s for s in linked.symbols if s.name=='si_change' and s.type==elf.STT_FUNC]
    assert len(matches)==1
    noop=matches[0]
    assert noop.size==2 and section.data[noop.value:noop.value+2]==bytes.fromhex('4e75')
    raw=bank.read_bytes();size,nrel=words(raw,52,2);start=80+4*nrel
    assert raw[start+noop.value:start+noop.value+2]==bytes.fromhex('4e75')
    rels=set(words(raw,80,nrel));directory,count=words(raw,32,2)
    assert count==len(report['modules'])
    a.out.mkdir(parents=True)
    results=[]
    for filename,cut in (CUTS if a.cut_set=='xy' else SPLIT_CUTS).items():
        result=bytearray(raw);changes=[];found={name:set() for name in cut}
        for i,name in enumerate(report['modules']):
            if name not in cut:continue
            _,_,subs,n=words(raw,start+directory+16*i,4)
            for j in range(n):
                event,fn=words(raw,start+subs+8*j,2)
                if event not in cut[name]:continue
                assert event in (5,10,13,14),'only known void audio signatures'
                at=subs+8*j+4;assert at in rels
                struct.pack_into('>I',result,start+at,noop.value)
                found[name].add(event)
                changes.append(dict(module=name,event=event,offset=at,before=fn,after=noop.value))
        assert found==cut,found
        body=result[80:start+size]
        struct.pack_into('>I',result,68,sum(words(body,0,len(body)//4))&0xffffffff)
        struct.pack_into('>I',result,16,zlib.adler32(result[20:])&0xffffffff)
        allowed=set(range(16,20))|set(range(68,72))
        for row in changes:allowed.update(range(start+row['offset'],start+row['offset']+4))
        assert len(result)==len(raw) and all(x==y or i in allowed for i,(x,y) in enumerate(zip(raw,result)))
        target=a.out/(filename+'.smz4');target.write_bytes(result)
        diagnostic=dict(kind='void-audio-cut',reference_sha256=report['sha256'],changes=changes,
                        void_noop_offset=noop.value,stock_pattern_only=True,
                        executable_bytes_unchanged=True,initializers_unchanged=True,AUDIO_unchanged=True)
        target.with_suffix('.smz4.json').write_text(json.dumps(dict(report,sha256=sha(target),diagnostic=diagnostic),indent=2)+'\n')
        target.with_suffix('.work').mkdir()
        shutil.copyfile(executable,target.with_suffix('.work')/'main.elf')
        results.append(dict(file=target.name,sha256=sha(target),**diagnostic))
    (a.out/'cuts.json').write_text(json.dumps(dict(ok=True,variants=results,device_access=False),indent=2)+'\n')
    print('Generated from exact production bank',report['sha256'])
    for row in results:print(row['file'],len(row['changes']),'callback pointers; code/layout/AUDIO unchanged')


if __name__=='__main__':main()
