"""Linked ARP module + real core bus, native pools, queue and note effects.

Runs sequence acceptance, template/output retention, queue draining and native
pitch/gate writes. Sound publication, ramps and parameter-lock application are
explicit synthesis boundaries. No device IO or physical-audio assertion.
"""
import argparse,json,struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
from elekloader import formats
from elekloader.sdk import elf
from unicorn import Uc,UC_ARCH_M68K,UC_MODE_BIG_ENDIAN,UC_HOOK_CODE
from unicorn.m68k_const import *
BASE,BANK,SP,RET=0x40000400,0x47000000,0x48010000,0x4f000000
KIT,SEQ,W,FRAME,LOCKPOOL=0x43000000,0x44000000,0x49010000,0x49001000,0x49004000
OWNER=RET+0x100;OWNERMAP=W+0x500;LIST=W+0x600
REGS=[UC_M68K_REG_D2,UC_M68K_REG_D3,UC_M68K_REG_D4,UC_M68K_REG_D5,UC_M68K_REG_D6,UC_M68K_REG_D7,
      UC_M68K_REG_A2,UC_M68K_REG_A3,UC_M68K_REG_A4,UC_M68K_REG_A5,UC_M68K_REG_A6]

class Machine:
    def __init__(self,core,bank):
        self.r=json.loads((core.parent/'verification.json').read_text());self.cs=self.r['symbols']
        u=self.u=Uc(UC_ARCH_M68K,UC_MODE_BIG_ENDIAN);u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        for a,n in [(0x40000000,0x6000000),(0x46000000,0x10000),(BANK,0x200000),
                    (SP-0x10000,0x20000),(0x49000000,0x20000),(RET,0x10000),(0x80000000,0x10000)]:u.mem_map(a,n)
        raw=core.read_bytes();u.mem_write(BASE,raw);layout=self.r['link'];lo,hi=layout['ddr'];at=layout['run_load']-BASE
        u.mem_write(lo,raw[at:at+hi-lo]);e=elf.Elf.load(bank.parent/(bank.stem+'.work')/'main.elf')
        self.sym={s.name:BANK+s.value for s in e.symbols if s.name and s.shndx==e.section('.image').idx}
        self.symbol_sizes={s.name:s.size for s in e.symbols if s.name and s.shndx==e.section('.image').idx}
        self.tracks=self.symbol_sizes['arp_parameters']//24
        assert self.tracks in [8,12]
        data=bank.read_bytes();size,n=struct.unpack_from('>2I',data,52);image=bytearray(data[80+4*n:80+4*n+size])
        for off in struct.unpack_from('>%dI'%n,data,80):self.pack_into(image,off,struct.unpack_from('>I',image,off)[0]+BANK)
        u.mem_write(BANK,bytes(image));u.mem_write(RET,b'\x4e\x71');u.mem_write(OWNER,b'\x4e\x75')
        self.phase='call';self.log=[];self.locks=[];self.count=0
        self.stock_trig_result=None;self.stock_trig_calls=0
        u.hook_add(UC_HOOK_CODE,self.code)
        self.w(0x444db730,LOCKPOOL);self.call(0x4011a3a0,71)
        self.w(0x43bd743c,KIT);self.w(0x800030b8,KIT);self.w(0x43bd7430,SEQ);self.w(0x43bd73bc,1)
        self.w(0x402d7010,14400);self.w(0x800046ec,0);self.u.mem_write(OWNERMAP,bytes(range(12)))
        # Bind the actual core's existing native list to this module and a
        # POLY-owner fixture. Interest publication/dispatch execute real core C.
        self.w(LIST,self.sym['arp_native']);self.w(LIST+4,OWNER);self.w(LIST+8,0)
        self.w(self.cs['core:events']+11*8,LIST)
        self.call(self.cs['core:core_native_interest_set'],OWNER,1<<15)
        self.w(W,3);self.w(W+4,self.cs['core:core_machine_ui_api'])
        assert self.call('arp_initialize',W)==0
        self.call(self.cs['core:core_native_initialize'])
    @staticmethod
    def pack_into(b,a,v):struct.pack_into('>I',b,a,v&0xffffffff)
    def read(self,a,n):return bytes(self.u.mem_read(a,n))
    def word(self,a):return int.from_bytes(self.read(a,4),'big')
    def w(self,a,n):self.u.mem_write(a,struct.pack('>I',n&0xffffffff))
    def track_address(self,name,track=0):
        assert 0<=track<self.tracks
        return self.sym[name]+(self.symbol_sizes[name]//self.tracks)*track
    def ret(self,v=0):
        sp=self.u.reg_read(UC_M68K_REG_A7);pc=self.word(sp)
        self.u.reg_write(UC_M68K_REG_D0,v&0xffffffff);self.u.reg_write(UC_M68K_REG_A7,sp+4);self.u.reg_write(UC_M68K_REG_PC,pc)
    def code(self,u,pc,n,_):
        self.count+=1
        if pc==self.cs['core:core_stock_trig'] and self.stock_trig_result is not None:
            self.stock_trig_calls+=1;self.ret(self.stock_trig_result);return
        if pc in [0x40001980,0x40001aa2]:self.ret();return # task mutex boundary
        if pc==OWNER:
            sp=u.reg_read(UC_M68K_REG_A7);op,args,out=struct.unpack('>3I',self.read(sp+4,12))
            if op==15:self.w(out,OWNERMAP);self.ret(1)
            else:self.ret(0)
            return
        if pc==0x400a4244 and self.phase=='accept':u.emu_stop();return
        if pc==0x400a3b76 and self.phase in ['accept','effects']:u.emu_stop();return
        if pc==0x400a31a2: # publication boundary: preserve exact chosen identity
            sp=u.reg_read(UC_M68K_REG_A7);sound,track=struct.unpack('>2I',self.read(sp+4,8))
            self.w(0x800030c0+track*4,sound);self.ret();return
        if pc in [0x40097874,0x40097760,0x40097732]:self.ret();return # smoothing boundary
        if pc==0x400978b4:
            sp=u.reg_read(UC_M68K_REG_A7);self.locks.append(struct.unpack('>3I',self.read(sp+4,12)));self.ret();return
    def call(self,start,*args):
        u=self.u;self.phase='call'
        for i,r in enumerate(REGS):u.reg_write(r,0x12340000+i*256)
        before=[u.reg_read(r) for r in REGS]
        u.reg_write(UC_M68K_REG_SR,getattr(self,'call_sr',0x2700));u.reg_write(UC_M68K_REG_A7,SP)
        u.mem_write(SP,struct.pack('>%dI'%(1+len(args)),RET,*args))
        u.emu_start(self.sym.get(start,start),RET,count=getattr(self,'call_budget',1000000))
        assert u.reg_read(UC_M68K_REG_PC)==RET,(start,hex(u.reg_read(UC_M68K_REG_PC)))
        assert u.reg_read(UC_M68K_REG_A7)==SP+4 and [u.reg_read(r) for r in REGS]==before,start
        return u.reg_read(UC_M68K_REG_D0)
    def configure(self,mode=2,track=0,mask=0xffff,range_=1,note_length=30):
        self.u.mem_write(W+0x100,struct.pack('>5BxH16b',mode,9,range_,note_length,15,mask,*([0]*16)))
        return self.call('arp_experiment_configure',track,W+0x100)
    def event(self,note=60,length=10800000,when=0,pooled=False,locked=True,flags=0x10601,track=0):
        e=self.call(0x4011a4c2);assert e
        lock=self.call(0x4011a472) if locked else 0
        values=[0]*20;values[1]=1;values[2]=track;values[3]=1;values[6]=note;values[9]=flags
        values[10]=KIT+0x2a+track*0x123;values[11]=KIT+0x150 if pooled else 0;values[12]=length;values[17]=lock
        self.u.mem_write(e,struct.pack('>20I',*values));self.call(0x4011a648,e,when)
        return e,lock
    def drain(self,begin,end):
        u=self.u;rounds=0
        while self.word(0x444de434):
            b=self.word(0x444de434)
            if not self.word(b) and ((self.word(b+4)-end)&0xffffffff)<0x80000000:break
            current=self.word(b+8)
            while current:
                # At most 128 native initial events plus one generated note
                # for each supported track; generated kind 2 cannot recapture.
                rounds+=1;assert rounds<=128+self.tracks,'recursive recapture or unbounded queue'
                action,track,kind=struct.unpack('>3I',self.read(current+4,12))
                for r in REGS:u.reg_write(r,0)
                for r,v in [(UC_M68K_REG_D0,action),(UC_M68K_REG_D1,kind),(UC_M68K_REG_D2,track),
                            (UC_M68K_REG_A2,current),(UC_M68K_REG_A3,b),(UC_M68K_REG_A6,FRAME)]:u.reg_write(r,v)
                self.w(FRAME-64,end);self.w(FRAME-56,0);self.w(FRAME-100,KIT)
                u.reg_write(UC_M68K_REG_A7,SP);u.reg_write(UC_M68K_REG_SR,0x2500)
                self.phase='accept';u.emu_start(0x400a4180,0,count=100000)
                pc=u.reg_read(UC_M68K_REG_PC)
                assert pc in [0x400a4244,0x400a3b76],hex(pc)
                if pc==0x400a4244:
                    pitch=self.word(current+24);gate=self.word(current+48);lock=self.word(current+68);flags=self.word(current+36)
                    sound=u.reg_read(UC_M68K_REG_A4)
                    transpose=int.from_bytes(self.read(sound+0xa8,1),'big',signed=True)
                    self.phase='effects';u.emu_start(pc,0,count=100000)
                    assert u.reg_read(UC_M68K_REG_PC)==0x400a3b76
                    if flags&0x10000:assert self.word(0x800046b8+4*track)==max(0,min(127,pitch+transpose))<<16,'native pitch path diverged'
                    if flags&0x200:assert self.word(0x80003084+4*track)==gate,'native gate path diverged'
                    self.log.append(dict(time=begin if self.word(b) else self.word(b+4),track=track,pitch=pitch,gate=gate,lock=lock,kind=kind))
                assert u.reg_read(UC_M68K_REG_A7)==SP
                current=u.reg_read(UC_M68K_REG_A2)
            nxt=self.word(b+16);self.call(0x4011a394,nxt);self.call(0x4011a570,b)
    def frame(self,begin):
        self.w(0x800046ec,begin);self.call('arp_frame');self.drain(begin,begin+28800)
    def gate_tick(self,tempo=14400):
        # Original countdown and expiry-flag loop, used between generated notes.
        u=self.u;self.phase='call';self.w(FRAME-52,0);self.w(FRAME-80,0)
        for r,v in [(UC_M68K_REG_D0,0),(UC_M68K_REG_D1,tempo),(UC_M68K_REG_D2,1),
                    (UC_M68K_REG_A1,0x80003050),(UC_M68K_REG_A0,0x80003084),(UC_M68K_REG_A6,FRAME)]:u.reg_write(r,v)
        u.reg_write(UC_M68K_REG_SR,0x2500);u.emu_start(0x400a3970,0x400a39a0,count=1000)
        assert u.reg_read(UC_M68K_REG_PC)==0x400a39a0
        return self.word(FRAME-52)
    def pool_counts(self):
        def count(a,off):
            seen=set()
            while a:assert a not in seen;seen.add(a);a=self.word(a+off)
            return len(seen)
        return count(self.word(0x444de440),76),count(self.word(0x444de43c),16),count(self.word(0x444de438),16)

def verify(core,bank,out):
    m=Machine(core,bank);initial=m.pool_counts();assert m.configure()==1
    root,lock=m.event();m.frame(0)
    assert len(m.log)==1 and m.word(lock)==1 # original freed, retained template owns one ref
    for t in range(28800,21628800,28800):m.frame(t)
    assert [x['pitch'] for x in m.log]==[60,72,60,72],m.log
    assert [x['time'] for x in m.log]==[0,5400000,10800000,16200000],m.log
    assert [x['gate'] for x in m.log]==[2784375,2784375,2784375,2700000],m.log
    assert m.pool_counts()==initial,(initial,m.pool_counts())
    # Next root must remain eligible after outputs used native live-note kind 2.
    m.event(note=65,when=21628800);m.frame(21628800)
    assert m.log[-1]['pitch']==65,m.log
    m.w(0x43bd73bc,2);m.frame(21657600);assert m.pool_counts()==initial
    # Refuse a POLY group, MIDI and the FX track before changing configuration.
    m.u.mem_write(OWNERMAP,bytes([0,0,*range(2,12)]));assert m.configure(track=0)==0
    m.u.mem_write(OWNERMAP,bytes(range(12)));m.u.mem_write(KIT+0x1550,b'\x00\x01');assert m.configure()==0
    m.u.mem_write(KIT+0x1550,b'\x00\x00');assert m.configure(track=12)==0
    failures=[]
    # Native event-pool exhaustion at template acquisition falls through to the
    # original note; putting back the deliberately withheld free chain balances.
    q=Machine(core,bank);base=q.pool_counts();q.configure();e,l=q.event()
    held=q.word(0x444de440);q.w(0x444de440,0);q.frame(0)
    assert len(q.log)==1 and q.log[0]['kind']==1
    head=q.word(0x444de440);assert head==e;q.w(head+76,held)
    assert q.pool_counts()==base;failures.append('template-pool-full-native-fallback')
    # Output allocation and bucket exhaustion are bounded and retain only the
    # template reference, then recover at the next scheduled note (no burst).
    for address,name in [(0x444de440,'event'),(0x444de43c,'bucket')]:
        q=Machine(core,bank);base=q.pool_counts();q.configure();_,l=q.event();q.frame(0)
        held=q.word(address);q.w(address,0);q.frame(5400000)
        assert len(q.log)==1 and q.word(l)==1
        q.w(address,held);q.frame(10800000);assert len(q.log)==2 and q.log[-1]['pitch']==60
        q.w(0x43bd73bc,2);q.frame(10828800);assert q.pool_counts()==base
        failures.append(name+'-pool-full-rollback-recovery')
    q=Machine(core,bank);base=q.pool_counts();q.configure(mask=0);q.event();q.frame(0)
    assert not q.log;q.w(0x43bd73bc,2);q.frame(28800);assert q.pool_counts()==base
    failures.append('all-rests-stop-balanced')
    q=Machine(core,bank);base=q.pool_counts();q.configure();q.event(pooled=True);q.frame(0)
    assert len(q.log)==1 and q.log[0]['kind']==1 and q.pool_counts()==base
    failures.append('temporary-Sound-native-fallback')
    q=Machine(core,bank);base=q.pool_counts();q.configure();q.event();q.frame(0)
    assert q.configure(mode=0)==1 and q.pool_counts()==base
    # Native release on disable must not poison following sequence notes.
    q.event(note=67,when=28800);q.frame(28800)
    assert q.log[-1]['pitch']==67 and q.log[-1]['kind']==1 and q.pool_counts()==base
    failures.append('disable-release-next-native-note')
    q=Machine(core,bank);base=q.pool_counts();q.configure();q.event();q.frame(0)
    q.w(0x43bd7430,SEQ+0x22000);q.frame(28800);assert q.pool_counts()==base
    failures.append('pattern-pointer-replacement')
    q=Machine(core,bank);base=q.pool_counts();q.configure();q.event(length=0xffffffff);q.frame(0)
    q.frame(5400000);assert len(q.log)==2 and all(x['gate']==2784375 for x in q.log)
    assert q.configure(mode=0)==1 and q.pool_counts()==base
    assert q.word(0x80003084)==1,'infinite gate was not released on disable'
    failures.append('infinite-note-disable-release')
    q=Machine(core,bank);base=q.pool_counts();q.configure();q.event(flags=0x10600);q.frame(0)
    assert len(q.log)==1 and q.log[0]['kind']==1 and q.pool_counts()==base
    failures.append('special-trigger-flags-native-fallback')
    q=Machine(core,bank);base=q.pool_counts();q.configure();q.event(length=0xffffffff);q.frame(0)
    # A trigless (lock) trig passing a running arp neither stops nor restarts it.
    q.event(flags=0x10600,when=28800);q.frame(28800)
    assert q.word(q.sym['arp_running'])&1 and q.log[-1]['gate']==10800000 # native applies it
    q.frame(5400000);assert q.log[-1]['pitch']==72 and q.log[-1]['time']==5400000
    assert q.configure(mode=0)==1 and q.pool_counts()==base
    failures.append('trigless-trig-keeps-running-arp')
    q=Machine(core,bank);q.configure();q.u.mem_write(W+0x103,b'\xff')
    assert q.call('arp_experiment_configure',0,W+0x100)==0
    failures.append('invalid-note-length-configuration-refused')
    # Native-zero TRIG infinity keeps the template alive; LEN is finite.
    q=Machine(core,bank);base=q.pool_counts();q.configure(note_length=0);q.event(length=0);q.frame(0)
    assert q.log[-1]['gate']==253125
    expiry=[q.gate_tick() for _ in range(19)]
    assert expiry==[0]*17+[1,0],expiry
    # Editing LEN does not drop a held chord or restart its scheduled phase.
    generation=q.word(q.sym['arp_generation']);assert q.configure(note_length=30)==1
    assert q.word(q.sym['arp_generation'])==generation
    q.frame(5400000);assert len(q.log)==2 and q.log[-1]['gate']==2784375
    assert q.configure(mode=0)==1 and q.pool_counts()==base
    assert q.gate_tick()==1 and q.gate_tick()==0
    failures.append('native-zero-infinity-LEN-expiry-edit-disable')
    q=Machine(core,bank);base=q.pool_counts();q.configure(note_length=127);q.event(length=0);q.frame(0)
    assert q.log[-1]['gate']==2136268810
    q.w(0x43bd73bc,2);q.frame(28800);assert q.pool_counts()==base and q.gate_tick()==1
    failures.append('maximum-LEN-STOP-releases-gate')
    q=Machine(core,bank);base=q.pool_counts();q.configure(note_length=126);q.event(length=10000);q.frame(0)
    assert q.log[-1]['gate']==10000 and q.gate_tick()==1
    q.frame(28800);assert q.pool_counts()==base
    failures.append('long-LEN-clipped-to-short-finite-parent')
    q=Machine(core,bank);base=q.pool_counts();q.configure(note_length=0);q.event(length=0);q.frame(0)
    q.w(0x80003084,1);assert q.gate_tick()==1 # old gate expires on a new note's frame
    q.frame(5400000);assert q.log[-1]['gate']==253125 and q.log[-1]['pitch']==72
    assert q.configure(mode=0)==1 and q.pool_counts()==base
    failures.append('expiry-and-next-generated-note-same-frame')
    q=Machine(core,bank);base=q.pool_counts();q.configure();q.w(0x43bd73bc,2);q.event();q.frame(0)
    assert len(q.log)==1 and q.log[-1]['kind']==1 and q.pool_counts()==base
    failures.append('stopped-sequence-preview-native-passthrough')
    for address,replacement,name in [(0x43bd7430,SEQ+0x22000,'pattern'),(0x43bd743c,KIT+0x30000,'kit')]:
        q=Machine(core,bank);base=q.pool_counts();q.configure();q.event(length=0);q.frame(0)
        q.w(address,replacement);assert q.configure(note_length=31)==1
        assert q.pool_counts()==base and q.word(0x80003084)==1
        q.frame(5400000);assert len(q.log)==1
        failures.append(name+'-replacement-before-LEN-edit-stops-source')
    q=Machine(core,bank);q.count=0;q.call('arp_frame');off=q.count
    q.configure();q.event();q.frame(0);q.w(0x800046ec,28800)
    q.count=0;q.call('arp_frame');idle=q.count
    q.w(0x800046ec,5400000);q.count=0;q.call('arp_frame');due=q.count
    # Rate and direction changes keep the currently sounding root and use
    # the new period without waiting for another sequencer trig.
    q=Machine(core,bank);base=q.pool_counts();q.configure();q.event(length=0);q.frame(0)
    generation=q.word(q.sym['arp_generation']);q.w(0x800046ec,2000000)
    q.u.mem_write(W+0x100,struct.pack('>5BxH16b',3,10,1,30,15,0xffff,*([0]*16)))
    assert q.call('arp_experiment_configure',0,W+0x100)==1
    assert q.word(q.sym['arp_generation'])==generation and q.pool_counts()!=base
    q.frame(4320000);assert len(q.log)==2 and q.log[-1]['time']==4320000
    q.configure(mode=0);assert q.pool_counts()==base
    # Stock TRIG may return a different event from the one supplied. Exercise
    # the core dispatcher and its original replay, then accept the result.
    def trig(q,step,event,track=0,source=None):
        q.stock_trig_result=event
        q.u.mem_write(W+0x200,struct.pack('>6I',track,SEQ,KIT,step,event if source is None else source,0))
        before=q.stock_trig_calls
        assert q.call(q.cs['core:core_native'],0,W+0x200)==event
        assert q.stock_trig_calls==before+1
        q.stock_trig_result=None
    lane=lambda track,n,step:SEQ+track*0x499+0x2c0+n*0x40+step
    for case,setup,expected in [
            ('step lanes by event',lambda q,e:(q.u.mem_write(lane(0,1,3),b'\x44'),q.u.mem_write(lane(0,2,3),b'\x47'),trig(q,3,e)),[60,64,67,60]),
            ('track defaults without a record',lambda q,e:q.u.mem_write(SEQ+0x3c6,b'\x4c'),[60,72,60,72]),
            ('first-step static',lambda q,e:(q.u.mem_write(lane(0,3,0),b'\x3b'),trig(q,0,0x444e2438)),[55,60,55,60]),
            ('zeroed lanes add nothing',lambda q,e:trig(q,5,e),[60,60,60,60])]:
        q=Machine(core,bank);base=q.pool_counts();q.configure(range_=0)
        q.u.mem_write(SEQ,bytes(0x499));q.u.mem_write(SEQ+0x2c0,b'\xff'*256)
        e,_=q.event(length=0);setup(q,e)
        for t in (0,5400000,10800000,16200000):q.frame(t)
        assert [x['pitch'] for x in q.log]==expected,(case,q.log)
        assert q.configure(mode=0)==1 and q.pool_counts()==base
        failures.append('chord: '+case)
    # Two successive trigs must take their own F/G/H lanes. The event pool may
    # reuse an address, so neither the previous chord nor a track default may
    # leak into the next root.
    q=Machine(core,bank);base=q.pool_counts();q.configure(mode=1,range_=0)
    q.u.mem_write(SEQ,bytes(0x499));q.u.mem_write(SEQ+0x2c0,b'\xff'*256)
    q.u.mem_write(SEQ+0x3c6,b'\x40\x40\x40')
    for step,values in ((0,(68,71,64)),(1,(52,69,64))):
        for ln,value in enumerate(values,1):q.u.mem_write(lane(0,ln,step),bytes([value]))
    e,_=q.event(note=60,length=0);trig(q,0,e)
    for t in (0,5400000,10800000):q.frame(t)
    e,_=q.event(note=65,length=0,when=16200000);trig(q,1,e)
    for t in (16200000,21600000,27000000):q.frame(t)
    assert [x['pitch'] for x in q.log]==[60,64,67,65,53,70],q.log
    assert q.configure(mode=0)==1 and q.pool_counts()==base
    failures.append('chord: successive steps retain independent NOT2..NOT4')
    # Step 1 inherits C4/+3/+5. Step 17 locks only NOT3 to +6. Stock returns
    # the queued root rather than the scratch event passed into TRIG.
    q=Machine(core,bank);base=q.pool_counts();q.configure(mode=1,range_=0)
    q.u.mem_write(SEQ,bytes(0x499));q.u.mem_write(SEQ+0x2c0,b'\xff'*256)
    q.u.mem_write(SEQ+0x3c5,bytes([60,67,69,64]))
    q.u.mem_write(lane(0,2,16),b'\x46')
    e,_=q.event(note=60,length=0);trig(q,0,e,source=W+0x700)
    for t in (0,5400000,10800000):q.frame(t)
    e,_=q.event(note=60,length=0,when=16200000);trig(q,16,e,source=W+0x700)
    for t in (16200000,21600000,27000000):q.frame(t)
    assert [x['pitch'] for x in q.log]==[60,63,65,60,63,66],q.log
    assert q.configure(mode=0)==1 and q.pool_counts()==base
    failures.append('chord: step 17 NOT3 lock survives stock event replacement')
    # The prepared static source can also return a pooled event. Its backup
    # record must be spent when that event plays, before any unrelated root.
    q=Machine(core,bank);base=q.pool_counts();q.configure(mode=1,range_=0)
    q.u.mem_write(SEQ,bytes(0x499));q.u.mem_write(SEQ+0x2c0,b'\xff'*256)
    q.u.mem_write(lane(0,3,0),b'\x3b')
    e,_=q.event(note=60,length=0);trig(q,0,e,source=0x444e2438)
    q.frame(0);q.frame(5400000)
    q.event(note=60,length=0,when=10800000)
    q.frame(10800000);q.frame(16200000)
    assert [x['pitch'] for x in q.log]==[60,55,60,60],q.log
    assert q.configure(mode=0)==1 and q.pool_counts()==base
    failures.append('chord: prepared fallback spent after pooled first root')
    result=dict(ok=True,native_pool_counts=initial,notes=m.log,limits=[
        'Native core dispatch, allocation/copy/free, lock refs, queue, pitch/gate run as target code.',
        'Sound publication, smoothing and parameter-lock application are explicit boundaries; no audio synthesis or device test.',
        'This fixture covers sequence roots; standalone held keyboard chords are checked separately. Native chord lanes are read from a sequence fixture; stock TRIG replay is stubbed with the returned event while core dispatch executes. Temporary pooled Sounds remain outside this fixture; persistence has a separate fixture.'],
        failure_cases=failures,frame_callback_instructions=dict(off=off,active_not_due=idle,due_with_native_pool_copy_queue=due),device_access=False)
    out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['core','bank','out']:p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();verify(a.core,a.bank,a.out)
