"""Differential against untouched Digitone 1.44 selection; no instrument IO.

Compile authored C/assembly using the Syntakt profile, run it on a CFV4E emulator, and
compare with the donor across modes, order, bit boundaries, rests and offsets.
This is deliberately not a full-firmware/clock/UI integration test.
"""
import argparse, hashlib, itertools, json, os, random, struct, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from elekloader.devices.syntakt import DEVICE
from elekloader.sdk import elf
from elekloader.sdk.syntakt.audio import audit_functions
from unicorn import Uc,UC_ARCH_M68K,UC_MODE_BIG_ENDIAN,UC_HOOK_CODE
from unicorn.m68k_const import *

DONOR_SHA='fce648a97c6c5d93b961732e8f8db6b02e0820c6d344c7e2131fa05a4b3168e4'
BASE,ENTRY,END=0x40000400,0x40097e46,0x40098130
STATE,MASKS,NODES,HEADS,TAILS=0x413915f4,0x41391694,0x413916d4,0x413926e4,0x413926d4
IMAGE,OWN,SETTINGS,SOUND,SP,RETURN=0x47000000,0x49000000,0x49010000,0x49020000,0x4dfff000,0x4dff0000
SAVED=[UC_M68K_REG_D2,UC_M68K_REG_D3,UC_M68K_REG_D4,UC_M68K_REG_D5,UC_M68K_REG_D6,
       UC_M68K_REG_D7,UC_M68K_REG_A2,UC_M68K_REG_A3,UC_M68K_REG_A4,UC_M68K_REG_A5,UC_M68K_REG_A6]

