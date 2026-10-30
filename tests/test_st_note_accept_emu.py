"""Real native acceptance slice vs core 0.12.20, with a dispatcher double.

Executes mute/staleness and Sound resolution, the linked core shim and the
displaced native instructions. Stops at effects entry or native cleanup.
The condition predicate and C dispatcher are explicit boundaries, not synthesis.
No device IO. Host tests separately execute the real native-list dispatcher.
"""
import argparse,itertools,json,struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from elekloader import formats
from unicorn import Uc,UC_ARCH_M68K,UC_MODE_BIG_ENDIAN,UC_HOOK_CODE
from unicorn.m68k_const import *
BASE,ENTRY,EFFECTS,CLEANUP=0x40000400,0x400a4180,0x400a4244,0x400a471e
KIT,EVENT,BUCKET,FRAME,STACK=0x43000000,0x49000000,0x49000100,0x49001000,0x48010000
REGS=[UC_M68K_REG_D0,UC_M68K_REG_D1,UC_M68K_REG_D2,UC_M68K_REG_D3,UC_M68K_REG_D4,
      UC_M68K_REG_D5,UC_M68K_REG_D6,UC_M68K_REG_D7,UC_M68K_REG_A0,UC_M68K_REG_A1,
      UC_M68K_REG_A2,UC_M68K_REG_A3,UC_M68K_REG_A4,UC_M68K_REG_A5,UC_M68K_REG_A6]

class Runtime:
    def __init__(self,image,report=None):
        self.u=u=Uc(UC_ARCH_M68K,UC_MODE_BIG_ENDIAN);u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        for a,n in [(0x40000000,0x06000000),(0x46000000,0x10000),(STACK-0x10000,0x20000),
                    (EVENT,0x10000),(0x80000000,0x10000)]:u.mem_map(a,n)
        u.mem_write(BASE,image);self.symbols={};self.count=0
        if report:
            layout=report['link'];lo,hi=layout['ddr'];at=layout['run_load']-BASE
            u.mem_write(lo,image[at:at+hi-lo]);self.symbols=report['symbols']
            self.dispatch=self.symbols['core:core_native']
            self.interest=self.symbols['core:core_native_interest']
        else:self.dispatch=0;self.interest=0
        u.hook_add(UC_HOOK_CODE,self.code)
    def read(self,a,n):return bytes(self.u.mem_read(a,n))
    def w(self,a,v):self.u.mem_write(a,struct.pack('>I',v&0xffffffff))
    def ret(self,v):
        u=self.u;sp=u.reg_read(UC_M68K_REG_A7);pc=int.from_bytes(self.read(sp,4),'big')
        u.reg_write(UC_M68K_REG_D0,v);u.reg_write(UC_M68K_REG_A7,sp+4);u.reg_write(UC_M68K_REG_PC,pc)
    def code(self,u,pc,n,_):
        if pc==EFFECTS or (pc==CLEANUP and not self.finish_cleanup) or (pc==0x400a3b76 and self.finish_cleanup):
            u.emu_stop();return
        self.count+=1
        if pc==0x4011a4f8:self.frees+=1
        if pc==0x400a1b14:self.predicates+=1;self.ret(self.reject);return
        if pc==self.dispatch:
            sp=u.reg_read(UC_M68K_REG_A7);op,args=struct.unpack('>2I',self.read(sp+4,8))
            assert op==20
            self.notifications.append(struct.unpack('>4I',self.read(args,16)))
            # Deliberate ABI scratch/CCR clobber, so the shim must preserve X.
            for reg in [UC_M68K_REG_D1,UC_M68K_REG_A0,UC_M68K_REG_A1]:u.reg_write(reg,0xa5a5a5a5)
            u.reg_write(UC_M68K_REG_SR,0x251f);self.ret(self.consume)
    def run(self,track,kind,action,sound,immediate,refusal,interest=0,consume=0,finish_cleanup=False):
        u=self.u;self.notifications=[];self.predicates=0;self.count=0;self.consume=consume
        self.finish_cleanup=finish_cleanup;self.frees=0
        self.reject=int(refusal=='condition');self.u.mem_write(EVENT,bytes(0x2000))
        for i,r in enumerate(REGS):u.reg_write(r,0x12340000+i*256)
        u.reg_write(UC_M68K_REG_SR,0x2500);u.reg_write(UC_M68K_REG_A7,STACK)
        for r,v in [(UC_M68K_REG_D0,action),(UC_M68K_REG_D1,kind),(UC_M68K_REG_D2,track),
                    (UC_M68K_REG_A2,EVENT),(UC_M68K_REG_A3,BUCKET),(UC_M68K_REG_A6,FRAME)]:u.reg_write(r,v)
        if self.interest:u.mem_write(self.interest+20,bytes([interest]))
        self.w(0x800030b8,KIT);self.w(0x800046ec,100000);self.w(FRAME-64,128800)
        self.w(FRAME-56,0);self.w(0x43bdc828,0)
        self.u.mem_write(0x43bde43e,struct.pack('>H',(1<<track) if refusal=='mute' else 0))
        self.w(0x45510fc8+4*track,3 if refusal=='stale' else 0)
        self.w(EVENT+4,action);self.w(EVENT+8,track);self.w(EVENT+12,kind);self.w(EVENT+36,0x123400)
        chosen=KIT+0x2a+track*0x123
        if sound in ['borrowed','pooled']:
            chosen=KIT+0x1000;self.w(EVENT+(40 if sound=='borrowed' else 44),chosen)
        self.w(BUCKET,immediate);self.w(BUCKET+4,110000)
        self.w(0x444de440,0)
        self.before=self.read(EVENT,80)
        u.emu_start(ENTRY,0,count=5000)
        pc=u.reg_read(UC_M68K_REG_PC);assert pc in [EFFECTS,CLEANUP,0x400a3b76],hex(pc)
        assert u.reg_read(UC_M68K_REG_A7)==STACK
        assert self.read(EVENT,80)==self.before,'shim changed/freed the borrowed event'
        if finish_cleanup:
            assert self.frees==1 and int.from_bytes(self.read(0x444de440,4),'big')==EVENT
        return dict(pc=pc,regs=[u.reg_read(r) for r in REGS],sr=u.reg_read(UC_M68K_REG_SR),
                    notifications=list(self.notifications),predicates=self.predicates,count=self.count,chosen=chosen)

