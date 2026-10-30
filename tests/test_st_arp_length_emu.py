"""LEN differential: original DN producer, labels and native gate countdowns.

Executes SHA-pinned DN instructions, compiled authored C and stock/core ST
counter loops. Checks the gate-to-expiry flag boundary; synthesis, envelopes,
external MIDI and physical time/audio are outside this test's scope.
"""
import argparse,hashlib,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_arp_selector_emu import Lab,build,DONOR_SHA,OWN,SOUND,BASE
from test_st_arp_generator_emu import Machine
from unicorn import Uc,UC_ARCH_M68K,UC_MODE_BIG_ENDIAN
from unicorn.m68k_const import *

EVENT,FRAME,WORK,TEXT=OWN+0x1000,OWN+0x3000,OWN+0x4000,OWN+0x5000

def write(u,a,n):u.mem_write(a,struct.pack('>I',n&0xffffffff))
def word(u,a):return int.from_bytes(u.mem_read(a,4),'big')
def string(u,a):
    s=bytearray()
    while len(s)<32:
        c=u.mem_read(a,1)[0];a+=1
        if not c:break
        s.append(c)
    return s.decode('ascii')

class Counter:
    def __init__(self,u,digitone):self.u=u;self.dn=digitone
    def arm(self,gate,tempo,voice):
        u=self.u;self.tempo=tempo;self.voice=voice
        if self.dn:
            self.address=0x80003f34+4*voice
            u.mem_write(0x80003f14,bytes(64));write(u,0x40242094,tempo)
        else:
            self.address=0x80003084+4*voice
            u.mem_write(0x80003050,bytes(26*4))
        write(u,self.address,gate)
    def step(self):
        u=self.u;u.reg_write(UC_M68K_REG_SR,0x2500)
        if self.dn:
            u.mem_write(WORK,bytes(48))
            for reg,v in [(UC_M68K_REG_A2,WORK),(UC_M68K_REG_A5,0x80003f14),(UC_M68K_REG_A4,0x80003f34),
                          (UC_M68K_REG_D2,0),(UC_M68K_REG_D3,1),(UC_M68K_REG_D4,0x4009f9dc)]:u.reg_write(reg,v)
            u.emu_start(0x4009d2a4,0x4009d2e4,count=1000);assert u.reg_read(UC_M68K_REG_PC)==0x4009d2e4
            flags=word(u,WORK+44)
        else:
            write(u,FRAME-52,0);write(u,FRAME-80,0)
            for reg,v in [(UC_M68K_REG_A6,FRAME),(UC_M68K_REG_A1,0x80003050),(UC_M68K_REG_A0,0x80003084),
                          (UC_M68K_REG_D0,0),(UC_M68K_REG_D1,self.tempo),(UC_M68K_REG_D2,1)]:u.reg_write(reg,v)
            u.emu_start(0x400a3970,0x400a39a0,count=1000);assert u.reg_read(UC_M68K_REG_PC)==0x400a39a0
            flags=word(u,FRAME-52)
        assert flags in [0,1<<self.voice]
        return word(u,self.address),flags

def target(image):
    u=Uc(UC_ARCH_M68K,UC_MODE_BIG_ENDIAN);u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
    for a,n in [(0x40000000,0x800000),(OWN,0x10000),(0x80000000,0x10000)]:u.mem_map(a,n)
    u.mem_write(BASE,image);return u

