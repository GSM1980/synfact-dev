"""Linked native service ABI/results/profiling oracle and instruction comparison.

The actual wrapper and dispatcher run; callbacks and the unchanged stock replay
are explicit boundaries. No device access, EMAC use or hardware-cycle claim.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
from test_st_native_routes_emu import Routes,LIST,ARGS,RESULT,CALLBACK,pack
from unicorn import UC_HOOK_MEM_READ
from unicorn.m68k_const import UC_M68K_REG_D0,UC_M68K_REG_D1,UC_M68K_REG_A0,UC_M68K_REG_A1

PROFILE,OTHER,TIMER=0x44003004,0x44004004,0xfc07000c


class Service(Routes):
    def __init__(self,folder):
        self.original=[];self.timer_reads=0;self.no_write=False
        super().__init__(folder)
        self.u.mem_map(0xfc070000,0x1000)
        def timer(u,access,at,size,value,user):
            assert at==TIMER and size==4
            self.w(TIMER,0xfffffffd+7*self.timer_reads);self.timer_reads+=1
        self.u.hook_add(UC_HOOK_MEM_READ,timer,begin=TIMER,end=TIMER+3)

    def code(self,u,pc,size,user):
        if pc==self.sym.get('core:core_native_original'):
            self.instructions+=1
            op,args=self.args(2)
            assert args==ARGS
            self.original.append(op)
            for reg in (UC_M68K_REG_D1,UC_M68K_REG_A0,UC_M68K_REG_A1):u.reg_write(reg,0xa55a5aa5)
            self.ret(0x6a000000^op);return
        if CALLBACK<=pc<CALLBACK+16*32 and (pc-CALLBACK)%32==0:
            op,args,result=self.args(3)
            assert op==self.operation and args==ARGS
            self.instructions+=1;self.hits.append(pc)
            if pc==self.mutator:return
            if not self.no_write:self.w(result,pc^op)
            for reg in (UC_M68K_REG_D1,UC_M68K_REG_A0,UC_M68K_REG_A1):u.reg_write(reg,0xa55a5aa5)
            self.ret(7 if pc==self.consume else 0);return
        super().code(u,pc,size,user)

    def reset_profile(self,at):
        self.w(at-4,0x1234a55a);self.w(at+144,0xa55a1234)
        self.u.mem_write(at,pack(*([0xffffffff]*36)))

    def profile_words(self,at):
        assert self.word(at-4)==0x1234a55a and self.word(at+144)==0xa55a1234
        return list(struct.unpack('>36I',bytes(self.u.mem_read(at,144))))


def verify(folder):
    m=Service(folder);rows=[]
    for profiled in (False,True):
        for count in (0,3,8):
            entries=[CALLBACK+32*i for i in range(count)]
            m.u.mem_write(LIST,pack(*entries,0));m.w(m.sym['core:core_native_list'],LIST)
            for route in (0,1,2,5,128):
                m.u.mem_write(m.sym['core:core_native_interest'],bytes([route])*24)
                for op in (*range(22),22,0xffffffff):
                    for take in ('none','first','last','empty_result'):
                        m.operation=op;m.original=[];m.hits=[];m.timer_reads=0
                        m.no_write=take=='empty_result'
                        m.consume=(entries[-1] if take=='last' else entries[0]) if entries and take!='none' else None
                        want=[]
                        if op<22 and route:
                            for i,fn in enumerate(entries):
                                if op in (18,19) or route&128 or route>>i&1:
                                    want.append(fn)
                                    if fn==m.consume:break
                        consumed=bool(want) and want[-1]==m.consume
                        expected=(0 if m.no_write else want[-1]^op) if consumed else 0x6a000000^op
                        m.reset_profile(PROFILE)
                        m.invoke('core_native_profile',PROFILE if profiled else 0)
                        m.instructions=0
                        result=m.invoke('core_native',op,ARGS)
                        n=m.instructions
                        assert result==expected and m.hits==want,(profiled,count,route,op,take,result,expected,m.hits,want)
                        assert m.original==([] if consumed else [op])
                        expected_profile=[0xffffffff]*36
                        if profiled and op<18:
                            expected_profile[op]=6;expected_profile[18+op]=0
                        assert m.profile_words(PROFILE)==expected_profile,(profiled,op)
                        assert m.timer_reads==(2 if op<18 else 1) if profiled else m.timer_reads==0
                        rows.append(dict(profile=profiled,subscribers=count,route=route,operation=op,
                                         consume=take,instructions=n))
    # A real callback changes the profiler pointer and optionally makes a
    # recursive invalid-op call. The current call must use its entry snapshot.
    m.no_write=False;m.mutator=CALLBACK
    m.u.mem_write(LIST,pack(CALLBACK,0));m.w(m.sym['core:core_native_list'],LIST)
    m.u.mem_write(m.sym['core:core_native_interest'],bytes([128])*24)
    transitions=0
    for initial,next_buffer,nested in ((0,OTHER,False),(PROFILE,0,False),(PROFILE,OTHER,False),(PROFILE,PROFILE,True)):
        code=bytes.fromhex('4feffff8')                 # outgoing op/args
        if nested:
            code+=bytes.fromhex('2ebc')+pack(22)+bytes.fromhex('2f7c')+pack(ARGS)+bytes.fromhex('0004')
            code+=bytes.fromhex('4eb9')+pack(m.sym['core:core_native'])
        code+=bytes.fromhex('2ebc')+pack(next_buffer)
        code+=bytes.fromhex('4eb9')+pack(m.sym['core:core_native_profile'])
        code+=bytes.fromhex('4fef000870004e75')
        assert len(code)<96
        m.u.mem_write(CALLBACK,code);m.u.ctl_remove_cache(CALLBACK,CALLBACK+len(code))
        m.reset_profile(PROFILE);m.reset_profile(OTHER)
        m.invoke('core_native_profile',initial)
        m.operation=17;m.consume=None;m.hits=[];m.original=[];m.timer_reads=0
        assert m.invoke('core_native',17,ARGS)==0x6a000011
        assert m.hits==[CALLBACK] and m.original==([22,17] if nested else [17])
        old=[0xffffffff]*36
        if initial:old[17]=13 if nested else 6;old[35]=0
        assert m.profile_words(PROFILE)==old and m.profile_words(OTHER)==[0xffffffff]*36
        transitions+=1
    return dict(rows=rows,profile_snapshot_transitions=transitions,checks=len(rows),abi=True,
                callback_results_and_original_replay=True,legacy_profile_canaries_and_wrap=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--before',type=Path,required=True);p.add_argument('--after',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    before,after=verify(a.before),verify(a.after)
    rows=[]
    for old,new in zip(before['rows'],after['rows']):
        assert {k:v for k,v in old.items() if k!='instructions'}=={k:v for k,v in new.items() if k!='instructions'}
        assert new['instructions']<=old['instructions'],('native service regression',old,new)
        rows.append({**{k:v for k,v in old.items() if k!='instructions'},'before':old['instructions'],'after':new['instructions']})
    sha=lambda path:hashlib.sha256((path/'main.bin').read_bytes()).hexdigest()
    result=dict(ok=True,checks=after['checks'],profile_snapshot_transitions=after['profile_snapshot_transitions'],
                all_call_counts_nonincreasing=True,rows=rows,bank_independent=True,device_access=False,
                core_sha256=dict(before=sha(a.before),after=sha(a.after)),
                limits=['Real linked wrapper/dispatcher; subscriber and unchanged original bodies are boundaries.',
                        'Deterministic timer oracle; guest instructions are not physical cycles or MAIN CPU points.'])
    a.out.write_text(json.dumps(result,indent=2)+'\n')
    print('PASS:',after['checks'],'service cases; ABI/results/replay/profiler snapshots/canaries/wrap; no count increases')
    for r in rows:
        if r['subscribers']==3 and r['route'] in (0,2,128) and r['operation']==17 and r['consume']=='none':print(r)


if __name__=='__main__':main()
