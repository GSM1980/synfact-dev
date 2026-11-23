"""Execute resident route encoding/dispatch with independent oracles; no device I/O.

Real core mask setters, encoder and dispatcher execute. Only subscriber
bodies are boundary doubles (one nested-update subscriber executes real code).
No EMAC instructions, unaligned accesses or physical cycle claims.
"""
import argparse,hashlib,json,random,struct,sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
from test_st_safe_boot_emu import Core,SP
from unicorn.m68k_const import *

LIST,OUT,ARGS,RESULT,CALLBACK=0x44000000,0x44001004,0x44002000,0x44002100,0x44010000
WORDS=0x44003000     # core 2.6.1: native_routes() output for the independent oracle
ALL=0xffffffff
REGISTERS=[UC_M68K_REG_D2,UC_M68K_REG_D3,UC_M68K_REG_D4,UC_M68K_REG_D5,
           UC_M68K_REG_D6,UC_M68K_REG_D7,UC_M68K_REG_A2,UC_M68K_REG_A3,
           UC_M68K_REG_A4,UC_M68K_REG_A5,UC_M68K_REG_A6]
VALID=tuple(range(18))+(20,21)
pack=lambda *v:struct.pack('>%dI'%len(v),*v)

def expected(entries,registry):
    out=[0]*24;t=pv=any_view=all_entries=0
    for index,fn in enumerate(entries):
        all_entries|=1<<min(index,7)
        mask,view=registry.get(fn,(0xffffffff,0))
        for op in range(24):
            if mask>>op&1:out[op]|=1<<min(index,7)
        if mask&16:
            tracks=mask>>24;t|=tracks if tracks not in (0,255) else 256
        if mask&0x600:
            any_view|=not view or bool(pv and pv!=view);pv=view
    if all_entries:out=[128 if x==all_entries else x for x in out]
    out[18]=t&255;out[19]=t>>8
    return bytes(out),0 if any_view else pv

def expected_routes(entries,registry):
    """Core 2.6.1: one bit per list entry up to 32, all ones past 32, for
    every subscriber, and for the TYPE bytes 18/19."""
    out=[0]*22;every=0
    for index,fn in enumerate(entries):
        mask,_=registry.get(fn,(ALL,0))
        for op in range(22):
            if mask>>op&1:out[op]=out[op]|1<<index if index<32 else ALL
        if index<32:every|=1<<index
    return [ALL if (every and x==every) or op in (18,19) else x for op,x in enumerate(out)]

def merged(flags,words):
    """Core 2.6.1 native_merge: an interested byte from its word."""
    out=bytearray(flags)
    for op in range(22):
        if op in (18,19) or not out[op] or not words[op]:continue
        out[op]=128 if words[op]==ALL else 127 if words[op]>>7 else words[op]
    return bytes(out)

