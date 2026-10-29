"""Core 0.12.18 lock preview without the heap: native vs patched, in Unicorn.

Runs the real 40039160 (view, ID, locked*) of two MAIN images: one where the
core leaves it stock (e.g. 0.12.17) and one with the 400391a8 site, with the
linked preview.s bytes at the site's target. Stubs stand for the application
queries, the view's vtable and operator new/delete, identically in both runs.
Each scenario compares the return value, the locked byte, the frame values,
callee-saved registers and SP, and the exact sequence of stub calls with their
arguments (allocator calls apart: the native run must allocate and free once
per held-trig read, the patched run never). Some lock queries change the
selection words, which both walks must read again per position.

LIBUNICORN_PATH=... <python with unicorn> tests/test_st_preview_emu.py \
    --native out/core-st-0.12.17-.../main.bin --patched out/core-st-0.12.18-.../main.bin \
    --object mods/core-st/out/core.work/preview.s.o
"""
import argparse,random,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from elekloader.sdk import elf
from unicorn import Uc,UC_ARCH_M68K,UC_MODE_BIG_ENDIAN,UC_HOOK_CODE,UC_HOOK_MEM_UNMAPPED,UC_PROT_ALL
from unicorn.m68k_const import *

BASE=0x40000400;SITE=0x400391a8;ENTRY=0x40039160;RETURN=0x4f000000
STACK=0x47ff0000;VIEW=0x49000000;SELECTION=0x49001000;VTABLE=0x49002000;STUBS=0x4a000000
APP,MODEL=0x11110000,0x22220000
NATIVE={0x400a61a4:'flags',0x400c2d2a:'held',0x4016e804:'app',0x40019036:'model',0x40027f3a:'length',
        0x400c34d6:'active',0x40103788:'new',0x40103794:'delete'}
VIRTUAL={0x90:'lock',0xa4:'pool',0x54:'value'}
SAVED=[UC_M68K_REG_D2,UC_M68K_REG_D3,UC_M68K_REG_D4,UC_M68K_REG_D5,UC_M68K_REG_D6,UC_M68K_REG_D7,
       UC_M68K_REG_A2,UC_M68K_REG_A3,UC_M68K_REG_A4,UC_M68K_REG_A5,UC_M68K_REG_A6]

class Machine:
    def __init__(self,image,preview=None):
        self.uc=uc=Uc(UC_ARCH_M68K,UC_MODE_BIG_ENDIAN)
        uc.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        size=(len(image)+0x400+0xffff)&~0xffff
        uc.mem_map(0x40000000,size);uc.mem_write(BASE,image)
        for at,n in ((0x46000000,0x10000),(STACK-0x10000,0x20000),(VIEW,0x10000),(STUBS,0x10000),(RETURN,0x1000),
                     (0x4c000000,0x100000)):
            uc.mem_map(at,n)
        if preview:
            target=struct.unpack_from('>I',image,SITE+2-BASE)[0]
            assert image[SITE-BASE:SITE-BASE+2]==b'\x4e\xf9' and image[SITE+6-BASE:SITE+8-BASE]==b'\x4e\x71'
            assert 0x46000000<=target<0x46010000;uc.mem_write(target,preview)
        else:assert image[SITE-BASE:SITE-BASE+8]==bytes.fromhex('42af00204878001c')
        self.stubs=dict(NATIVE)
        for k,(off,name) in enumerate(VIRTUAL.items()):
            self.stubs[STUBS+16*k]=name;uc.mem_write(VTABLE+off,struct.pack('>I',STUBS+16*k))
        for at in self.stubs:
            if at>=STUBS:uc.mem_write(at,b'\x4e\x75')
        uc.mem_write(RETURN,b'\x4e\x71'*8)
        uc.hook_add(UC_HOOK_CODE,self.code)
        uc.hook_add(UC_HOOK_MEM_UNMAPPED,self.unmapped)
        self.stray=[]

    def unmapped(self,uc,access,at,size,value,user):
        self.stray.append(hex(at));return False

    def args(self,n):
        sp=self.uc.reg_read(UC_M68K_REG_A7)
        return struct.unpack('>%dI'%(n+1),bytes(self.uc.mem_read(sp,4*(n+1))))

    def code(self,uc,at,size,user):
        name=self.stubs.get(at)
        if not name:return
        s=self.s;w=self.args(3)
        if name=='flags':r,a=s['flags'],(w[1],)
        elif name=='held':r,a=s['held'],(w[1],)
        elif name=='app':r,a=APP,()
        elif name=='model':r,a=MODEL,(w[1],)
        elif name=='length':r,a=s['length'],(w[1],)
        elif name=='active':r,a=s['active'],(w[1],)
        elif name=='new':
            r,a=self.heap,(w[1],);self.heap+=(w[1]+15)&~15
        elif name=='delete':r,a=0,(w[1],)
        elif name=='lock':
            r,a=s['locks'].get(w[2],0xffffffff),(w[1],w[2],w[3])
            change=s['changes'].get(w[2])
            if change:uc.mem_write(SELECTION+0x158,struct.pack('>2I',*change))
        elif name=='pool':r,a=s['pools'].get(w[2],0xffffffff),(w[1],w[2])
        elif name=='value':r,a=s['value'],(w[1],w[2],w[3])
        self.log.append((name,)+a)
        uc.reg_write(UC_M68K_REG_D0,r&0xffffffff)
        uc.reg_write(UC_M68K_REG_A7,uc.reg_read(UC_M68K_REG_A7)+4);uc.reg_write(UC_M68K_REG_PC,w[0])

    def run(self,s):
        uc=self.uc;self.s=s;self.log=[];self.heap=0x4c000000
        uc.mem_write(VIEW,bytes(0x100));uc.mem_write(VIEW,struct.pack('>I',VTABLE))
        uc.mem_write(VIEW+0x6c,struct.pack('>I',SELECTION))
        uc.mem_write(SELECTION+0x158,struct.pack('>2I',s['high'],s['low']))
        locked=VIEW+0x200;uc.mem_write(locked,b'\xaa')
        sp=STACK;uc.mem_write(sp-0x400,bytes(0x400))
        sp-=16;uc.mem_write(sp,struct.pack('>4I',RETURN,VIEW,s['id'],locked))
        regs=[0x1000+k for k in range(len(SAVED))]
        for r,v in zip(SAVED,regs):uc.reg_write(r,v)
        uc.reg_write(UC_M68K_REG_A7,sp)
        uc.emu_start(ENTRY,RETURN,count=200000)
        assert uc.reg_read(UC_M68K_REG_PC)==RETURN and not self.stray,self.stray
        assert [uc.reg_read(r) for r in SAVED]==regs and uc.reg_read(UC_M68K_REG_A7)==sp+4
        return uc.reg_read(UC_M68K_REG_D0),bytes(uc.mem_read(locked,1)),self.log

