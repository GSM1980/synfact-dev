"""Validate only the intended void callback cuts, with real init and callbacks.

Native event/metadata/heap/mutex and DSP bodies retain the activity fixture's
explicit boundaries. The unchanged C parser also runs all metadata mutations.
No device access, full scheduler or physical CPU percentage is claimed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
from test_st_lfo3_activity_emu import Activity
from test_st_module_startup_emu import BANK
from elekloader.sdk import elf


def words(raw,at,n):return struct.unpack_from('>'+str(n)+'I',raw,at)


class Audio(Activity):
    def run(self,mode):
        self.u.mem_map(0x10000000,0x10000)
        # Their real initializers only borrow the API/read the existing window.
        for name,init,_ in self.modules:
            if name in ('STEREO','AUDIO RECORDER'):self.real[init]=name
        return super().run(mode)

    def callback(self,address):
        self.sym['core:audio_probe']=address
        self.call('audio_probe')
        return dict(self.last)


def callbacks(raw):
    size,nrel=words(raw,52,2);start=80+4*nrel
    directory,count=words(raw,32,2)
    rows=[]
    for i in range(count):
        _,_,subs,n=words(raw,start+directory+16*i,4)
        for j in range(n):
            event,fn=words(raw,start+subs+8*j,2)
            if event in (5,10,13,14):rows.append((event,BANK+fn))
    return rows


def verify(core,reference,variants,parser):
    raw=reference.read_bytes();report=json.loads(reference.with_suffix('.smz4.json').read_text())
    linked=elf.Elf.load(reference.with_suffix('.work')/'main.elf')
    authored=[s for s in linked.symbols if s.name=='si_change' and s.type==elf.STT_FUNC and s.size==2]
    assert len(authored)==1
    known={
        'X-sin-frame-POLY':{'poly_module':{5}},
        'Y-sin-audio-STEREO-RECORDER':{'st_stereo_module':{5,10,13,14},'recorder_module':{10}},
        'Z1-sin-audio-STEREO':{'st_stereo_module':{5,10,13,14}},
        'Z2-sin-audio-RECORDER':{'recorder_module':{10}},
    }
    size,nrel=words(raw,52,2);start=80+4*nrel
    directory,count=words(raw,32,2)
    rows=[];reference_masks=None
    for path in (reference,*variants):
        subprocess.run([str(parser.resolve()),str(path.resolve())],check=True)
        candidate=path.read_bytes()
        if path!=reference:
            assert path.stem in known
            changes=json.loads(path.with_suffix('.smz4.json').read_text())['diagnostic']
            assert changes['reference_sha256']==hashlib.sha256(raw).hexdigest()
            noop=changes['void_noop_offset'];assert noop==authored[0].value
            assert raw[start+noop:start+noop+2]==bytes.fromhex('4e75')
            allowed=set(range(16,20))|set(range(68,72));found={k:set() for k in known[path.stem]}
            for i,name in enumerate(report['modules']):
                at=directory+16*i
                assert candidate[start+at:start+at+16]==raw[start+at:start+at+16],'initializer/directory changed'
                _,_,subs,n=words(raw,start+at,4)
                for j in range(n):
                    event,fn=words(raw,start+subs+8*j,2)
                    cut=event in known[path.stem].get(name,set())
                    assert words(candidate,start+subs+8*j,2)==(event,noop if cut else fn)
                    if cut:
                        found[name].add(event);allowed.update(range(start+subs+8*j+4,start+subs+8*j+8))
            assert found==known[path.stem]
            assert len(candidate)==len(raw)
            assert all(x==y or i in allowed for i,(x,y) in enumerate(zip(raw,candidate)))
        m=Audio(core,path)
        assert m.word(m.bs['modulation_skip_mode'])==2 and m.word(m.lfo['hooks_installed'])==1
        # Warm the real POLY cache against a valid stock sequence/Kit. A null
        # boot pointer would benchmark its retry/submit path every block.
        m.w(0x43bd7430,0x44080000);m.w(0x43bd743c,0x41ad5134)
        m.call('poly_sync')
        masks=bytes(m.u.mem_read(m.sym['core:core_native_interest'],24))
        if reference_masks is None:reference_masks=masks
        else:assert masks==reference_masks,'native routes changed'
        assert masks[17]==0 and masks[16]==0
        # Publish the real mod-info pointer, as machine-boot does after binding.
        m.w(m.sym['core:core_bank_info'],BANK+words(raw,60,1)[0])
        tick_rows=[]
        for label,_,events in m.modules:
            if label=='STEREO':
                # Read its packed descriptor/subscriptions, not a fabricated fn.
                i=next(i for i,n in enumerate(report['modules']) if n=='st_stereo_module')
                _,_,subs,n=words(candidate,start+directory+16*i,4)
                tick_rows=[BANK+fn for event,fn in (words(candidate,start+subs+8*j,2) for j in range(n)) if event==0]
        assert len(tick_rows)==1
        m.callback(tick_rows[0])
        assert m.word(m.bs['st_stereo_sram'])==1,'real stereo SRAM placement failed'
        cbs=callbacks(candidate);scenarios={}
        for tag in (0,0x53545232):
            m.w(0x100005c4,tag);blocks=[]
            for _ in range(4):
                cost=0
                for event in (5,13,14,10):
                    for op,fn in cbs:
                        if op==event:cost+=m.callback(fn)['instructions']
                blocks.append(cost)
                assert bytes(m.u.mem_read(0x8000dad0,256))==bytes(256),'stock bus changed'
            scenarios['tagged_empty' if tag else 'untagged']=blocks
        m.sym['core:probe_modulation']=0x40097afc
        for _ in range(128):m.call('probe_modulation',0x80006348)
        counts={k:m.word(m.bs['modulation_skip_'+k]) for k in ('blocks','hits','skips','errors')}
        assert counts==dict(blocks=128,hits=127,skips=124,errors=0)
        rows.append(dict(bank=path.name,callback_instructions=scenarios,stereo_in_SRAM=True,
                         native_routes=masks.hex(),**counts))
    for scenario in ('untagged','tagged_empty'):
        assert all(row['callback_instructions'][scenario][-1]<rows[0]['callback_instructions'][scenario][-1] for row in rows[1:])
    return dict(ok=True,cases=rows,code_layout_and_AUDIO_identical=True,
                initializers_and_native_masks_unchanged=True,device_access=False,
                limits=['Actual LFO3/ARP/POLY/Stereo/Recorder/MSKIP initialization and live lists.',
                        'Unchanged parser and metadata mutations; real void callbacks and SRAM placement.',
                        'Native metadata/event/heap/mutex and original DSP bodies are explicit boundaries.',
                        'No physical CPU timing, complete boot or scheduler.'])


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('core','reference','parser','out'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--variant',type=Path,action='append',required=True)
    a=p.parse_args();result=verify(a.core,a.reference,a.variant,a.parser)
    result['sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (a.reference,*a.variant)}
    a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
