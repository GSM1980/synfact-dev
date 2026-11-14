"""Compare full linked MSKIP calls and decisions against the previous bank.

Uses the owner's read-only stock conversion/modulation fixtures (SYNTAKT_WORK).
Native initialization/heap have their documented fixture boundaries. No device
IO or physical-cycle claims. Separate stock-oracle tests remain mandatory.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
if not os.environ.get('SYNTAKT_WORK'):
    raise SystemExit('SYNTAKT_WORK is required for the native reference fixture')
from test_st_modulation_skip_emu import load,fixture,bump,counter,RAW_MIRROR,MIRROR,WORKING,ON
from unicorn import UC_HOOK_CODE

NAMES=('zero','zero_destinations','static_one','moving_one','moving_three','moving_eleven',
       'fx_only','overlap','dense','unsafe','publication_zero','publication_one',
       'unknown_writer','corrupt_cache')
COUNTERS=('mode','blocks','hits','skips','partial_runs','errors','run_mask','machine_writes')


def prepare(bank,case):
    data,symbols=load(bank);m=fixture(5,data,symbols,ON)
    for t in range(13):
        for k in range(24):m.write32(RAW_MIRROR+124*t+4*k,0)
    active=([12] if case=='fx_only' else list(range(11)) if case=='moving_eleven'
            else [2,3,5] if case=='moving_three' else
            [2] if case in ('static_one','moving_one','publication_one','overlap','corrupt_cache','unsafe') else [])
    for t in active:m.write32(RAW_MIRROR+124*t+16,0x0f00001c)
    if case=='zero_destinations':
        for t in range(13):
            for k in range(24):m.write32(RAW_MIRROR+124*t+4*k,28+k%3)
    if case=='overlap':m.write32(RAW_MIRROR+124*2,28)
    if case=='dense':
        for t in range(13):
            for k in range(24):m.write32(RAW_MIRROR+124*t+4*k,0x01000000|k)
    if case=='unsafe':m.write32(RAW_MIRROR+124*2+16,0x0001ffff)
    m.bench_matrix=bytes(m.uc.mem_read(RAW_MIRROR+124*2,96))
    m.bench_count=0;m.bench_enabled=False
    def instruction(u,pc,n,user):
        if m.bench_enabled:m.bench_count+=1
    m.uc.hook_add(UC_HOOK_CODE,instruction)
    return m,symbols,active


def step(m,symbols,case,active,b):
    if case.startswith('publication_'):
        written=counter(m,symbols,'machine_writes')
        m.publish(2)
        assert counter(m,symbols,'machine_writes')==(written+2)&0xffffffff
        # The writer is real; its row's 24 matrix words are explicit fixture
        # inputs, as in prepare(). Supply the chosen zero/active matrix before
        # conversion while retaining the native writer's actual dirty hint.
        m.uc.mem_write(RAW_MIRROR+124*2,m.bench_matrix)
    if case=='unknown_writer' and b==33:
        m.write32(RAW_MIRROR+124*4+36,0x12340011)
    if case=='corrupt_cache' and b==33:
        at=symbols['post']+2*(24*2)
        m.uc.mem_write(at,struct.pack('>H',struct.unpack('>H',bytes(m.uc.mem_read(at,2)))[0]^1))
    # Per-block changing base controls and source values; no host modulation.
    if case=='zero_destinations':
        values=(0x4000,0x7fff,0x8000,0xbfff,0xc000,0xffff)
        for t in range(13):
            m.uc.mem_write(MIRROR+28+142*t+56,struct.pack('>3H',*[values[(b+i)%6] for i in range(3)]))
    m.call(0x40097952,MIRROR)
    if case.startswith('moving_') or case=='fx_only':
        for t in active:
            m.uc.mem_write(0x80004746+2*t,struct.pack('>H',0x2000+((b*131+t*37)&0x1fff)))
    m.bench_count=0;m.bench_enabled=True
    m.call(0x40097afc,WORKING)
    m.bench_enabled=False
    return m.bench_count


def edges(before,after):
    specs=[({},WORKING),({'ticks_irq':63},WORKING),({'count':63},WORKING),
           ({'ticks_irq':0xffffffff},WORKING),({'count':0xffffffff},WORKING),
           ({'modulation_skip_blocks':0xffffffff},WORKING),
           ({'modulation_skip_mode':0},WORKING),({'modulation_skip_mode':1},WORKING),
           ({'modulation_skip_mode':3},WORKING),({},WORKING+4),
           ({'unsafe_mask':1},WORKING),({'total_used':101},WORKING),
           ({'unsafe_mask':1,'ticks_irq':63},WORKING),
           ({'modulation_skip_machine_writes':2},WORKING),
           ({'raw_ready':0},WORKING),({'valid_mask':0},WORKING)]
    machines=[]
    for bank in (before,after):
        m,s,active=prepare(bank,'zero')
        for b in range(4):step(m,s,'zero',active,b)
        data,_=load(bank)
        machines.append((m,s,bytes(m.uc.mem_read(0x46000000,len(data))),bytes(m.uc.mem_read(0x80000000,0x20000))))
    fields=('ticks_irq','count','valid_mask','machine_seen','raw_ready','unsafe_mask','total_used','capture_mask','check_mask')
    for change,working in specs:
        results=[]
        for m,s,bankdata,sram in machines:
            m.uc.mem_write(0x46000000,bankdata);m.uc.mem_write(0x80000000,sram)
            for name,value in change.items():m.write32(s[name],value)
            decision=m.call(s.get('modulation_skip_dispatch',s['modulation_skip_decide']),working)
            values={name:counter(m,s,name) for name in COUNTERS}
            values.update({name:struct.unpack('>I',bytes(m.uc.mem_read(s[name],4)))[0] for name in fields})
            results.append((decision,values,bytes(m.uc.mem_read(0x80000000,0x20000))))
        assert results[0]==results[1],('dispatch edge',change,hex(working))
    return len(specs)


def verify(before,after,blocks,cases):
    rows={}
    for case in cases:
        old,os,oa=prepare(before,case);new,ns,na=prepare(after,case)
        counts=[[],[]]
        for b in range(blocks):
            counts[0].append(step(old,os,case,oa,b));counts[1].append(step(new,ns,case,na,b))
            assert bytes(old.uc.mem_read(0x80000000,0x20000))==bytes(new.uc.mem_read(0x80000000,0x20000)),(case,b,'SRAM')
            for name in COUNTERS:
                assert counter(old,os,name)==counter(new,ns,name),(case,b,name)
        rows[case]=dict(before=sum(counts[0]),after=sum(counts[1]),
                        steady_before=counts[0][10],steady_after=counts[1][10],
                        first_before=counts[0][0],first_after=counts[1][0],
                        max_before=max(counts[0]),max_after=max(counts[1]),
                        per_block_before=counts[0],per_block_after=counts[1],
                        final={name:counter(new,ns,name) for name in COUNTERS})
        print(case,'total',sum(counts[0]),'->',sum(counts[1]),'steady',counts[0][10],'->',counts[1][10],flush=True)
    return dict(ok=True,blocks_per_case=blocks,rows=rows,dispatch_edge_cases=edges(before,after),SRAM_and_decisions_identical=True,
                bank_sha256={k:hashlib.sha256(v.read_bytes()).hexdigest() for k,v in (('before',before),('after',after))},
                device_access=False,physical_cycles_measured=False)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('before','after','out'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--blocks',type=int,default=128)
    p.add_argument('--require-no-regressions',action='store_true')
    p.add_argument('--case',choices=NAMES,action='append')
    a=p.parse_args();assert a.blocks>=128
    result=verify(a.before,a.after,a.blocks,a.case or NAMES)
    if a.require_no_regressions:
        for name,row in result['rows'].items():
            assert row['after']<=row['before'],('total instruction regression',name)
            assert row['steady_after']<=row['steady_before'],('steady instruction regression',name)
    a.out.parent.mkdir(parents=True,exist_ok=True)
    a.out.write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: linked SRAM/counters/decisions and ABI; instructions are not hardware CPU percentages')