class Routes(Core):
    def __init__(self,folder):
        self.hits=[];self.consume=None;self.operation=None;self.mutator=None;self.instructions=0
        super().__init__(folder)
        masks=self.sym['core:core_native_masks']
        end=self.sym.get('core:core_native_masks_end',masks+4*12)
        assert end>masks and (end-masks)%12==0
        self.native_slots=(end-masks)//12
        self.w(ARGS,0x12345678)
        for i in range(40):self.u.mem_write(CALLBACK+32*i,bytes.fromhex('70004e75'))   # 33+ subscribers
    def code(self,u,pc,size,user):
        self.instructions+=1
        if CALLBACK<=pc<CALLBACK+32*40 and (pc-CALLBACK)%32==0:
            op,args,result=self.args(3)
            assert op==self.operation and args==ARGS and result==RESULT
            self.hits.append(pc)
            if pc==self.mutator:return
            self.w(result,pc^op)
            # Exercise caller-saved clobbers rather than preserving everything
            # by accident in a Python callback boundary.
            for reg in (UC_M68K_REG_D1,UC_M68K_REG_A0,UC_M68K_REG_A1):u.reg_write(reg,0xa55a5aa5)
            self.ret(7 if pc==self.consume else 0);return
        super().code(u,pc,size,user)
    def invoke(self,name,*args):
        saved=[0x12340000+i for i in range(len(REGISTERS))]
        for reg,value in zip(REGISTERS,saved):self.u.reg_write(reg,value)
        result=self.call(name,*args)
        assert [self.u.reg_read(r) for r in REGISTERS]==saved,name
        assert self.u.reg_read(UC_M68K_REG_A7)==SP+4,name
        return result
    def setup(self,entries,registry):
        self.u.mem_write(LIST,pack(*entries,0))
        self.w(self.sym['core:core_native_list'],LIST)
        slots=list(registry.items());assert len(slots)<=self.native_slots
        rows=[(fn,mask,view) for fn,(mask,view) in slots]
        rows += [(0,0,0)]*(self.native_slots-len(rows))
        self.u.mem_write(self.sym['core:core_native_masks'],b''.join(pack(*x) for x in rows))
        self.w(OUT-4,0xa55a1234);self.w(OUT+24,0x1234a55a)
        self.u.mem_write(OUT,bytes([0xa5])*24)
        view=self.invoke('core_native_route_fill',OUT,LIST)
        flags=bytes(self.u.mem_read(OUT,24));want=expected(entries,registry)
        assert (flags,view)==want,(entries,registry,flags.hex(),want)
        assert self.word(OUT-4)==0xa55a1234 and self.word(OUT+24)==0x1234a55a
        # Core 2.6.1: the real word builder and byte rewrite, against the oracle.
        words=expected_routes(entries,registry);final=merged(flags,words)
        self.invoke('native_routes',WORDS,LIST)
        actual=list(struct.unpack('>22I',bytes(self.u.mem_read(WORDS,88))))
        assert actual==words,(entries,registry,[hex(x) for x in actual],[hex(x) for x in words])
        self.invoke('native_merge',OUT,WORDS)
        assert bytes(self.u.mem_read(OUT,24))==final,(bytes(self.u.mem_read(OUT,24)).hex(),final.hex())
        assert self.sym['core:core_native_interest']-self.sym['core:core_native_routes']==88
        self.u.mem_write(self.sym['core:core_native_interest'],final)
        self.u.mem_write(self.sym['core:core_native_routes'],pack(*words))
        self.oracle_words=words
        return final
    def dispatch(self,entries,flags,op,consume,words=None):
        self.operation=op;self.consume=consume;self.hits=[];self.w(RESULT,0xdeadbeef)
        bits=flags[op];want=[]
        # The setup oracle's words; after the public setters, the core's own.
        if words is None:words=list(struct.unpack('>22I',bytes(self.u.mem_read(self.sym['core:core_native_routes'],88))))
        word=words[op]
        for i,fn in enumerate(entries):
            # Core 2.6.1: 0x80 (and ops 18/19) is everyone; another nonzero
            # byte takes the entries from the operation's word.
            if op in (18,19) or bits&128:wanted=True
            else:wanted=bool(bits) and i<32 and word>>i&1
            if wanted:
                want.append(fn)
                if fn==consume:break
        result=self.invoke('core_event_native',op,ARGS,RESULT)
        assert self.hits==want,(op,flags.hex(),self.hits,want)
        assert result==int(bool(want) and want[-1]==consume)
        assert self.word(RESULT)==(want[-1]^op if want else 0xdeadbeef)