def verify(donor,stock,core,bank,toolchain,out):
    raw=donor.read_bytes();assert hashlib.sha256(raw).hexdigest()==DONOR_SHA
    lab=Lab(raw,build(out,toolchain));u=lab.u;u.mem_map(0x80000000,0x10000)
    # The two native counter loops used below have no calls or owning models.
    # Stock and delivered core are both executed, not modeled in Python.
    native=stock.read_bytes();patched=core.read_bytes()
    assert hashlib.sha256(native).hexdigest()=='8cf2b4b4ea80d58953da46ca453328454fb3fca5cb5f844a2f7bad4b72241f53'
    a,z=0x400a3970-BASE,0x400a39a0-BASE;assert native[a:z]==patched[a:z]
    dn=Counter(u,True);st=Counter(target(native),False);installed=Counter(target(patched),False)
    rows=[];count=0
    for n in range(128):
        u.mem_write(SOUND+0x112,bytes([n]));u.reg_write(UC_M68K_REG_A5,SOUND);u.reg_write(UC_M68K_REG_A3,EVENT)
        u.emu_start(0x4009e78a,0x4009e79c,count=20);assert u.reg_read(UC_M68K_REG_PC)==0x4009e79c
        original=word(u,EVENT+52)
        authored,_=lab.call('arp_note_gate',n);assert authored==original,(n,authored,original)
        lab.call('arp_format_note_length',TEXT,n)
        label=string(u,word(u,0x40233c04+4*n));assert string(u,TEXT)==label,(n,string(u,TEXT),label)
        for voice in [0,7]:
            # Execute the original consumer that installs the produced gate.
            u.reg_write(UC_M68K_REG_A6,FRAME);u.reg_write(UC_M68K_REG_A3,EVENT);write(u,FRAME-128,voice)
            u.emu_start(0x4009ea70,0x4009ea88,count=20)
            assert word(u,0x80003f34+4*voice)==original
            for tempo in [3600,14400,36000]:
                # Exact expiry boundaries without executing millions of idle
                # frames for long notes: identical residuals enter actual loops.
                for residual in [original,2*tempo+1,tempo+1,tempo,1,0,0xffffffff]:
                    for c in [dn,st,installed]:c.arm(residual,tempo,voice)
                    for _ in range(4):
                        expected=dn.step();assert st.step()==expected and installed.step()==expected,(n,tempo,voice,residual)
                        count+=1
        rows.append(dict(length=n,label=label,gate=original))
    assert lab.call('arp_note_gate',128)[0]==0 and lab.call('arp_note_gate',255)[0]==0
    lab.call('arp_format_note_length',TEXT,255);assert string(u,TEXT)=='?'
    assert lab.call('arp_note_gate',1)[0]!=rows[0]['gate'],'negative control: wrong setting was ignored'
    # All settings also pass through the linked module, real configuration
    # bus and native queue/effects. A native-zero infinite root is retained
    # across LEN edits, so the source does not mask longer output gates.
    m=Machine(core,bank);pools=m.pool_counts();assert m.configure(note_length=0)==1
    m.event(length=0);generation=m.word(m.sym['arp_generation'])
    for row in rows:
        assert m.configure(note_length=row['length'])==1
        assert m.word(m.sym['arp_generation'])==generation
        m.frame(row['length']*5400000)
        assert len(m.log)==row['length']+1 and m.log[-1]['gate']==row['gate'],(row,m.log[-1])
    assert m.configure(mode=0)==1 and m.pool_counts()==pools
    assert m.gate_tick()==1 and m.gate_tick()==0
    result=dict(ok=True,settings=128,countdown_comparisons=count,tempos=[30,120,300],voices=[0,7],
        linked_module_outputs=len(m.log),linked_module_pool_counts=pools,
        donor_main_sha256=DONOR_SHA,source_audit=lab.audit,rows=rows,negative_controls=1,
        limits=['Gate producer, gate store and DN/ST stock/core countdowns execute native instructions.',
                'C formatting compares to original labels; no donor tables are shipped in the module.',
                'Linked module uses the native bus/pools/queue/pitch/gate, with explicit synthesis/owner-map boundaries as in generator test.',
                'Note-off flag boundary only; no synthesis/envelopes, full scheduler, external MIDI or hardware test.'],
        maximum_setting=dict(label=rows[-1]['label'],gate=rows[-1]['gate'],infinite=False,
                             note='Observed OS 1.44 lookup beyond the regular duration table; retained for parity.'),device_access=False)
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: 128 native LEN gates/labels and linked outputs; %d DN/ST stock/core countdown comparisons; max edge preserved'%count)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['digitone-main','stock-main','core','bank','toolchain','out']:p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();verify(a.digitone_main,a.stock_main,a.core,a.bank,a.toolchain,a.out)
