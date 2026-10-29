"""Count real linked POLY instructions in ColdFire Unicorn; no device IO.

Native synthesis/queue calls are explicit boundaries. Guards run to their stock
continuation/skip label (native bodies are separately audited byte-for-byte).
Publication is executed completely and checked against an independent field map.
Counts are instructions, not cycles, IRQ scheduling or physical CPU load.
"""
import argparse, hashlib, json, struct, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from elekloader.sdk import elf
from unicorn import Uc,UC_ARCH_M68K,UC_MODE_BIG_ENDIAN,UC_HOOK_CODE
from unicorn.m68k_const import *
BANK,STACK,RETURN,STUB=0x47000000,0x48010000,0x4f000000,0x4f010000
SEQ,KIT,WORK,HOST,ARGS,RESULT,EVENT=0x44000000,0x43000000,0x49000000,0x49010000,0x49010100,0x49010200,0x49010300
SAVED=[UC_M68K_REG_D2,UC_M68K_REG_D3,UC_M68K_REG_D4,UC_M68K_REG_D5,UC_M68K_REG_D6,UC_M68K_REG_D7,
       UC_M68K_REG_A2,UC_M68K_REG_A3,UC_M68K_REG_A4,UC_M68K_REG_A5,UC_M68K_REG_A6]

