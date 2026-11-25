"""Comp-st on the linked OS 1.42 core: routing, math, controls and Kit codec.

The stock UI service boundaries are test_st_chorus_ui_emu's recorded stubs;
the actual core entries, compact assembly, global provider and native Kit
codec execute. No device access, no full startup or physical timing model.
"""
import argparse
import importlib.util
import json
import math
from pathlib import Path
import random
import struct
import sys
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from test_st_chorus_ui_emu import Machine,W,SP,RET,BANK
from unicorn import UC_HOOK_CODE
from unicorn.m68k_const import *

ID,UI,KIT,CV=0x40a,0x4f1,0x41ad6134,W+0x1000
DEFAULT=[0x7f00,0x1800,0x2000,0x4000,0x300,0,0]
NAMES=['THR','ATK','REL','MUP','RAT','MIX','SCF']
SCF=6
HOLD,LP=48,60   # comp_runtime offsets (comp.h)


class NativeStrings(Machine):
    def code(self,u,pc,n,a):
        if pc==0x401bf31e:return  # execute the actual QString constructor
        super().code(u,pc,n,a)


class NativeDraw(NativeStrings):
    def __init__(self,*args):
        self.dials=[];self.invalidations=[];self.texts=[];self.page_draws=0;super().__init__(*args)
        self.u.mem_map(0x4a000000,0x200000);self.heap=0x4a000000

    def code(self,u,pc,n,a):
        if pc==self.sym.get('comp_query') and self.word(u.reg_read(UC_M68K_REG_A7)+4)==30:
            arg=self.word(u.reg_read(UC_M68K_REG_A7)+8)
            self.dials.append(struct.unpack('>8I',self.read(arg,32)))
        if pc==0x400ffe22:self.invalidations.append(self.word(u.reg_read(UC_M68K_REG_A7)+4));self.ret(0);return
        # 1.1.0's GR readout (F, x 59) uses the native printf-style text with the
        # page's label font, whose object the boot constructors fill: a boundary.
        # Its calls are recorded as (y, align, text) with the format checked.
        if pc==0x400f8bd0:
            a=struct.unpack('>8I',self.read(u.reg_read(UC_M68K_REG_A7),32))
            if a[2]==0x443db230 and a[3]==59:
                assert self.read(a[6],3)==b'%s'+bytes(1)
                self.texts.append((a[4],a[5],self.read(a[7],16).split(bytes(1))[0].decode()));self.ret(0);return
        if pc==0x4003a244:self.page_draws+=1
        if pc in (0x400f72be,0x400f8f70,0x4003a244):return
        # Native application/model selection and held-key/sequencer state
        # are boundaries. The complete page, labels, dials and bitmap run.
        if pc in (0x40019092,0x4001c422):self.ret(W+0xa000);return
        if pc in (0x400c3340,0x400c2dd6,0x40102f40,0x4001ff74,0x400249ce):self.ret(0);return
        super().code(u,pc,n,a)


def draw_frame(m,view,bitmap):
    # The core DRAW event follows the real view virtual draw. Execute that
    # native SourcePageView method, then COMP's overlay, exactly once each.
    before=m.page_draws
    m.call(0x40030b84,view,bitmap)
    assert m.page_draws==before+1,'native FX view must paint its page once'
    m.call('comp_draw',view,bitmap,0)
    assert m.page_draws==before+1,'COMP must not paint the native page again'


def initialize(m):
    m.w(0x800030b8,KIT)
    m.w(W,3);m.w(W+4,m.cs['core:core_machine_ui_api'])
    m.w(W+0x1e00,m.sym['comp_native']);m.w(W+0x1e04,0)
    m.w(m.cs['core:events']+11*8,W+0x1e00)
    m.call('comp_params_init');assert m.call('comp_ui_init',W)==0
    m.call(m.cs['core:core_native_initialize'])


def set_values(m,values):
    for i,value in enumerate(values):
        assert m.call(0x4000da32,W,ID+i,value,0,0,1)==0
        assert m.call(0x4000d94a,W,ID+i)==value