def verify(stock,core,out):
    f,d,r=formats.load(stock);assert f.sha256=='8e2488f462c4a5656396a895f113bcd415e9900fa8709340dccf45d4cb9ed19e'
    report=json.loads((core.parent/'verification.json').read_text())
    a,b=Runtime(f.section(3)),Runtime(core.read_bytes(),report)
    rows=[];cost=[]
    for args in itertools.product([0,7,8,11],[1,2],[1,2],['kit','borrowed','pooled'],[0,1],['none','mute','stale','condition']):
        native=a.run(*args);off=b.run(*args);observer=b.run(*args,interest=1)
        for actual in [off,observer]:
            assert actual['pc']==native['pc'] and actual['regs']==native['regs'],(args,native,actual)
            assert actual['sr']==native['sr'] and actual['predicates']==native['predicates'],args
        assert not off['notifications'];accepted=native['pc']==EFFECTS
        assert len(observer['notifications'])==int(accepted)
        if accepted:
            assert observer['notifications']==[(EVENT,native['chosen'],100000 if args[4] else 110000,128800)],args
            consumed=b.run(*args,interest=1,consume=1)
            assert consumed['pc']==CLEANUP and len(consumed['notifications'])==1
            assert consumed['regs'][2:]==native['regs'][2:],args
            assert consumed['sr']&0xfff0==native['sr']&0xfff0,args
            cost.append(off['count']-native['count'])
        rows.append(dict(track=args[0]+1,kind=args[1],action=args[2],sound=args[3],immediate=args[4],refusal=args[5],accepted=accepted))
    for track in [0,7,8,11]:
        b.run(track,2,1,'borrowed',1,'none',interest=1,consume=1,finish_cleanup=True)
    result=dict(ok=True,cases=len(rows),accepted=sum(x['accepted'] for x in rows),native_release_cases=4,
                off_extra_instructions=sorted(set(cost)),protected_sections=report['protected_sections'],
                limits=['Native acceptance slice only; condition predicate and dispatcher are doubles.',
                        'Four consumed cases execute native event release once; lock/Sound lifecycle and synthesis remain outside scope.'],device_access=False)
    out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['stock','core','out']:p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();verify(a.stock,a.core,a.out)