class Lab:
    def __init__(self,donor,image):
        self.u=u=Uc(UC_ARCH_M68K,UC_MODE_BIG_ENDIAN);u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        for a,n in [(0x40000000,0x300000),(0x41390000,0x10000),(IMAGE,0x10000),
                    (OWN,0x40000),(0x4dff0000,0x10000)]:u.mem_map(a,n)
        u.mem_write(BASE,donor);u.mem_write(RETURN,b'\x4e\x71')
        e=elf.Elf.load(image);section=e.section('.image');u.mem_write(IMAGE,section.data)
        self.sym={s.name:s.value for s in e.symbols if s.name}
        self.audit=audit_functions(e,'.image',IMAGE)
        self.count=0;u.hook_add(UC_HOOK_CODE,self.trace)
    def trace(self,*_):self.count+=1
    def w(self,a,n):self.u.mem_write(a,struct.pack('>I',n&0xffffffff))
    def read(self,a,n):return bytes(self.u.mem_read(a,n))
    def call(self,fn,*args):
        u=self.u
        for i,r in enumerate(SAVED):u.reg_write(r,0x13570000+i*256)
        saved=[u.reg_read(r) for r in SAVED]
        u.reg_write(UC_M68K_REG_SR,0x2000);u.reg_write(UC_M68K_REG_A7,SP)
        u.mem_write(SP,struct.pack('>%dI'%(1+len(args)),RETURN,*args))
        self.count=0;u.emu_start(self.sym.get(fn,fn),RETURN,count=30000)
        assert u.reg_read(UC_M68K_REG_PC)==RETURN,(fn,hex(u.reg_read(UC_M68K_REG_PC)))
        assert u.reg_read(UC_M68K_REG_A7)==SP+4
        assert [u.reg_read(r) for r in SAVED]==saved,fn
        v=u.reg_read(UC_M68K_REG_D0)
        return v if v<0x80000000 else v-0x100000000,self.count
    def fixture(self,track,mode,notes,octaves,mask,length,offsets):
        self.u.mem_write(0x41390000,bytes(0x10000));self.u.mem_write(SOUND,bytes(0x200))
        state=STATE+40*track
        for off in [0,12,16]:self.w(state+off,0xffffffff)
        self.w(state+24,1);self.w(state+32,SOUND)
        self.u.mem_write(SOUND+0x10f,bytes([mode,0,octaves,0,length]))
        self.u.mem_write(SOUND+0x114,struct.pack('>H',mask))
        self.u.mem_write(SOUND+0x116,bytes(x&255 for x in offsets))
        nodes=[NODES+track*1024+note*8 for note in notes]
        for i,(note,node) in enumerate(zip(notes,nodes)):
            self.w(node,nodes[i-1] if i else 0);self.w(node+4,nodes[i+1] if i+1<len(nodes) else 0)
            a=MASKS+track*16+(note//32)*4
            self.w(a,int.from_bytes(self.read(a,4),'big')|1<<(note&31))
        self.w(HEADS+track*4,nodes[0] if nodes else 0);self.w(TAILS+track*4,nodes[-1] if nodes else 0)
        own=OWN+180*(track+8)
        self.call('arp_reset',own,1)
        for note in notes:assert self.call('arp_hold',own,note)[0]==1
        self.u.mem_write(SETTINGS,struct.pack('>5BxH16b',mode,0,octaves,0,length,mask,*offsets))
        return own

def build(out,toolchain):
    out.mkdir(parents=True,exist_ok=False)
    env=dict(os.environ);env['GCC_EXEC_PREFIX']=str(toolchain).replace('\\','/')+'/'
    env['PATH']=str(toolchain)+os.pathsep+env.get('PATH','')
    objects=[]
    for name in ['selector','input','ui','clock','length']:
        obj=out/(name+'.o');objects.append(obj)
        subprocess.run([str(toolchain/'gcc.exe'),*DEVICE.toolchain['cflags'],'-fno-jump-tables',
                        '-c',str(ROOT/'mods/arp-st'/str(name+'.c')),'-o',str(obj)],env=env,check=True)
    # Production selector entry is authored assembly on ColdFire; host C stays
    # available as an independent implementation. Keep the native oracle checks.
    obj=out/'selector-asm.o';objects.append(obj)
    subprocess.run([str(toolchain/'as.exe'),*DEVICE.toolchain['asflags'],
                    str(ROOT/'mods/arp-st/selector.s'),'-o',str(obj)],env=env,check=True)
    script=out/'lab.ld';script.write_text('SECTIONS { . = 0x47000000; .image : { *(.text*) *(.rodata*) *(.data*) *(.bss*) } }')
    image=out/'selector.elf'
    subprocess.run([str(toolchain/'ld.exe'),'-T',str(script),'-o',str(image),*map(str,objects)],env=env,check=True)
    return image

def clocks(lab):
    """Execute actual target clock functions at native frame boundaries."""
    state=OWN+0x31000;due=SETTINGS+0x300;polls=0
    den=[1,2,3,4,5,6,8,10,12,16,20,24,32,40,48,64,80,96]
    for speed,d in enumerate(den):
        for tempo in [3600,14400,36000]:
            step=2*tempo;period=86400000//d
            assert lab.call('arp_clock_arm',state,0,speed,1)[0]==1
            for n in range(d):
                tick=n*period;begin=tick//step*step
                if begin:
                    assert lab.call('arp_clock_poll',state,begin-step,begin,1,due)[0]==0
                    polls+=1
                assert lab.call('arp_clock_poll',state,begin,begin+step,1,due)[0]==1
                assert int.from_bytes(lab.read(due,4),'big')==tick;polls+=1
            assert int.from_bytes(lab.read(state+16,4),'big')==0
    assert lab.call('arp_clock_arm',state,0xfffffff0,17,7)[0]==1
    assert lab.call('arp_clock_poll',state,0xfffffff0,0x7070,7,due)[0]==1
    assert int.from_bytes(lab.read(due,4),'big')==0xfffffff0
    assert lab.call('arp_clock_poll',state,0x7070,0xe0f0,8,due)[0]==0
    assert int.from_bytes(lab.read(state+12,4),'big')==0
    return dict(configurations=54,checked_polls=polls,wrap_and_stale=True)

def verify(donor_path,out,toolchain):
    donor=donor_path.read_bytes();assert hashlib.sha256(donor).hexdigest()==DONOR_SHA
    lab=Lab(donor,build(out,toolchain));rng=random.Random(29012026)
    sets=[[],[60],[60,64,67],[67,60,64],[0,31,32,63,64,95,96,127],list(range(128))]
    native_cost=[];source_cost=[];calls=0
    # Include the donor's extreme masks/notes as well as randomized insertion order.
    configs=list(itertools.product(range(5),range(len(sets)),[0,3],[0,0xffff,0xa55a],[0,15]))
    for c,(mode,which,octaves,mask,length) in enumerate(configs):
        notes=sets[which];offsets=[rng.randrange(-24,25) for _ in range(16)]
        track=c%4;own=lab.fixture(track,mode,notes,octaves,mask,length,offsets)
        for step in range(32):
            expected,n=lab.call(ENTRY,track,1);actual,m=lab.call('arp_next',own,SETTINGS,1)
            assert actual==expected,dict(mode=mode,notes=notes,range=octaves,mask=mask,length=length,step=step,actual=actual,expected=expected)
            native_cost.append(n);source_cost.append(m);calls+=1
        assert lab.call(ENTRY,track,2)[0]==lab.call('arp_next',own,SETTINGS,2)[0]==-2
    # An independently changed source ordering must fail the original oracle.
    own=lab.fixture(0,1,[67,60,64],0,0xffff,15,[0]*16)
    lab.u.mem_write(SETTINGS,bytes([2]))
    assert lab.call(ENTRY,0,1)[0]!=lab.call('arp_next',own,SETTINGS,1)[0]
    # Mutating held notes: duplicate, release, reinsert in TRUE order, no cross-track state.
    for t in range(12):
        s=OWN+180*t;lab.call('arp_reset',s,1)
        for n in [67,60,64]:assert lab.call('arp_hold',s,n)[0]==1
        assert lab.call('arp_hold',s,60)[0]==0
        assert lab.call('arp_release',s,60)[0]==1
        assert lab.call('arp_hold',s,60)[0]==1
        assert lab.call('arp_hold',s,128)[0]==0
        assert lab.call('arp_release',s,128)[0]==0
    lab.u.mem_write(SETTINGS,struct.pack('>5BxH16b',1,0,0,0,15,0xffff,*([0]*16)))
    for t in range(12):
        assert [lab.call('arp_next',OWN+180*t,SETTINGS,1)[0] for _ in range(4)]==[67,64,60,67]
    for mode in range(5):
        lab.u.mem_write(SETTINGS,bytes([mode]));lab.call('arp_activate',SETTINGS)
        assert lab.read(SETTINGS,1)==bytes([mode])
    result=dict(ok=True,donor_main_sha256=DONOR_SHA,donor_selector=[hex(ENTRY),hex(END)],
                configurations=len(configs),compared_calls=calls,tracks=12,negative_controls=1,
                donor_instructions=[min(native_cost),max(native_cost)],source_instructions=[min(source_cost),max(source_cost)],
                clock=clocks(lab),audit=lab.audit,source_sha256={n:hashlib.sha256((ROOT/'mods/arp-st'/n).read_bytes()).hexdigest() for n in ['arp.h','selector.c','selector.s','input.c','ui.h','ui.c','clock.h','clock.c']},
                limits=['Selector and clock only; no native scheduling, note events, persistence or UI execution.',
                        'Instruction counts are emulator instructions, not cycles or physical CPU percentages.',
                        'Held-note mutation has source invariants but no complete donor lifecycle differential.'],device_access=False)
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--digitone-main',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--toolchain',type=Path,required=True)
    a=p.parse_args();verify(a.digitone_main,a.out,a.toolchain)