def block(m,left,right,cv=(40000,50000),through_core=False):
    m.u.mem_write(0x80003c50,struct.pack('>32i',*left))
    m.u.mem_write(0x80003cd0,struct.pack('>32i',*right))
    if not through_core:
        m.w(CV,0);m.w(CV+4,cv[0]);m.w(CV+8,cv[1]);m.call('comp_audio',CV)
        return m.word(CV+4),m.word(CV+8)
    u=m.u
    original=bytes([0x59])*128;u.mem_write(CV,original)
    u.reg_write(UC_M68K_REG_SR,0x2700);u.reg_write(UC_M68K_REG_A7,SP)
    u.reg_write(UC_M68K_REG_A0,0x12345678);u.reg_write(UC_M68K_REG_A3,CV)
    u.reg_write(UC_M68K_REG_D1,cv[0]);u.reg_write(UC_M68K_REG_D3,cv[1])
    u.emu_start(0x400905b6,0x400905da,count=100000)
    assert u.reg_read(UC_M68K_REG_PC)==0x400905da
    assert u.reg_read(UC_M68K_REG_A7)==SP
    assert u.reg_read(UC_M68K_REG_A0)==0x12345678
    assert m.read(CV,70)==original[:70] and m.read(CV+74,54)==original[74:]
    return m.half(CV+70),m.half(CV+72)


def fixture(core,bank):
    m=Machine(core,bank);initialize(m);return m


def rat_limit_frames():
    """1.1.0's RAT dials for 50.00, 100.0 and INF, made here as ui.c's rat_limit_make
    makes them, from the imported 20.00 frame (never stored)."""
    import re
    t=(ROOT/'mods/comp-st/imported/dials.h').read_text()
    nums=[int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)',re.search(r'comp_rat_bits\[8\]\[38\]=\{(.*?)\};',t,re.S).group(1))]
    base=nums[7*38:8*38]
    def glyph(f,x,rows):
        for r,row in enumerate(rows):
            for c,ch in enumerate(row):
                if ch=='#':f[x+c]|=1<<(27+r)
    five,zero,one=('###','#..','###','..#','###'),('###','#.#','#.#','#.#','###'),('#',)*5
    out=[]
    for edge in (9,7,9):
        f=list(base)
        for c in range(2,18):f[c]&=~(1<<31)
        for c in range(9,18):f[c]&=~(0xf<<27)
        for c in range(2,edge+1):f[c]|=1<<31
        out.append(f)
    glyph(out[0],11,five);glyph(out[0],15,zero)
    glyph(out[1],9,one);glyph(out[1],11,zero);glyph(out[1],15,zero)
    f=out[2]
    for r,row in enumerate(('.##.##.','#..#..#','.##.##.')):
        for c,ch in enumerate(row):
            if ch=='#':f[11+c]|=1<<(28+r)
    for c in range(6,17):f[c]=(f[c]|1<<23)&~(1<<22)
    assert all(f[19:]==base[19:] for f in out),'the mask is 20.00s'
    return out


def test_controls(m):
    for i,name in enumerate(NAMES):
        assert m.call(0x4000d94a,W,ID+i)==DEFAULT[i]
        m.w(W+0x200,0);m.w(W+0x204,ID+i)
        assert m.call('comp_query',18,W+0x200,0,W+0x240)==1
        assert m.string(m.word(W+0x240))==name
        m.w(W+0x200,UI+i)
        assert m.call('comp_query',6,W+0x200,0,W+0x240)==1 and m.word(W+0x240)==ID+i
    m.w(W+0x200,39)
    assert m.call('comp_native',11,W+0x200,W+0x240)==1
    layout=m.word(W+0x240)
    assert struct.unpack('>8I',m.read(layout+8,32))==(ID,ID+1,ID+2,ID+3,ID+4,0,ID+SCF,ID+5)
    set_values(m,[0,0x7f00,0,0,0x700,0x7f00,0x7f00])
    # 1.1.0: RAT's eight Digitakt positions, then 50.00, 100.0 and INF (0xa00)
    for i,top in ((4,0xa00),(SCF,0x7f00)):
        assert m.call(0x4000da32,W,ID+i,0xffff,0,0,1)==0
        assert m.call(0x4000d94a,W,ID+i)==top
        assert m.call(0x4000da32,W,ID+i,0x3ff,0,0,1)==0
        assert m.call(0x4000d94a,W,ID+i)==0x300
    # 1.1.0: RAT's 50.00, 100.0 and INF dials, made at init from 20.00's
    made=struct.unpack('>114I',m.read(m.sym['rat_limit_bits'],3*38*4))
    assert [list(made[k*38:(k+1)*38]) for k in range(3)]==rat_limit_frames(),'RAT 50/100/INF dials'
    print('PASS controls: original seven names, ranges (RAT to INF, its 50/100/INF dials), F empty, G SCF (128 positions), global model IO')


