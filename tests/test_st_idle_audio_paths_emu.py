"""Linked idle audio callbacks and Recorder transitions against the prior bank.

Uses actual core/module initialization and Recorder C workers. Native metadata,
heap/mutex and original LFO DSP retain the activity fixture's boundaries.
Real stereo EMAC is checked separately with test_st_stereo_main_current.py.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
from test_st_idle_audio_cuts_emu import Audio,callbacks,words
from test_st_module_startup_emu import BANK

PCM,VIEW,MANAGER,NODE=0x44200000,0x44210000,0x44211000,0x44212000
SCALARS=('sam_enabled','sam_source','sam_clock','sam_state','sam_frames',
         'sam_peak','sam_peak_r','sam_last_source','sam_play_length','sam_play_pos',
         'sam_pcm_peak','sam_ret_clock','headroom_meter_phase','headroom_meter_signature',
         'headroom_normalizing_skips','headroom_ready_skips',
         'headroom_ui_meter_samples','headroom_ui_meter_skipped_blocks',
         'headroom_armed_quiet_blocks')


def events(bank,module):
    raw=bank.read_bytes();_,nr=words(raw,52,2);start=80+4*nr
    directory,count=words(raw,32,2)
    names=json.loads(bank.with_suffix('.smz4.json').read_text())['modules']
    assert count==len(names)
    _,_,subs,n=words(raw,start+directory+16*names.index(module),4)
    return {op:BANK+fn for op,fn in (words(raw,start+subs+8*j,2) for j in range(n))}


def recorder(m,fn,scenario):
    enabled,source,state,stereo,clock,preview,closed=scenario
    for name in SCALARS:m.w(m.bs[name],0)
    values=dict(sam_enabled=enabled,sam_source=source,sam_state=state,
                sam_stereo=stereo,sam_take_stereo=stereo,sam_post=1,sam_take_post=1,
                sam_clock=clock,sam_ret_clock=clock,sam_peak=12345,sam_peak_r=23456,
                sam_frames=8 if state==3 else 0,sam_limit=32,sam_trigger=0,
                sam_mon=1,sam_take_source=source,sam_pcm=PCM,sam_view=VIEW,
                sam_manager=MANAGER,sam_play_length=preview,sam_play_pos=2,
                headroom_meter_phase=24)
    for name,value in values.items():m.w(m.bs[name],value)
    m.u.mem_write(VIEW,bytes(256));m.u.mem_write(VIEW+0x30,bytes([closed]))
    m.w(MANAGER+0x1c,NODE);m.w(NODE+8,VIEW)
    m.u.mem_write(PCM,struct.pack('>128h',*[(i-64)*101 for i in range(128)]))
    for name,n in (('sam_ret',512),('sam_report',32)):m.u.mem_write(m.bs[name],bytes(n))
    for at in (0x8000dad0,0x8000dfd0,0x8000dcd0,0x8000ddd0):
        m.u.mem_write(at,struct.pack('>64i',*[i*12345-300000 for i in range(64)]))
    m.u.mem_write(0x80004760,struct.pack('>640i',*[(i-320)*4096 for i in range(640)]))
    cost=m.callback(fn)['instructions']
    snapshot={name:m.word(m.bs[name]) for name in SCALARS}
    for name,n in (('sam_ret',512),('sam_report',32)):
        snapshot[name]=bytes(m.u.mem_read(m.bs[name],n)).hex()
    snapshot['PCM']=bytes(m.u.mem_read(PCM,256)).hex()
    snapshot['preview_bus']=bytes(m.u.mem_read(0x8000dfd0,256)).hex()
    return snapshot,cost


def verify(core,before,after):
    def recorder_object(bank):
        report=json.loads(bank.with_suffix('.smz4.json').read_text())
        values=[digest for name,digest in report['object_sha256'].items()
                if Path(name).name=='main2-0.o']
        assert len(values)==1
        return values[0]
    recorder_changed=recorder_object(before)!=recorder_object(after)
    machines=[Audio(core,bank) for bank in (before,after)]
    rec=[events(bank,'recorder_module')[10] for bank in (before,after)]
    results=[]
    for tag in (0,0x53545232):
        row=[]
        for m,bank in zip(machines,(before,after)):
            m.w(0x43bd7430,0x44080000);m.w(0x43bd743c,0x41ad5134);m.call('poly_sync')
            raw=bank.read_bytes()
            m.w(m.sym['core:core_bank_info'],BANK+words(raw,60,1)[0])
            m.callback(events(bank,'st_stereo_module')[0])
            assert m.word(m.bs['st_stereo_sram'])==1
            m.w(0x100005c4,tag)
            costs=[]
            for _ in range(4):
                cost=0
                for event in (5,13,14,10):
                    for op,fn in callbacks(raw):
                        if op==event:cost+=m.callback(fn)['instructions']
                costs.append(cost)
                assert bytes(m.u.mem_read(0x8000dad0,256))==bytes(256)
            row.append(costs)
        assert row[1][-1]<row[0][-1],row
        results.append(dict(tag=hex(tag),before=row[0],after=row[1]))
    cases=[]
    for enabled in (0,1):
        for source in (3,4,5,18):
            for state in (0,1,2,3,4):
                for stereo in (0,1):
                    for preview in (0,7):
                        # Native per-source capture is a separate host oracle;
                        # this fixture executes the module's MAIN L+R worker.
                        if enabled and stereo and source in (3,4):continue
                        cases.append((enabled,source,state,stereo,0xfffffff0,preview,0))
    cases.extend((1,5,2,0,0,preview,1) for preview in (0,7))
    measurements={}
    for scenario in cases:
        a,ac=recorder(machines[0],rec[0],scenario)
        b,bc=recorder(machines[1],rec[1],scenario)
        assert a==b,(scenario,{k:(a[k],b[k]) for k in a if a[k]!=b[k]})
        if not scenario[0] and not scenario[5]:
            if recorder_changed:assert bc<ac,(scenario,ac,bc)
            else:assert bc==ac,(scenario,ac,bc)
            measurements['closed_no_audition']=dict(before=ac,after=bc)
    # Independent closed-view contract, including the 32-bit clock wrap.
    scenario=(0,5,0,0,0xfffffff0,0,0)
    actual,_=recorder(machines[1],rec[1],scenario)
    assert actual['sam_clock']==16 and actual['headroom_meter_phase']==23
    return dict(ok=True,recorder_transition_cases=len(cases),recorder_costs=measurements,
                complete_idle_callbacks=results,closed_clock_wrap_checked=True,
                stereo_in_SRAM=True,device_access=False,
                limits=['Native metadata/heap/mutex and original LFO DSP are boundaries.',
                        'Recorder workers execute; stereo EMAC checked separately on analog-emu.',
                        'Instruction counts are not physical cycles or CPU percentages.'])


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('core','before','after','out'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();result=verify(a.core,a.before,a.after)
    result['bank_sha256']={k:hashlib.sha256(v.read_bytes()).hexdigest() for k,v in (('before',a.before),('after',a.after))}
    a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
