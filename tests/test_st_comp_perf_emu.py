"""Bit-exact COMP optimization against the previous linked release and MIT math.

Both callbacks execute at the actual core FX-CV site, including native clamps.
Random control changes retain envelope history; Kit switches, ADC extrema,
fractional controls, MIX endpoints and near-threshold peaks are included.
No physical timing or device access.
"""
import argparse
import importlib.util
from pathlib import Path
import random
import struct
from test_st_comp_emu import fixture,set_values,block,W,KIT,ID,ROOT


def bind(m):
    m.w(W+0x1d00,m.sym['comp_audio']);m.w(W+0x1d04,0)
    m.w(m.cs['core:core_fx_cv_table']+2,W+0x1d00)


def verify(core,bank,previous,sram=False):
    spec=importlib.util.spec_from_file_location('compact',ROOT/'mods/comp-st/upstream/dsp.py')
    d=importlib.util.module_from_spec(spec);spec.loader.exec_module(d)
    def optimized():
        m=fixture(core,bank)
        if sram:assert m.call('comp_place')==3
        return m
    def meter(m):
        return m.word(m.word(m.sym['comp_hot'])+52) if 'comp_hot' in m.sym else m.word(m.sym['comp_gr_meter'])
    old,new=fixture(core,previous),optimized()
    bind(old);bind(new);rng=random.Random(0x4350);checks=0
    # Same uncompressed gain for every integer MUP plus fractional settings;
    # this also validates the 16-bit multiply's maximum operand (<65536).
    for makeup in sorted(set(list(range(0,0x7f01,256))+[rng.randrange(0x7f01) for _ in range(128)])):
        values=[0x7f00,0,0,makeup,0,0x7f00]
        set_values(new,values)
        gain=d.Compact(d.Preset(*values[:4],0)).block([0]*32,[0]*32)
        assert 0<=gain<65536
        for cv in ((0,65535),(1,4096),(65536,0x7fffffff),(0xffffffff,12345)):
            got=block(new,[0]*32,[0]*32,cv,True)
            expected=tuple((v&65535) if v&0x80000000 else min(65535,(min(v,65535)*gain)>>12) for v in cv)
            assert got==expected,(makeup,cv,got,expected)
            checks+=1
    # Fresh states, then persist history across changing all six controls.
    old,new=fixture(core,previous),optimized();bind(old);bind(new)
    for n in range(1600):
        if n%13==0:
            kit=KIT+(n//13%3)*0x1618
            old.w(0x800030b8,kit);new.w(0x800030b8,kit)
        values=[rng.randrange(0x7f01) for _ in range(4)]+[rng.randrange(8)<<8,rng.randrange(0x7f01)]
        if n%17==0:values[5]=0
        if n%19==0:values=[0x7f00]*4+[0x700,0x7f00]
        set_values(old,values);set_values(new,values)
        # Audit the task-prepared row against the independent upstream model.
        config=new.call('comp_current_config',new.word(0x800030b8))
        words=struct.unpack('>6i4I',new.read(config,40))
        pr=d.Preset(*values[:4],values[4]>>8)
        # MCF54415 FI=1/RT=0 truncates each product to 40 bits before
        # accumulating. The vendored ideal frac2 uses full 64-bit products.
        k,rn=0,pr.rel
        for _ in range(32):
            k=((pr.att*k>>23)+((0x7fffffff-pr.att)*rn>>23))>>8
            rn=d.frac(rn,pr.rel)
        pr.k=k
        assert words[:6]==(pr.thr,pr.slope,pr.mk,pr.a32,pr.r32,pr.k),(n,values,words,pr.__dict__)
        assert d.Compact.log(words[8])<=pr.thr,'unsafe idle threshold'
        wet=(values[5]<<15)//0x7f00
        assert words[6:8]==(wet,4096-(wet>>3))
        mode=n%8
        if mode<2:left=right=[0]*32
        elif mode==2:left=[-2**31]*32;right=[2**31-1]*32
        elif mode<6:
            peak=max(256,min(0x7fffffff,words[8]+mode-4))
            left=[peak]*32;right=[-peak-1]*32
        else:
            left=[rng.randrange(-2**31,2**31) for _ in range(32)]
            right=[rng.randrange(-2**31,2**31) for _ in range(32)]
        cv=(rng.choice([0,1,40000,65535,65536,0x7fffffff,0xffffffff]),rng.randrange(0x10000))
        a,b=block(old,left,right,cv,True),block(new,left,right,cv,True)
        assert a==b,(n,values,cv,a,b)
        old_state=struct.unpack('>2i',old.read(old.sym['comp_k_state'],8))
        new_state=struct.unpack('>2i',new.read(new.word(new.sym['comp_hot'])+44,8))
        old_config=struct.unpack('>11i',old.read(old.sym['comp_k_const'],44))
        assert old_state==new_state,(n,values,mode,words,old_config,'state',old_state,new_state)
        assert meter(old)==meter(new),(n,values,old_state,new_state,'meter',meter(old),meter(new))
        checks+=1
    print('PASS optimized COMP:',checks,'blocks; linked old/new output and GR exact, all integer MUP gains, random Q8 coefficients, Kit/control transitions and VCA/ADC extrema')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('core','bank','previous'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--sram',action='store_true')
    a=p.parse_args();verify(a.core,a.bank,a.previous,a.sram)