def test_math(core,bank):
    spec=importlib.util.spec_from_file_location('upstream_comp_math',ROOT/'mods/comp-st/upstream/dsp.py')
    dsp=importlib.util.module_from_spec(spec);spec.loader.exec_module(dsp)
    checks=0
    # 1.1.0's 50.00, 100.0 and INF: the same curve, slope 1/R in Q31 (INF: 0)
    limit={8:0x028f5c29,9:0x0147ae14,10:0}
    assert limit[8]==round(2**31/50) and limit[9]==round(2**31/100)
    for ratio in [0,3,7,8,9,10]:
        for mix in [0,0x1234,0x4000,0x7f00]:
            m=fixture(core,bank)
            values=[0x4000,0x1800,0x2000,0x2000,ratio<<8,mix]
            preset=dsp.Preset(*values[:4],min(ratio,7))
            if ratio in limit:preset.slope=limit[ratio]
            set_values(m,values);oracle=dsp.Compact(preset)
            for n in range(32):
                rng=random.Random(100+n)
                left=[int(2**29*math.sin((n*32+i)*.31)) for i in range(32)]
                right=[rng.randrange(-2**29,2**29) for _ in range(32)]
                if n==0:left=right=[0]*32
                if n==1:left=[-(2**31)]*32;right=[2**31-1]*32
                g=oracle.block(left,right)
                wet=(mix<<15)//0x7f00
                gain=((g*wet)>>15)+4096-(wet>>3) if mix else 4096
                expected=tuple(min(65535,(cv*gain)>>12) for cv in [40000,50000])
                actual=block(m,left,right)
                assert actual==expected,(ratio,mix,n,actual,expected,gain)
                checks+=1
    print('PASS DSP:',checks,'blocks vs upstream fixed-point model and independent parallel-gain formula')


def test_fx_site(core,bank):
    m=fixture(core,bank)
    # Publish the same event operand that module binding publishes, using a
    # fixture-owned callback list. The original VCA stores execute afterward.
    m.w(W+0x1d00,m.sym['comp_audio']);m.w(W+0x1d04,0)
    m.w(m.cs['core:core_fx_cv_table']+2,W+0x1d00)
    sentinel=bytes(range(256))*10
    m.u.mem_write(0x80003d50,sentinel)
    set_values(m,[0x2000,0,0x2000,0,0x700,0x7f00])
    for _ in range(8):got=block(m,[2**29]*32,[2**28]*32,through_core=True)
    assert got[0]<40000 and got[1]<50000,got
    assert m.read(0x80003d50,len(sentinel))==sentinel,'track/direct audio modified'
    set_values(m,[0x2000,0,0x2000,0,0x700,0])
    assert block(m,[2**29]*32,[2**28]*32,through_core=True)==(40000,50000)
    m.w(W+0x1d00,0)
    assert block(m,[2**29]*32,[2**28]*32,through_core=True)==(40000,50000)
    print('PASS FX site: only two FX CV words changed; MIX 0 and no subscriber replay stock')