def scenario(rng):
    length=rng.choice([0,1,15,16,31,32,33,48,63,64,rng.randrange(0,65)])
    bits=lambda:rng.choice([0,0xffffffff,1<<rng.randrange(32),rng.getrandbits(32),rng.getrandbits(32)&rng.getrandbits(32)])
    positions=list(range(64));rng.shuffle(positions)
    locks={p:rng.choice([0,0x4000,0x7f00,rng.getrandbits(16)]) for p in positions[:rng.choice([0,0,1,3,20])]}
    pools={p:rng.randrange(128) for p in positions[:rng.choice([0,0,1,5,30])]}
    changes={p:(bits(),bits()) for p in positions[:rng.choice([0,0,0,2])]}
    return dict(length=length,high=bits(),low=bits(),held=rng.choice([1,1,1,0,0x100]),active=rng.choice([1,1,1,0,0x100]),
                flags=rng.choice([0,0,1,0x10000]),id=rng.choice([0x52,0x21c,0x13,0x10,0x239,rng.randrange(0x400)]),
                locks=locks,pools=pools,changes=changes,value=rng.getrandbits(16))

def verify(native,patched,obj,count):
    o=elf.Elf.load(obj);preview=o.section('.run').data
    assert preview and len(preview)<=256
    a=Machine(native.read_bytes());b=Machine(patched.read_bytes(),preview)
    rng=random.Random(20261028);held=allocations=0;locked=0
    for n in range(count):
        s=scenario(rng);ra,la,loga=a.run(s);rb,lb,logb=b.run(s)
        calls=lambda log:[c for c in log if c[0] not in ('new','delete')]
        assert (ra,la)==(rb,lb) and calls(loga)==calls(logb),(n,s,ra,rb,la,lb,loga,logb)
        heap=[c[0] for c in loga if c[0] in ('new','delete')]
        assert heap==(['new','delete'] if s['held']&255 else []),(n,heap)
        assert not any(c[0] in ('new','delete') for c in logb)
        held+=bool(s['held']&255);allocations+=len(heap)//2;locked+=lb==b'\x01'
    print(f'PASS: {count} reads of 40039160, native {native.parent.name} vs patched {patched.parent.name}: '
          f'same value, lock flag, registers and call sequence; {held} with a trig held ({locked} locked), '
          f'{allocations} native new/delete pairs, none patched')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--native',type=Path,required=True);p.add_argument('--patched',type=Path,required=True)
    p.add_argument('--object',type=Path,required=True);p.add_argument('--count',type=int,default=4000)
    a=p.parse_args();verify(a.native.resolve(),a.patched.resolve(),a.object.resolve(),a.count)