def verify(folder,out):
    m=Routes(folder);rng=random.Random(0x54415);checks=0
    functions=[CALLBACK+32*i for i in range(10)]
    # The real catalog registers ARP/POLY/Chorus/Playhead before the deferred
    # LFO3 initializer. Its fifth narrowing must reach both C and assembly.
    assert m.native_slots==32,m.native_slots
    entries=functions[:5];m.setup(entries,{})
    for i in (1,2,3,4,0):m.invoke('core_native_interest_set',entries[i],1<<21 if i else 0)
    flags=bytes(m.u.mem_read(m.sym['core:core_native_interest'],24))
    assert flags[17]==0,flags.hex()
    m.dispatch(entries,flags,17,None)
    m.invoke('core_native_interest_set',entries[0],1<<17)
    flags=bytes(m.u.mem_read(m.sym['core:core_native_interest'],24))
    m.dispatch(entries,flags,17,None);assert m.hits==entries[:1]
    # Fill the entire table through the public API and exercise its last
    # record. A seventeenth callback must retain safe fail-open semantics.
    entries=[CALLBACK+32*i for i in range(m.native_slots+1)]
    m.setup(entries,{})
    for fn in entries[:-1]:m.invoke('core_native_interest_set',fn,0)
    start=m.sym['core:core_native_masks']
    saved=bytes(m.u.mem_read(start,m.native_slots*12))
    m.invoke('core_native_interest_set',entries[-1],0)
    assert bytes(m.u.mem_read(start,m.native_slots*12))==saved
    m.w(m.sym['core:core_native_list'],LIST)
    m.u.mem_write(LIST,pack(*entries[:-1],0))
    m.invoke('core_native_initialize')
    assert bytes(m.u.mem_read(m.sym['core:core_native_interest'],24))==bytes(24)
    m.invoke('core_native_interest_set',entries[-2],1<<9)
    m.invoke('core_native_page_view_set',entries[-2],0x1234)
    assert m.word(m.sym['core:core_native_page_view'])==0x1234
    flags=bytes(m.u.mem_read(m.sym['core:core_native_interest'],24))
    assert flags[9]==127 and flags[17]==0
    # Core 2.6.1: the 32nd subscriber has its own bit; only it runs.
    assert m.word(m.sym['core:core_native_routes']+4*9)==1<<31
    m.dispatch(entries[:-1],flags,9,None);assert m.hits==[entries[-2]]
    m.u.mem_write(LIST,pack(*entries,0));m.invoke('core_native_initialize')
    flags=bytes(m.u.mem_read(m.sym['core:core_native_interest'],24))
    assert flags[17]==128
    # The 33rd (no mask slot: every operation) is past 32: full traversal.
    assert m.word(m.sym['core:core_native_routes']+4*17)==ALL
    m.dispatch(entries,flags,17,None);assert m.hits==entries
    masks=(0,0xffffffff,1<<17,1<<20,1<<4|0x06000000,1<<4|0xff000000,
           1<<9,1<<10,0x600)
    for count in (0,1,2,3,7,8,9,40):
        for case in range(32):
            registry={fn:(rng.choice(masks) if case<16 else rng.getrandbits(32),
                          rng.choice((0,0x1234,0x5678))) for fn in functions[:4]}
            entries=[rng.choice(functions[:4] if case&1 else functions) for _ in range(count)]
            flags=m.setup(entries,registry);words=m.oracle_words
            for op in range(22):
                m.dispatch(entries,flags,op,None,words)
                m.dispatch(entries,flags,op,rng.choice(entries) if entries else None,words)
                checks+=2
    # Exercise the real public setter/refresh, including an unchanged value.
    entries=functions[:3];registry={f:(1<<17,0) for f in entries}
    m.setup(entries,registry)
    for fn in entries:
        m.invoke('core_native_interest_set',fn,1<<20)
        registry[fn]=(1<<20,0)
        want=expected(entries,registry)[0]
        assert bytes(m.u.mem_read(m.sym['core:core_native_interest'],24))==merged(want,expected_routes(entries,registry))
        m.invoke('core_native_interest_set',fn,1<<20)
    # A real subscriber narrows other masks while dispatch is in progress.
    # The current snapshot must still visit them; the next dispatch must not.
    setter=m.sym['core:core_native_interest_set']
    # The mutator occupies three callback slots; put the other subscribers
    # beyond it so the test code cannot overlap their entry points.
    entries=[functions[0],functions[4],functions[5]];m.mutator=entries[0]
    m.setup(entries,{f:(1<<3,0) for f in entries})
    code=bytes.fromhex('4feffff4')
    for fn in entries[1:]:
        code+=bytes.fromhex('203c')+pack(fn)+bytes.fromhex('2e8042af00044eb9')+pack(setter)
    code+=bytes.fromhex('4fef000c70004e75')
    assert len(code)<96
    m.u.mem_write(entries[0],code);m.u.ctl_remove_cache(entries[0],entries[0]+len(code))
    m.operation=3;m.consume=None;m.hits=[]
    assert m.invoke('core_event_native',3,ARGS,RESULT)==0 and m.hits==entries
    m.hits=[];assert m.invoke('core_event_native',3,ARGS,RESULT)==0 and m.hits==entries[:1]
    m.mutator=None
    # A deliberately wrong route must be observable in the callback trace.
    entries=functions[4:6];flags=m.setup(entries,{f:(1<<17,0) for f in entries});words=m.oracle_words
    # Core 2.6.1: a positive byte takes its entries from the word; corrupt both.
    m.u.mem_write(m.sym['core:core_native_interest']+17,b'\x01')
    m.w(m.sym['core:core_native_routes']+4*17,2)
    try:m.dispatch(entries,flags,17,None,words)
    except AssertionError:negative=True
    else:raise AssertionError('corrupt route escaped callback trace oracle')
    result=dict(ok=True,encoder_cases=256,dispatch_cases=checks,abi=True,native_slots=m.native_slots,
                fifth_subscriber=True,last_slot=True,capacity_overflow_fail_open=True,
                nested_snapshot=True,negative_control_detected=negative,device_access=False,
                core_sha256=hashlib.sha256((folder/'main.bin').read_bytes()).hexdigest(),
                limits=['Subscriber bodies are boundaries; nested mask-update body is real target code.',
                        'No full native boot, EMAC use, cache timing or device I/O.'])
    out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();verify(a.core,a.out)