def test_scf(core,bank):
    # Wiring only: the kernel's SCF setup, its state and the task reset.
    # The loop's EMAC arithmetic is checked in analog-emu
    # (test_st_comp_scf_emu.py); Unicorn's EMAC is not trusted for it.
    def run(scf,hz):
        m=fixture(core,bank);set_values(m,[0,0,0x2000,0,0x700,0x7f00,scf])
        hot=m.word(m.sym['comp_hot'])
        for b in range(60):
            left=[int(2**30*math.sin(2*math.pi*hz*(b*32+i)/48000)) for i in range(32)]
            got=block(m,left,left)
        return m,hot,got
    low_off=run(0,50)[2];m,hot,low_on=run(0x4100,50);high_off=run(0,3000)[2];high_on=run(0x4100,3000)[2]   # 250 Hz
    assert low_on[0]>low_off[0]*1.5,('250 Hz SCF must ignore most of a 50 Hz sine',low_off,low_on)
    assert abs(high_on[0]-high_off[0])<=high_off[0]//8+2,('3 kHz passes',high_off,high_on)
    lp=struct.unpack('>2i',m.read(hot+LP,8));assert lp!=(0,0),'SCF state not kept'
    assert m.call(0x4000da32,W,ID+SCF,0x4000,0,0,1)==0
    assert struct.unpack('>2i',m.read(hot+LP,8))==lp,'moving between cutoffs keeps the state'
    assert m.call(0x4000da32,W,ID+SCF,0,0,0,1)==0 and m.call(0x4000da32,W,ID+SCF,0x100,0,0,1)==0
    assert struct.unpack('>2i',m.read(hot+LP,8))==(0,0),'turning SCF on starts from rest'
    off=run(0,50)[0];assert struct.unpack('>2i',off.read(off.word(off.sym['comp_hot'])+LP,8))==(0,0)
    print('PASS SCF wiring: 50 Hz CV %d -> %d with 250 Hz, 3 kHz %d -> %d; state kept across cutoffs, reset when turned on, untouched when OFF'
          %(low_off[0],low_on[0],high_off[0],high_on[0]))


def test_persistence(m):
    slot=0x43c0dfc0+0x2e0200
    marker=bytes([0xa7])*0x1600;m.u.mem_write(slot,marker)
    values=[0x1234,0x2345,0x3456,0x4567,0x500,0x5678,0x300];set_values(m,values)
    # Real module notification, packed full-Q8 values; native record geometry
    # and the adjacent LFO3/Playhead bytes must be untouched by its codec.
    m.u.mem_write(W+0x300,struct.pack('>5I',5,slot,KIT,1,1))
    m.call('comp_meta',W+0x300)
    assert m.read(slot,0x15d0)==marker[:0x15d0]
    assert m.read(slot+0x15e0,32)==marker[0x15e0:]
    saved=m.read(slot+0x15d0,16)
    assert struct.unpack('>8H',saved)[5]==0x0503,'RAT high byte, SCF low byte'
    set_values(m,DEFAULT)
    m.u.mem_write(W+0x300,struct.pack('>5I',6,KIT,slot,1,1));m.call('comp_meta',W+0x300)
    assert [m.call('comp_param_get',ID+i) for i in range(7)]==values
    for j in range(16):
        bad=bytearray(saved);bad[j]^=1;m.u.mem_write(slot+0x15d0,bytes(bad))
        m.call('comp_meta',W+0x300)
        assert [m.call('comp_param_get',ID+i) for i in range(7)]==DEFAULT
    # A 0.2.0 record (RAT's low byte zero, its own check) reads as SCF OFF;
    # an SCF index past 7 is refused like any out-of-range value.
    for low,expected in ((0,values[:6]+[0]),(127,values[:6]+[0x7f00]),(128,DEFAULT)):
        old=bytearray(saved);old[11]=low
        words=list(struct.unpack('>8H',bytes(old)))
        h=0x4350
        for byte in bytes(old[:14]):h=((h*33)&0xffffffff)^byte
        words[7]=(h^(h>>16))&0xffff;m.u.mem_write(slot+0x15d0,struct.pack('>8H',*words))
        m.call('comp_meta',W+0x300)
        assert [m.call('comp_param_get',ID+i) for i in range(7)]==expected,(low,expected)
    m.u.mem_write(slot+0x15d0,saved)
    # After native Kit construction, the core's NEW notification reaches the
    # provider and resets the live row. No LFO3 dependency.
    set_values(m,values)
    m.call(0x400b3384,KIT,sr=0x2000)
    assert [m.call('comp_param_get',ID+i) for i in range(7)]==DEFAULT
    # Older-core fallback: LFO3 names a temporary full project container in
    # its Sound metadata, which the old five-field core notification cannot
    # recognize. API 19 uses query 34 exclusively; the real KitActive mirror
    # descriptors/jobs are exercised separately below.
    m.w(m.sym['comp_core_slots'],0)
    container=0x4c000000;mirror=container+0x2e0200+3*0x1600
    m.u.mem_map(0x4c2e0000,0x10000);m.u.mem_write(mirror,bytes(0x1600))
    def core_meta(kind,a,b):m.u.mem_write(W+0x300,struct.pack('>5I',kind,a,b,1,1));m.call('comp_meta',W+0x300)
    def lfo3_meta(kind,a,b,c):m.u.mem_write(W+0x340,struct.pack('>4I',kind,a,b,c));assert m.call('comp_native',21,W+0x340,W+0x360)==0
    set_values(m,values)
    core_meta(5,mirror,KIT);assert m.read(mirror+0x15d0,16)==bytes(16),'the core alone cannot place the mirror'
    lfo3_meta(5,mirror,KIT,container);assert m.read(mirror+0x15d0,16)==saved,'the mirror gets the record'
    # Power-up: either notification may come first for the same decode.
    set_values(m,DEFAULT);core_meta(6,KIT,mirror)
    assert [m.call('comp_param_get',ID+i) for i in range(7)]==DEFAULT
    lfo3_meta(6,KIT,mirror,container)
    assert [m.call('comp_param_get',ID+i) for i in range(7)]==values,'restored from the mirror'
    set_values(m,DEFAULT);lfo3_meta(6,KIT,mirror,container);core_meta(6,KIT,mirror)
    assert [m.call('comp_param_get',ID+i) for i in range(7)]==values,'the core does not undo it'
    # Without the container the slot is unknown: the initial values, as before.
    lfo3_meta(6,KIT,mirror,0)
    assert [m.call('comp_param_get',ID+i) for i in range(7)]==DEFAULT
    # A known slot is the same through either notification.
    set_values(m,values);lfo3_meta(5,slot,KIT,0);assert m.read(slot+0x15d0,16)==saved
    print('PASS persistence: full Q8, SCF in RAT low byte, 0.2.0 record, 16 corrupt-record cases, neighboring mods, native Kit constructor, legacy LFO3 container fallback')


