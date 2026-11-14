"""Real core routing and LFO3 output publication, with explicit DSP boundaries.

Executes actual LFO3/ARP/POLY/MSKIP initialization, public Sound publication,
shared lock storage/apply/restore, IRQ masking and core route construction.
The original LFO kernel/renderer and metadata/allocator/mutex are boundaries;
separate analog-emu tests check the unchanged real EMAC renderer.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
from test_st_patchers_emu import Patchers
from test_st_safe_boot_emu import SP,RET
from unicorn import UC_HOOK_MEM_WRITE
from unicorn.m68k_const import *

POOL=0x44000000
PATTERN=0x40a38b34
KIT=0x41ad5134
WORK=0x800021b0
PROXY,GETTER=0x45000000,0x45010000
SAVED=(UC_M68K_REG_D2,UC_M68K_REG_D3,UC_M68K_REG_D4,UC_M68K_REG_D5,
       UC_M68K_REG_D6,UC_M68K_REG_D7,UC_M68K_REG_A2,UC_M68K_REG_A3,
       UC_M68K_REG_A4,UC_M68K_REG_A5,UC_M68K_REG_A6)


class Activity(Patchers):
    def __init__(self,core,bank,break_updates=False):
        self.total=0;self.hits=Counter();self.sr=0x2700;self.break_updates=break_updates
        super().__init__(core,bank)
        for name,init,_ in self.modules:
            if name in ('ARP EXPERIMENT','POLY'):self.real[init]=name
        self.dynamic='lfo3_output_rows' in self.bs
        self.tracked={self.sym['core:core_native']:'dispatch',
                      self.sym['core:core_native_interest_set']:'interest_sets',
                      self.bs['lfo3_events_bank']:'overlay',
                      self.bs.get('lfo3_output_track'):'output_updates'}
        self.event_boundaries={self.bs['lfo3_events_native_'+name] for name in ('apply','clear','restore','fx_restore')}
        self.u.hook_add(UC_HOOK_MEM_WRITE,self.write_hook)
        self.run('full')
        assert self.word(self.lfo['hooks_installed'])==1 and self.word(self.lfo['in_sram'])==1
        self.sr=0x2000
        self.w(0x444db730,POOL)
        self.context=self.word(self.bs['lfo3_events_context'])
        self.call('lfo3_events_init',self.context,71)
        self.store=self.call('lfo3_owned_lookup',PATTERN+0x3bc5)
        assert self.store

    def write_hook(self,u,access,at,n,value,user):
        if self.dynamic and at==self.bs['lfo3_output_rows']:
            assert u.reg_read(UC_M68K_REG_SR)&0x700==0x700,'output bitmap written outside IRQ exclusion'

    def code(self,u,pc,n,user):
        self.total+=1
        if pc in self.tracked:
            self.hits[self.tracked[pc]]+=1
            if self.tracked[pc]=='interest_sets':
                assert u.reg_read(UC_M68K_REG_SR)&0x700==0x700,'route setter outside IRQ exclusion'
            if self.break_updates and self.tracked[pc]=='output_updates':
                self.ret(0);return
        if pc==0x4011926e:
            # Original LFO body boundary, after the adapter's exact 64-byte
            # frame. Restore its real MOVEM saves and return to the caller.
            self.hits['original_lfo']+=1
            sp=u.reg_read(UC_M68K_REG_A7)
            for reg,value in zip(SAVED,struct.unpack('>11I',bytes(u.mem_read(sp,44)))):
                u.reg_write(reg,value)
            u.reg_write(UC_M68K_REG_A7,sp+64);self.ret(0);return
        render=self.word(self.lfo['render']) if self.lfo else 0
        if render and pc==render:
            self.hits['render']+=1;self.ret(0);return
        if pc in self.event_boundaries:self.ret(0);return
        if pc in (0x400a63ec,0x400a6300):
            slot=self.args(1)[0];self.ret(0x100+slot if slot else 0);return
        if pc==0x400a666a:self.ret(0x200);return
        if pc==GETTER:self.ret(self.getter_source);return
        super().code(u,pc,n,user)

    def call(self,name,*args):
        u=self.u;u.reg_write(UC_M68K_REG_SR,self.sr);u.reg_write(UC_M68K_REG_A7,SP)
        u.mem_write(SP,struct.pack('>'+str(1+len(args))+'I',RET,*[v&0xffffffff for v in args]))
        saved=[u.reg_read(r) for r in SAVED]
        before=self.total;self.hits=Counter()
        at=self.bs[name] if name in self.bs else self.sym['core:'+name]
        u.emu_start(at,RET,count=3000000)
        assert u.reg_read(UC_M68K_REG_PC)==RET,(name,hex(u.reg_read(UC_M68K_REG_PC)))
        assert u.reg_read(UC_M68K_REG_A7)==SP+4,name
        assert [u.reg_read(r) for r in SAVED]==saved,name
        assert u.reg_read(UC_M68K_REG_SR)&0x2700==self.sr&0x2700,name
        self.last=dict(instructions=self.total-before,**self.hits)
        return u.reg_read(UC_M68K_REG_D0)

    def expected_output(self):
        bank=self.word(self.bs['lfo3_bank']);bits=0
        for t in range(13):
            base=struct.unpack('>8H',bytes(self.u.mem_read(bank+528+16*t,16)))
            active=self.context+4+64*36+t*36
            mask=self.word(active)
            value=struct.unpack('>16H',bytes(self.u.mem_read(active+4,32)))
            dep=value[7] if mask&0x80 else base[7]
            dest=value[3] if mask&8 else base[3]
            if dep!=0x4000 and 1<=dest>>8<=70:bits|=1<<t
        return bits

    def check(self):
        expected=self.expected_output()
        if self.dynamic:
            assert self.word(self.bs['lfo3_output_rows'])==expected
            route=bytes(self.u.mem_read(self.sym['core:core_native_interest']+17,1))[0]
            assert bool(route&0x81)==bool(expected),'LFO3 route differs from output rows'
        assert self.word(self.bs['modulation_skip_mode'])==2
        for op in (9,10,11):
            assert bytes(self.u.mem_read(self.sym['core:core_native_interest']+op,1))!=b'\0','page route lost'

    def publish(self,t,depth=0x4000,dest=0):
        record=self.bs['kit_shadow']+16*t
        values=[0x7000,0x300,0x4000,dest,0,0,0,depth]
        self.u.mem_write(record,struct.pack('>8H',*values))
        source=KIT+(0xdce if t==12 else 0x2a+t*0x123)
        if t<12:self.u.mem_write(source+0xa2,b'\x04')
        self.call('lfo3_publication_begin')
        self.call('lfo3_publication_end',source,t,int(t==12),1)
        cost=dict(self.last);self.check();return cost

    def lock(self,t,depth=None,dest=None,arp=False,static=False):
        self.call('lfo3_owned_clear',self.store)
        for parameter,value in ((7,depth),(3,dest),(8,2 if arp else None)):
            if value is not None:assert self.call('lfo3_owned_set',self.store,t,3,parameter,value,0)==1
        lock=POOL
        if static:
            lock=POOL+0x10000+4*t;self.w(0x444e3884+4*t,lock)
        self.call('lfo3_events_capture',lock,PATTERN,t,3)
        before=self.expected_output()
        self.call('lfo3_events_apply',t,lock,WORK)
        cost=dict(self.last);self.check();return before,cost

    def frame(self):
        self.sr=0x2500
        self.call('core_native_lfo',WORK,14400,0x1fff)
        self.sr=0x2000
        self.check();return dict(self.last)


def verify(core,before,after):
    old,new=[Activity(core,bank) for bank in (before,after)]
    assert new.dynamic
    measurements=[];publications=[]
    for label,depth,dest in (('inactive',0x4000,0),('active',0x7000,0x2f00),
                              ('fine_depth',0x4001,0x2f00),('neutral',0x4000,0x2f00)):
        pub=[m.publish(0,depth,dest) for m in (old,new)]
        publications.append(dict(case=label,before=pub[0],after=pub[1]))
        costs=[m.frame() for m in (old,new)]
        assert costs[1]['instructions']<=costs[0]['instructions'],(label,costs)
        if label in ('inactive','neutral'):
            assert costs[1].get('dispatch',0)==costs[1].get('overlay',0)==costs[1].get('render',0)==0
        else:assert costs[1]['render']==1
        measurements.append(dict(case=label,before=costs[0],after=costs[1]))
    tests=0
    # Every track including FX, fine depth, invalid/fine/NONE destination.
    for t in range(13):
        for dest in (0,0x100,0x2f00,0x4600,0x4700,0x7f00,0x2f01):
            for depth in (0,0x3fff,0x4000,0x4001,0x7fff,0x8000,0xffff):
                new.publish(t,depth,dest);new.frame();tests+=1
        new.publish(t)
    # Multi-track first/last transitions; no setter for ordinary value updates.
    new.publish(0,0x7000,0x2f00)
    assert new.publish(12,0x7000,0x2f00).get('interest_sets',0)==0
    assert new.publish(0).get('interest_sets',0)==0
    assert new.publish(12).get('interest_sets',0)==1
    # Queued and static first-step locks must wake the same next frame.
    for static in (False,True):
        for t in range(13):
            new.publish(t,0x4000,0x2f00)
            _,cost=new.lock(t,depth=0x7000,static=static)
            assert new.frame()['render']==1
            new.call('lfo3_events_restore',t,WORK,0);new.check()
            assert new.frame().get('render',0)==0
            new.publish(t,0x7000,0x2f00)
            new.lock(t,depth=0x4000,static=static)
            assert new.frame().get('render',0)==0
            new.call('lfo3_events_clear',t);new.check()
            assert new.frame()['render']==1
            new.publish(t)
            new.lock(t,depth=0x7000,dest=0x2f00,static=static)
            assert new.frame()['render']==1
            new.call('lfo3_events_fx_restore' if t==12 else 'lfo3_events_restore',
                     *([WORK,0] if t==12 else [t,WORK,0]));new.check()
            new.lock(t,arp=True,static=static)
            assert new.frame().get('render',0)==0
            new.call('lfo3_events_clear',t);new.check();tests+=8
    # Pool reset clears locked eligibility and recomputes the same base rows.
    new.publish(4,0x4000,0x2f00);new.lock(4,depth=0x7000)
    new.call('lfo3_events_init',new.context,71);new.check()
    assert new.frame().get('render',0)==0
    # Real editor/action refresh, distinct from Sound publication. Only the
    # native proxy's model getter is doubled; record lookup/refresh are real.
    new.publish(0,0x4000,0x2f00)
    new.getter_source=KIT+0x2a
    for at,value in ((PROXY,PROXY+0x100),(PROXY+0x100+0x28,0x40012986),
                     (PROXY+0x10,PROXY+0x200),(PROXY+0x200,PROXY+0x300),
                     (PROXY+0x300+0x28,GETTER)):
        new.w(at,value)
    for depth in (0x4001,0x7000,0x4000):
        assert new.call('lfo3_actions_store',PROXY,0x3f9+8,depth,0,0,0)==1
        new.check();assert bool(new.frame().get('render',0))==(depth!=0x4000);tests+=1
    # Task republish after codec/copy updates must also wake the route.
    new.u.mem_write(new.bs['kit_shadow']+14,b'\x70\x00')
    new.call('lfo3_republish');new.check();assert new.frame()['render']==1;tests+=1
    new.publish(0)
    # Nested publication keeps the original busy guard until the outer end.
    new.call('lfo3_publication_begin');new.publish(0,0x7000,0x2f00)
    assert new.frame().get('render',0)==0
    new.call('lfo3_publication_end',0,0,0,0)
    assert new.frame()['render']==1;tests+=1
    new.publish(0)
    # CRC fallback resets the prepared bank and rebuilds eligibility. Test
    # with and without locks that independently supply DEP and DEST.
    fallback=[]
    for locked in (False,True):
        f=Activity(core,after);f.publish(0,0x7000,0x2f00)
        if locked:f.lock(0,depth=0x7000,dest=0x2f00)
        f.w(f.lfo['ticks'],59)
        f.u.mem_write(0x80007340,bytes([bytes(f.u.mem_read(0x80007340,1))[0]^1]))
        f.call('lfo3_tick',PROXY);f.check()
        assert f.word(f.lfo['in_sram'])==0
        assert f.word(f.lfo['render'])==f.bs['lfo3_render']
        assert bool(f.frame().get('render',0))==locked
        fallback.append(dict(locked=locked,route_enabled=locked));tests+=1
    # Charge both route transitions to the event side, then require a net
    # saving after sixteen idle blocks even with the renderer's work replaced
    # by a constant boundary (a conservative instruction-only comparison).
    totals=[]
    for m in (old,new):
        m.publish(0)
        total=m.publish(0,0x7000,0x2f00)['instructions']
        total+=m.publish(0)['instructions']
        for _ in range(16):total+=m.frame()['instructions']
        totals.append(total)
    assert totals[1]<=totals[0],totals
    if not old.dynamic:assert totals[1]<totals[0],totals
    # A second subscriber may still need LFO while LFO3 is suspended. Use the
    # actual host API/route builder, preserving POLY's other operation bits;
    # its real inactive handler declines here (no fabricated analog state).
    masks=new.sym['core:core_native_masks'];poly=new.bs['poly_native']
    slot=next(masks+12*i for i in range(4) if new.word(masks+12*i)==poly)
    previous=new.word(slot+4)
    new.sr=0x2700;new.call('core_native_interest_set',poly,previous|(1<<17));new.sr=0x2000
    assert new.frame().get('render',0)==0
    assert bytes(new.u.mem_read(new.sym['core:core_native_interest']+17,1))==b'\x04'
    new.publish(0,0x7000,0x2f00);assert new.frame()['render']==1
    new.publish(0)
    assert bytes(new.u.mem_read(new.sym['core:core_native_interest']+17,1))==b'\x04'
    new.sr=0x2700;new.call('core_native_interest_set',poly,previous);new.sr=0x2000
    assert new.frame().get('dispatch',0)==0;tests+=3
    # Required negative control: dropping a publication update must be caught.
    broken=Activity(core,after,break_updates=True)
    try:broken.publish(0,0x7000,0x2f00)
    except AssertionError:negative=True
    else:raise AssertionError('missed activation negative control was not detected')
    return dict(ok=True,publication_and_lock_checks=tests,measurements=measurements,
                publication_costs=publications,CRC_fallback=fallback,
                POLY_interest_preserved=True,
                two_transitions_plus_16_idle_blocks=dict(before=totals[0],after=totals[1]),
                negative_control=negative,device_access=False,
                limits=['Real initialization, public publication/locks, IRQ masking and resident route builder.',
                        'Native metadata/event bodies/heap/mutex and DSP renderer/original LFO body are explicit boundaries.',
                        'Separate analog-emu runs the unchanged real renderer/EMAC.',
                        'Instructions are not cycles or hardware CPU points.'])


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('core','before','after','out'):p.add_argument('--'+key,type=Path,required=True)
    a=p.parse_args();result=verify(a.core,a.before,a.after)
    result['bank_sha256']={k:hashlib.sha256(p.read_bytes()).hexdigest() for k,p in [('before',a.before),('after',a.after)]}
    a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
