"""Bound COMP's cache footprint from actual linked instruction/data accesses.

Reports distinct cached lines touched in one steady FX callback window;
SRAM accesses bypass both caches. These are warm/cold footprint bounds,
not an interleaved whole-firmware cache simulation or hardware timings.
"""
import argparse
import json
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from test_st_comp_emu import fixture,set_values,block,W,ID
from unicorn import UC_HOOK_CODE,UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE


def profile(core,bank,sram=False):
    m=fixture(core,bank)
    if sram:assert m.call('comp_place')==3
    set_values(m,[0x4000,0x1800,0x2000,0x2000,0x300,0x7f00])
    m.w(W+0x1d00,m.sym['comp_audio']);m.w(W+0x1d04,0)
    m.w(m.cs['core:core_fx_cv_table']+2,W+0x1d00)
    ins=[];access=[]
    h=m.u.hook_add(UC_HOOK_CODE,lambda u,p,n,x:ins.append((p,n)))
    j=m.u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,lambda u,t,a,n,v,x:access.append((a,n)))
    # Read instrumentation is compiled into translated blocks. Install it
    # before warmup; adding it after translation undercounts ordinary reads.
    m.u.ctl_remove_cache(0x40000000,0x80010000)
    for _ in range(32):block(m,[2**29]*32,[-2**29]*32,through_core=True)
    ins.clear();access.clear()
    block(m,[2**29]*32,[-2**29]*32,through_core=True)
    def lines(events,lo,hi):
        return {a//16 for p,n in events for a in range(p,p+n) if lo<=a<hi}
    il=lines(ins,0x40000000,0x48000000)
    # Only bank-owned data. Fixture IRQ stack/application globals are not a
    # claim about their actual hardware cacheability or whole-system misses.
    dl=lines(access,0x47000000,0x47200000)
    n=len(ins)-6
    si=sum(1 for p,_ in ins if 0x80000000<=p<0x80010000)
    adc_reads=sum(1 for p,_ in access if 0x80003c50<=p<0x80003d50)
    assert adc_reads==(64 if 'comp_hot' in m.sym else 65),'incomplete memory instrumentation'
    controls={}
    for i,name in enumerate(('THR','ATK','REL','MUP','RAT','MIX')):
        value=m.call('comp_param_get',ID+i)
        ins.clear();access.clear()
        m.call(0x4000da32,W,ID+i,value+256 if i!=5 else value-256,0,0,1)
        controls[name]=len(ins)
    m.u.hook_del(h);m.u.hook_del(j)
    return dict(extra_instructions=n,cached_instruction_lines=len(il),bank_data_lines=len(dl),
        control_task_instructions=controls,sram_instructions=si,adc_reads=adc_reads,
        code_bytes=len(il)*16,bank_data_bytes=len(dl)*16,
        warm_proxy_cycles=.85*n,cold_proxy_cycles=.85*n+40*(len(il)+len(dl)))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('core','bank','previous','out'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args()
    rows=dict(previous=profile(a.core,a.previous),optimized_ddr=profile(a.core,a.bank),optimized_sram=profile(a.core,a.bank,True))
    a.out.write_text(json.dumps(dict(cases=rows,
        units='guest instructions and distinct 16-byte cached lines in the FX window',
        proxy='0.85 cycles/instruction + 40/fill; cold assumes every distinct line misses once; warm assumes no misses',
        excludes='native interleaved traffic, task setters, IRQ stack/globals and real hardware timing',device_access=False),indent=2)+'\n')
    for name,row in rows.items():print(name,row)