class MirrorMachine(NativeStrings):
    """Native KitActive/codec; task queue and observer fanout are boundaries."""
    def __init__(self,*args):
        self.jobs=[]
        super().__init__(*args)

    def code(self,u,pc,n,a):
        if pc==0x4016ec18:self.ret(W+0x18000);return   # native task queue singleton
        if pc==0x400bdc48:
            closure=self.word(u.reg_read(UC_M68K_REG_A7)+12)
            self.jobs.append((self.word(closure+12),self.read(self.word(closure),12)))
            self.ret(0);return
        if pc in (0x4016dc10,0x40171eda):self.ret(0);return  # queue owns its copy; observers
        super().code(u,pc,n,a)

    def drain(self):
        jobs,self.jobs=self.jobs,[]
        for callback,job in jobs:
            self.u.mem_write(W+0x18100,job);self.w(W+0x18120,W+0x18100)
            assert callback in (0x4000ea98,0x4000eac8)
            assert self.call(callback,W+0x18120,sr=0x2000)==1
        return len(jobs)


def mirror_active(m):
    app,vectors=0x45500000,0x45000000
    active=app+0x130
    m.w(0x444e23f4,app);m.w(app,0x401c5708)
    m.w(active,0x401c3694);m.w(active+0x10,KIT)
    m.w(active+0x44,(m.word(0x800030b8)-KIT)//0x1618)
    # The first list holds arrays of 128 Kits; the second individual slots
    # of the selected Kit. Neither holds full project containers. Include
    # an empty entry; both native KitActive mirror lists are exercised.
    for offset,entry,base in ((0x20,vectors,0x45001000),(0x2c,vectors+8,0x45100000)):
        m.w(active+offset,entry);m.w(active+offset+4,entry+8)
        m.w(entry,0);m.w(entry+4,base+(0 if offset==0x20 else m.word(active+0x44)*0x1600))


def test_mirror_power_cycle(core,bank):
    values=[0x1234,0x2345,0x3456,0x4567,0xa00,0x5678,0x7f00]
    for k in (0,127):
        m=MirrorMachine(core,bank);initialize(m)
        kit=KIT+k*0x1618;m.call(0x400b3384,kit,sr=0x2000);m.w(0x800030b8,kit)
        slots=[base+k*0x1600 for base in (0x45001000,0x45100000)]
        for slot in slots:m.u.mem_write(slot,b'\xa7'*0x1600)
        mirror_active(m)
        # Production setter -> real KitActive::kitChanged -> native jobs ->
        # real Kit encoder -> core notification. No LFO3 initialized/provider.
        set_values(m,values)
        assert m.drain()==2*len(values)
        for slot in slots:
            assert m.half(slot+0x15d0)==0x4351,'COMP must reach both power-cycle mirrors without LFO3'
            assert m.read(slot+0x14e0,0xf0)==b'\xa7'*0xf0,'LFO3 neighbors'
            assert m.read(slot+0x15e0,0x20)==b'\xa7'*0x20,'Playhead neighbors'
            # A fresh bank instance has no retained COMP RAM. Restore only
            # the encoded native mirror, as at boot, then run the real decoder.
            boot=MirrorMachine(core,bank);initialize(boot);boot.w(0x800030b8,kit);mirror_active(boot)
            boot.u.mem_write(slot,m.read(slot,0x1600))
            assert boot.call(0x400a78a0,kit,slot,sr=0x2000)&255
            assert [boot.call('comp_param_get',ID+i) for i in range(7)]==values
        # Negative control: old COMP's five-field notifications cannot place
        # either mirror. Keep native jobs/codecs identical and remove only
        # the new capacity input; the record must then remain absent.
        m.w(m.sym['comp_core_slots'],0)
        for slot in slots:m.u.mem_write(slot+0x15d0,bytes(16))
        assert m.call('comp_param_set',ID,0x1200)==0
        assert m.drain()==2
        assert all(m.read(slot+0x15d0,16)==bytes(16) for slot in slots)
    print('PASS power-cycle mirrors without LFO3: native setter/KitActive/jobs/codecs, both mirror kinds, Kits 0/127, seven controls, fresh RAM, neighbors, old-path negative control')


def test_native_codec(core,bank):
    m=NativeStrings(core,bank);initialize(m)
    slot=0x43c0dfc0+0x2e0200
    values=[0x1234,0x2345,0x3456,0x4567,0x500,0x5678,0x700]
    m.call(0x400b3384,KIT,sr=0x2000);set_values(m,values)
    assert m.call(0x400a78f4,slot,KIT,0,sr=0x2000)&255
    m.call(0x400b3384,KIT,sr=0x2000)
    assert m.call(0x400a78a0,KIT,slot,sr=0x2000)&255
    assert [m.call('comp_param_get',ID+i) for i in range(7)]==values
    dst=KIT+0x1618
    assert m.call(0x4011613c,dst,KIT,0x1618,sr=0x2000)==dst
    m.w(0x800030b8,dst)
    assert [m.call('comp_param_get',ID+i) for i in range(7)]==values
    # Native SysEx buffers retain their shorter geometry; no trailer write.
    small=W+0xc000;m.u.mem_write(small,b'\xa7'*0x1600)
    assert m.call(0x400a78f4,small,KIT,0,sr=0x2000)&255
    assert m.read(small+0x14d2,0x1600-0x14d2)==b'\xa7'*(0x1600-0x14d2)
    # The boot/project image uses a third native container, in addition to
    # the two working caches. Test its final slot, not only the active cache.
    m.w(0x800030b8,KIT);set_values(m,values)
    image_slot=0x40653514+0x2e0200+127*0x1600
    assert m.call(0x400a78f4,image_slot,KIT,0,sr=0x2000)&255
    m.call(0x400b3384,KIT,sr=0x2000)
    assert m.call(0x400a78a0,KIT,image_slot,sr=0x2000)&255
    assert [m.call('comp_param_get',ID+i) for i in range(7)]==values
    temp=W+0xc000
    m.call(0x4011613c,temp,KIT,0x1618,sr=0x2000)
    m.call(0x4011613c,dst,temp,0x1618,sr=0x2000)
    m.w(0x800030b8,dst)
    assert [m.call('comp_param_get',ID+i) for i in range(7)]==values
    # Reuse of an untracked buffer cannot resurrect its previous COMP data.
    m.u.mem_write(temp,b'\xa5')
    m.call(0x4011613c,dst,temp,0x1618,sr=0x2000)
    assert [m.call('comp_param_get',ID+i) for i in range(7)]==DEFAULT
    print('PASS native Kit encode/decode/copy, project image, stale buffer and short SysEx boundary')


def test_page(core,bank,out):
    m=NativeDraw(core,bank);initialize(m)
    view,bitmap,pixels,pages=W+0x6000,W+0x7000,W+0x9000,W+0x5000
    m.w(view,0x401ca37c);m.w(view+0x88,0x14)
    m.w(view+0x7c,pages);m.w(view+0x80,pages+4);m.w(view+0x84,pages+8);m.w(pages,32)
    # The first view phase extends only this native FX DRIVE vector.
    m.call('comp_draw',view,bitmap,0)
    assert (m.word(pages),m.word(pages+4),m.word(view+0x80))==(32,39,pages+8)
    m.w(view+0x90,1)
    for knob,expected in enumerate([ID,ID+1,ID+2,ID+3,ID+4,0,ID+SCF,ID+5]):
        assert m.call(0x40044c54,view,knob)==expected  # real SourcePageView route
        assert m.call(0x40037d9e,view,knob)==expected  # base navigation route
    # Model selection is explicit; real core global get supplies raw values.
    m.u.mem_write(W+0xa100,m.read(0x401c3dd0,128))
    m.w(W+0xa000,W+0xa100);m.w(W+0xa120,0x4000d94a);m.w(W+0xa124,0x4000da32)
    m.w(0x443db21c,0x402aa114);m.w(0x443db230,0x402aa114)
    m.call(0x400f72be,bitmap,128,64,pixels,0)
    draw_frame(m,view,bitmap)
    assert sorted((a[1],a[2]) for a in m.dials)==list(zip(range(ID,ID+7),DEFAULT))
    assert m.texts[-2:]==[(13,2,'0.0'),(0,2,'GR')],'1.1.0: F reads GR 0.0 with no reduction'
    # Original format strings, including decimal MUP and the discrete RAT;
    # ATK and REL as their time constants in ms; SCF's eight positions.
    for pid,raw,expected in ((ID,0x7f00,'127'),(ID+1,0,'0.10'),(ID+1,0x1800,'3.67'),(ID+1,0x7f00,'100'),
                             (ID+2,0,'10.0'),(ID+2,0x2000,'73.4'),(ID+2,0x7f00,'2000'),(ID+3,0,'0.0'),
                             (ID+3,0x7f00,'24.0'),(ID+4,0x700,'20.00'),(ID+4,0x800,'50.00'),(ID+4,0x900,'100.0'),(ID+4,0xa00,'INF'),(ID+5,0x4000,'64'),
                             (ID+SCF,0,'OFF'),(ID+SCF,0x100,'60Hz'),(ID+SCF,0x1300,'90Hz'),(ID+SCF,0x2a00,'150Hz'),
                             (ID+SCF,0x7e00,'978Hz'),(ID+SCF,0x7f00,'1kHz')):
        assert m.string(m.call(0x4007bbe2,pid,raw))==expected
    # The value pop-up's title (the selected model's label lookup): ATK and
    # REL carry their unit there, the others keep Digitakt's long names.
    for pid,expected in ((ID,'Threshold'),(ID+1,'Attack (ms)'),(ID+2,'Release (ms)'),(ID+3,'Makeup Gain')):
        assert m.string(m.call(0x40013730,W,pid))==expected,(hex(pid),m.string(m.call(0x40013730,W,pid)))
    if out:
        out.parent.mkdir(parents=True,exist_ok=True)
        words=struct.unpack('>256I',m.read(pixels,1024));rows=bytearray()
        for y in reversed(range(64)):
            row=bytearray(b'\0')
            for x in range(128):
                on=words[2*x+y//32]&(1<<(31-y%32))
                row.extend(bytes((218,240,235) if on else (20,26,28))*6)
            rows.extend(row*6)
        def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
        out.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',768,384,8,2,0,0,0))+
                        chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
    m.w(view+0x90,0);m.dials.clear();m.call('comp_draw',view,bitmap,0)
    assert not m.dials and m.word(pages)==32
    print('PASS native Source/base navigation, seven original Digitakt dials/formats, GR readout on F, SCF on G and DRIVE retained')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--core',type=Path,required=True);p.add_argument('--bank',type=Path,required=True);p.add_argument('--preview',type=Path);a=p.parse_args()
    m=fixture(a.core,a.bank);test_controls(m);test_math(a.core,a.bank);test_fx_site(a.core,a.bank)
    test_scf(a.core,a.bank)
    test_persistence(fixture(a.core,a.bank))
    test_mirror_power_cycle(a.core,a.bank)
    test_native_codec(a.core,a.bank);test_page(a.core,a.bank,a.preview)
    print('PASS comp-st; no device access')
if __name__=='__main__':main()