class Runtime:
    def __init__(self,path,native=None):
        self.path=path;raw=path.read_bytes()
        e=elf.Elf.load(path.parent/(path.stem+'.work')/'main.elf')
        self.sym={s.name:BANK+s.value for s in e.symbols if s.name and s.shndx==e.section('.image').idx}
        self.size={s.name:s.size for s in e.symbols if s.name}
        n,nrel=struct.unpack_from('>2I',raw,52);data=bytearray(raw[80+4*nrel:80+4*nrel+n])
        for off in struct.unpack_from('>%dI'%nrel,raw,80):
            struct.pack_into('>I',data,off,struct.unpack_from('>I',data,off)[0]+BANK)
        self.u=u=Uc(UC_ARCH_M68K,UC_MODE_BIG_ENDIAN);u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        for at,length in [(0x40000000,0x06000000),(BANK,(n+65535)&~65535),(STACK-65536,0x20000),
                          (WORK,0x20000),(RETURN,0x20000),(0x80000000,0x10000)]:u.mem_map(at,length)
        if native:u.mem_write(0x40000400,native)
        self.sym['stock_mod']=0x40097afc;self.sym['stock_lfo']=0x40119266
        u.mem_write(BANK,bytes(data));u.mem_write(STUB,b'\x4e\x75');u.mem_write(RETURN,b'\x4e\x71')
        self.stops=set();u.hook_add(UC_HOOK_CODE,self.code)
        self.cfg_states=self.size['poly_seq_config']-8-12-13*16
        assert self.cfg_states in (8,12)
    def w(self,at,value):self.u.mem_write(at,struct.pack('>I',value))
    def read(self,at,n):return bytes(self.u.mem_read(at,n))
    def code(self,u,at,n,user):
        if at in self.stops:u.emu_stop();return
        self.count+=1
        if at==STUB:
            # native_original(TRIG,args) returns supplied event, without native work.
            sp=u.reg_read(UC_M68K_REG_A7);args=struct.unpack('>I',self.read(sp+8,4))[0]
            self.w(RESULT+4,1)
            u.reg_write(UC_M68K_REG_D0,struct.unpack('>I',self.read(args+16,4))[0])
    def topology(self,groups):
        self.groups=groups;states=[0]*12;owners=list(range(12))
        for t,v in groups:
            states[t]=v+1
            for k in range(1,v):owners[t+k]=t
        cfg=self.sym['poly_seq_config'];self.w(cfg,1);self.w(cfg+4,SEQ)
        self.u.mem_write(cfg+8,bytes(states[:self.cfg_states]));self.u.mem_write(self.sym['poly_owner_map'],bytes(owners))
        for name,val in [('poly_has_groups',int(bool(groups))),('poly_analog_active',int(any(t>=8 for t,v in groups)))]:
            if name in self.sym:self.u.mem_write(self.sym[name],bytes([val]))
        if 'poly_publish_end' in self.sym:self.w(self.sym['poly_publish_end'],max([t+v for t,v in groups] or [1]))
        if 'poly_publish_first' in self.sym:self.w(self.sym['poly_publish_first'],min([i for i in range(12) if owners[i]<i] or [1]))
        if 'poly_slave_mask' in self.sym:self.w(self.sym['poly_slave_mask'],sum(1<<t for t in range(12) if owners[t]<t and ('poly_copy_pending' not in self.sym or t>=8)))
        # Copy-on-change banks decline MOD/LFO ownership and publish only the
        # effective shared map. Fixtures model configuration publication too.
        if 'poly_shared_mask' in self.sym:
            for name in ('poly_lfo_map','poly_post_map'):
                self.u.mem_write(self.sym[name],bytes(owners))
            shared=[t for t in range(1,12) if owners[t]<t]
            self.w(self.sym['poly_shared_mask'],sum(1<<t for t in shared))
            self.w(self.sym['poly_publish_span'],(shared[0]<<8)|(shared[-1]+1) if shared else 0x0c0c)
            if 'poly_publish_entry' in self.sym:self.w(self.sym['poly_publish_entry'],self.sym['poly_fast_publish_one' if len(shared)==1 else 'poly_fast_publish_span'])
        self.w(self.sym['cache_sequence'],SEQ);self.w(self.sym['cache_midi_mask'],0)
        self.u.mem_write(self.sym['record_cache'],bytes(7));self.u.mem_write(SEQ+0x21381,bytes(7))
        self.w(0x43bd7430,SEQ);self.w(0x43bd743c,KIT);self.u.mem_write(KIT+0x1550,bytes(2))
        self.w(self.sym['poly_host'],HOST);self.w(HOST,7);self.w(HOST+60,STUB)
        return owners
    def execute(self,start,args=(),stops=(),regs=None,preserve=True):
        u=self.u
        for i,r in enumerate(SAVED):u.reg_write(r,0x12340000+i*0x100)
        u.reg_write(UC_M68K_REG_SR,0x2000);u.reg_write(UC_M68K_REG_A7,STACK)
        u.mem_write(STACK,struct.pack('>%dI'%(len(args)+1),RETURN,*args))
        if regs:
            for r,v in regs.items():u.reg_write(r,v)
        before=[u.reg_read(r) for r in SAVED]
        # Native modulation/LFO precondition: fractional, signed EMAC mode.
        self.count=0;self.stops=set()
        u.mem_write(RETURN+16,bytes.fromhex('a93c00000020'))
        u.emu_start(RETURN+16,RETURN+22,count=1)
        self.count=0;self.stops={self.sym[n] for n in stops}
        u.ctl_remove_cache(BANK,BANK+0x100000)
        u.emu_start(self.sym[start],RETURN,count=200000)
        pc=u.reg_read(UC_M68K_REG_PC)
        assert pc==RETURN or pc in self.stops,(start,hex(pc))
        if preserve:
            assert u.reg_read(UC_M68K_REG_A7)==STACK+4
            assert before==[u.reg_read(r) for r in SAVED],start
        return self.count
    def kernels(self,groups):
        self.topology(groups)
        # Explicit zero-depth native parameter fixture; real native bodies/helpers.
        self.u.mem_write(WORK,bytes(0x1800))
        self.u.mem_write(0x80004670,bytes(0x100))
        self.u.mem_write(0x444da700,bytes(0x800))
        self.u.mem_write(0x80002f50,bytes([4]*12))
        guarded=bool(groups) and ('poly_copy_pending' not in self.sym or any(t>=8 for t,v in groups))
        return [self.execute('poly_mod_shared' if guarded else 'stock_mod',[WORK]),
                self.execute('poly_native_shared' if guarded else 'stock_lfo',[WORK,14400,0])]

    def publication(self,groups):
        owners=self.topology(groups)
        source=bytes((i*37+i//17)%256 for i in range(0x754+13*0x7c));expected=bytearray(source)
        for t,o in enumerate(owners):
            if o>=t:continue
            for offset,size,stride in [(0,2,2),(0x1c,142,142),(0x754,100,124),
                                       (0x754+100,1,124),(0x754+104,19,124)]:
                expected[offset+t*stride:offset+t*stride+size]=source[offset+o*stride:offset+o*stride+size]
        self.u.mem_write(WORK,source)
        # OFF uses subscription gating; the kernel's domain is active groups.
        if not groups:return 0
        count=self.execute('poly_fast_publish_one' if 'poly_fast_publish_one' in self.sym and (self.read_span()&255)==(self.read_span()>>8)+1 else 'poly_fast_publish_span',[WORK,self.sym['poly_owner_map'],self.read_span()]) if 'poly_publish_span' in self.sym else self.execute('poly_fast_publish',[WORK,self.sym['poly_owner_map']])
        assert self.read(WORK,len(source))==expected,('projection',groups)
        return count
    def read_span(self):return struct.unpack('>I',self.read(self.sym['poly_publish_span'],4))[0]
    def trig(self,groups,track):
        self.topology(groups);self.u.mem_write(EVENT,bytes(80));self.w(EVENT,1)
        self.u.mem_write(ARGS,struct.pack('>6I',track,SEQ,KIT,0,EVENT,RESULT+8))
        return self.execute('poly_native',[0,ARGS,RESULT])
    def publish_dispatch(self,groups):
        self.topology(groups);self.w(ARGS,WORK)
        return self.execute('poly_native',[3,ARGS,RESULT]) # ST_NATIVE_PUBLISH
    def block(self,groups,kernels):
        owners=self.topology(groups)
        result={'sync':self.execute('poly_frame'),'publish':0,'routing':0,'type':0}
        if groups:
            self.w(ARGS,WORK)
            result['publish']=self.execute('poly_native',[3,ARGS,RESULT])
            result['routing']=sum(self.execute('poly_native',[op,ARGS,RESULT]) for op in ((15,) if 'poly_copy_pending' in self.sym and not any(t>=8 for t,v in groups) else (15,16,17)))
            for t in range(8):
                if owners[t]<t and 'poly_copy_pending' not in self.sym:
                    self.w(ARGS,0x80002f50+t)
                    result['type']+=2*self.execute('poly_native',[4,ARGS,RESULT])
        result['native_kernels']=sum(kernels)
        result['total']=sum(result.values())
        return result

    def guard(self,name,track):
        if name=='poly_mod_first_guard':
            regs={UC_M68K_REG_A4:0x80004670+2*track,UC_M68K_REG_D2:WORK+0x754+124*track,
                  UC_M68K_REG_D4:WORK+0x1c+142*track}
            stops=['poly_mod_first_continue','poly_mod_first','poly_mod_after_first']
        elif name=='poly_mod_second_guard':
            regs={UC_M68K_REG_D2:2*track,UC_M68K_REG_D3:WORK+0x1c+142*track,
                  UC_M68K_REG_A2:WORK+124*track,UC_M68K_REG_A6:0x80004746}
            stops=['poly_mod_second_continue','poly_mod_second','poly_mod_return']
        else:
            regs={UC_M68K_REG_A0:WORK};stops=['poly_native_continue','poly_native_next']
            self.w(STACK+56,track);self.w(STACK+76,0)
        count=self.execute(name,stops=stops,regs=regs,preserve=False)
        pc=self.u.reg_read(UC_M68K_REG_PC)
        skip=pc!=self.sym[stops[0]]
        if name=='poly_native_lfo_guard':assert self.u.reg_read(UC_M68K_REG_A0)==WORK
        assert skip==any(t<track<t+v and ('poly_copy_pending' not in self.sym or t>=8) for t,v in self.groups) if track<self.cfg_states else not skip
        return count

def verify(before,after,out,stock):
    from elekloader import formats
    image,dev,rel=formats.load(stock)
    assert image.sha256=='8e2488f462c4a5656396a895f113bcd415e9900fa8709340dccf45d4cb9ed19e'
    native=image.section(3)
    a,b=Runtime(before,native),Runtime(after,native);rows=[]
    for label,groups in [('off',[]),('digital-low',[(0,2)]),('digital-high',[(6,2)]),
                         ('digital-two',[(0,4),(4,4)]),('analog-9x2',[(8,2)]),
                         ('analog-9x3',[(8,3)]),('analog-10x2',[(9,2)]),('mixed',[(0,4),(8,3)])]:
        # Main cannot express analog groups: identical digital subset is the baseline.
        old=[(t,v) for t,v in groups if t<8]
        row={'scenario':label,'groups':groups,'baseline_groups':old,'trig':{},'guards':{}}
        for track in [0,1,6,7,8,9,10,11,12]:
            x,y=a.trig(old,track),b.trig(groups,track);row['trig'][str(track)]=[x,y]
            if not any(t>=8 for t,v in groups):assert y<=x,('TRIG regression',label,track,x,y)
        row['publication']=[a.publication(old),b.publication(groups)]
        row['native_kernels']=[a.kernels(old),b.kernels(groups)]
        row['block']=[a.block(old,row['native_kernels'][0]),b.block(groups,row['native_kernels'][1])]
        assert row['block'][1]['total']<=row['block'][0]['total'],('block regression',row)
        assert row['block'][1]['sync']==row['block'][0]['sync'],('sync regression',row)
        for name,n in [('poly_mod_first_guard',12),('poly_mod_second_guard',13),('poly_native_lfo_guard',13)]:
            counts=[]
            for rt,gs in [(a,old),(b,groups)]:
                rt.topology(gs);counts.append(sum(rt.guard(name,t) for t in range(n)))
            row['guards'][name]=counts
            assert counts[1]<=counts[0],('guard overhead regression',label,name,counts)
        if old==groups:assert row['publication'][1]<=row['publication'][0],row
        rows.append(row)
    # All legal digital layouts + analog configurations exercise actual word copies,
    # including opposite parity and preserving the unused auxiliary byte 101..103.
    import itertools
    cases=0
    for code in itertools.product(range(4),repeat=8):
        groups=[];t=0;ok=True
        while t<8:
            v=code[t]+1
            if v>1:
                if t+v>8 or any(code[j] for j in range(t+1,t+v)):ok=False;break
                groups.append((t,v));t+=v
            else:t+=1
        if not ok:continue
        for analog in [[],[(8,2)],[(8,3)],[(9,2)]]:
            count=b.publication(groups+analog)
            if not analog:assert count<=a.publication(groups),('digital publication regression',groups)
            cases+=1
    result={'before_sha256':hashlib.sha256(before.read_bytes()).hexdigest(),
            'after_sha256':hashlib.sha256(after.read_bytes()).hexdigest(),'publication_topologies':cases,
            'counts':rows,'units':'linked instructions; boundary doubles; not device cycles'}
    out.write_text(json.dumps(result,indent=2)+'\n')
    for r in rows:print(r['scenario'],r['publication'],r['native_kernels'],r['guards'],r['trig']['0'],[x['total'] for x in r['block']])
    print('PASS: linked instruction budgets, guard bounds (FX 12), TRIG, exact field projection;',cases,'topologies')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--before',type=Path,required=True)
    p.add_argument('--after',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--stock',type=Path,required=True)
    a=p.parse_args();verify(a.before,a.after,a.out,a.stock)
