"""Same-layout idle diagnostic: actual directory, binder, stubs and MSKIP.

LFO3, ARP, POLY and MSKIP initializers execute, as do diagnostic initializers.
Other module initializers/native heap/mutex are explicit fixture boundaries.
No complete hardware boot, scheduler or CPU percentage is claimed.
"""
import argparse
import hashlib
import json
import struct
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
from test_st_patchers_emu import Patchers
from test_st_module_startup_emu import BANK,DESCRIPTORS
from test_st_safe_boot_emu import SP
from tools.build_st_idle_bisect import words,KEEP
from unicorn.m68k_const import *


class Diagnostic(Patchers):
    def __init__(self,core,bank,broken_interest=False):
        super().__init__(core,bank)
        self.broken_interest=broken_interest
        self.stub_calls=0
        self.stub=self.bs['st_idle_diag_initialize']
        self.noop=self.bs['st_idle_diag_noop']
        raw=bank.read_bytes()
        directory,count=words(raw,32,2)
        names=json.loads(bank.with_suffix('.smz4.json').read_text())['modules']
        # Startup's historical fixture reads ELF-symbol descriptors. Exercise
        # the actual packed directory instead, as machine-boot does.
        self.modules=[];self.init_entries={};self.real={}
        self.expected=[list(x) for x in self.original]
        for i in range(count):
            at=BANK+directory+16*i
            row=bytes(self.u.mem_read(at,16))
            name,init,subs,n=struct.unpack('>4I',row)
            label=self.c_string(name)
            self.u.mem_write(DESCRIPTORS+16*i,row)
            events=[]
            for j in range(n):
                event,fn=struct.unpack('>2I',bytes(self.u.mem_read(subs+8*j,8)))
                self.expected[event&255].append(fn|(0x80000000 if event&0x100 else 0)|(1 if event&0x200 else 0))
                events.append(event)
            self.modules.append((label,init,events))
            if init not in (0xffffffff,self.stub):
                self.init_entries[init]=label
                if names[i] in ('lfo3_module','modulation_skip_module','arp_module','poly_module'):
                    self.real[init]=label
        self.count=count

    def code(self,u,pc,n,user):
        if pc==self.stub:
            self.stub_calls+=1
            if self.broken_interest:self.ret(0)
            return
        super().code(u,pc,n,user)

    def boot(self):
        saved=(UC_M68K_REG_D2,UC_M68K_REG_D3,UC_M68K_REG_D4,UC_M68K_REG_D5,
               UC_M68K_REG_D6,UC_M68K_REG_D7,UC_M68K_REG_A2,UC_M68K_REG_A3,
               UC_M68K_REG_A4,UC_M68K_REG_A5,UC_M68K_REG_A6)
        values=[0x12340000+256*i for i in range(len(saved))]
        for reg,value in zip(saved,values):self.u.reg_write(reg,value)
        assert self.call('core_modules_bind',DESCRIPTORS,self.count)==1
        assert [self.u.reg_read(r) for r in saved]==values
        assert self.u.reg_read(UC_M68K_REG_A7)==SP+4
        assert [self.event_list(i) for i in range(15)]==self.expected
        assert self.stub_calls==sum(init==self.stub for _,init,_ in self.modules)
        return bytes(self.u.mem_read(self.sym['core:core_native_interest'],24))


def verify(core,reference,catalog,parser):
    before,after=reference.read_bytes(),catalog.read_bytes()
    assert len(before)==len(after)
    size,nrel=words(before,52,2);start=80+4*nrel
    report=json.loads(catalog.with_suffix('.smz4.json').read_text())
    changes=report['diagnostic']['changes']
    allowed={16,17,18,19,68,69,70,71}
    for row in changes:allowed.update(range(start+row['offset'],start+row['offset']+4))
    assert all(a==b or i in allowed for i,(a,b) in enumerate(zip(before,after)))
    assert before[80:start]==after[80:start]
    audio_at,audio_size=words(before,72,2)
    assert before[48+audio_at:48+audio_at+audio_size]==after[48+audio_at:48+audio_at+audio_size]
    for bank in (reference,catalog):
        subprocess.run([str(parser.resolve()),str(bank.resolve())],check=True)
    rows=[]
    native=(core/'main.bin').read_bytes()
    for bank in (reference,catalog):
        m=Diagnostic(core,bank)
        interest=m.boot()
        full=bank==reference
        assert bool(interest[17])==full,interest.hex()
        assert interest[16]==0,'inactive POLY must not replace modulation'
        if not full:assert interest==bytes(24),interest.hex()
        slots=sum(bool(m.word(m.sym['core:core_native_masks']+12*i)) for i in range(4))
        assert slots==(4 if full else 1),slots
        assert m.word(m.bs['modulation_skip_mode'])==2
        assert bool(m.word(m.lfo['hooks_installed']))==full
        assert bool(m.word(m.lfo['in_sram']))==full
        at=0x40115f78
        original=native[at-0x40000400:at-0x40000400+8]
        assert (bytes(m.u.mem_read(at,8))==original)!=full
        # All typed callback signatures may pass extra words to the no-op.
        m.sym['core:diagnostic_noop']=m.noop
        assert m.call('diagnostic_noop',1,2,3,4)==0
        assert m.u.reg_read(UC_M68K_REG_A7)==SP+4
        m.u.mem_write(m.sym['core:core_native_interest']+16,b'\0')
        m.sym['core:probe_modulation']=0x40097afc
        for _ in range(128):m.call('probe_modulation',0x80006348)
        counters={name:m.word(m.bs['modulation_skip_'+name]) for name in ('blocks','hits','skips','errors')}
        assert counters==dict(blocks=128,hits=127,skips=124,errors=0),counters
        rows.append(dict(bank=bank.name,stub_initializers=m.stub_calls,
                         LFO3_hooks=full,native_interest=interest.hex(),native_slots=slots,**counters))
    broken=Diagnostic(core,catalog,broken_interest=True)
    assert broken.boot()!=bytes(24),'negative control failed to expose fail-open dispatch'
    # A data corruption must still be rejected by the unmodified real parser.
    corrupt=catalog.with_suffix('.corrupt.smz4')
    raw=bytearray(after);raw[start+size-1]^=1;corrupt.write_bytes(raw)
    assert subprocess.run([str(parser.resolve()),str(corrupt.resolve())],capture_output=True).returncode!=0
    return dict(ok=True,cases=rows,same_MAIN_code_offsets=True,AUDIO_identical=True,
                negative_controls=['disabled native interest not registered','corrupted checksum rejected'],
                limits=['Unchanged native core binder/publisher and real LFO3/ARP/POLY/MSKIP/diagnostic initializers execute.',
                        'Other module initialization, native heap/mutex and DMA/scheduler are boundaries.',
                        'No device access, complete boot or hardware CPU timing.'],device_access=False)


def verify_variant(core,reference,bank,parser):
    """Check a complementary group split against the hardware-tested R."""
    raw,variant=reference.read_bytes(),bank.read_bytes()
    report=json.loads(bank.with_suffix('.smz4.json').read_text())
    info=report['diagnostic']
    pair={'lfo3_module','arp_module'}
    scenario=info['scenario']
    assert scenario in ('without-lfo3-arp','only-lfo3-arp','only-lfo3','lfo3-without-native-callback')
    if scenario=='without-lfo3-arp':disabled=pair
    elif scenario=='only-lfo3-arp':disabled=set(report['modules'])-KEEP-pair
    else:disabled=set(report['modules'])-KEEP-{'lfo3_module'}
    callbacks_only={'lfo3_module':[11]} if scenario=='lfo3-without-native-callback' else {}
    assert set(info['disabled_modules'])==disabled
    assert info.get('disabled_callbacks',{})==callbacks_only
    assert info['reference_sha256']==hashlib.sha256(raw).hexdigest()
    assert len(raw)==len(variant)
    size,nrel=words(raw,52,2);start=80+4*nrel
    assert variant[80:start]==raw[80:start]
    m=Diagnostic(core,bank)
    stub,no_op=m.stub-BANK,m.noop-BANK
    directory,count=words(raw,32,2)
    allowed=set(range(16,20))|set(range(68,72))
    for i,name in enumerate(report['modules']):
        at=directory+16*i
        label,init,subs,n=words(raw,start+at,4)
        expected_init=stub if name in disabled and init!=0xffffffff else init
        assert words(variant,start+at,4)==(label,expected_init,subs,n)
        if name in disabled and init!=0xffffffff:allowed.update(range(start+at+4,start+at+8))
        for j in range(n):
            event,fn=words(raw,start+subs+8*j,2)
            off=name in disabled or (event&255) in callbacks_only.get(name,())
            assert words(variant,start+subs+8*j,2)==(event,no_op if off else fn)
            if off:allowed.update(range(start+subs+8*j+4,start+subs+8*j+8))
    assert all(a==b or i in allowed for i,(a,b) in enumerate(zip(raw,variant)))
    subprocess.run([str(parser.resolve()),str(bank.resolve())],check=True)
    interest=m.boot()
    lfo='lfo3_module' not in disabled
    lfo_callback=lfo and not callbacks_only
    arp='arp_module' not in disabled
    poly='poly_module' not in disabled
    slots=sum(bool(m.word(m.sym['core:core_native_masks']+12*i)) for i in range(4))
    assert slots==1+int(lfo)+int(arp)+int(poly),slots
    assert interest[17]==(1 if lfo_callback else 0),interest.hex()
    assert interest[20]==(2 if arp else 0),interest.hex()
    assert interest[21]==(2 if arp else 0),interest.hex()
    if scenario=='lfo3-without-native-callback':assert interest==bytes(24),interest.hex()
    assert interest[16]==0 and interest[18:20]==bytes(2),interest.hex()
    assert bool(m.word(m.lfo['hooks_installed']))==lfo
    assert bool(m.word(m.lfo['in_sram']))==lfo
    assert ('POLY' in m.initialized)==poly
    assert ('ARP EXPERIMENT' in m.initialized)==arp
    assert m.word(m.bs['modulation_skip_mode'])==2
    at=0x40115f78
    target=int.from_bytes(bytes(m.u.mem_read(at+2,4)),'big')
    assert (target==m.bs['lfo3_owned_copy_entry'])==lfo
    m.sym['core:probe_modulation']=0x40097afc
    for _ in range(128):m.call('probe_modulation',0x80006348)
    counters={key:m.word(m.bs['modulation_skip_'+key]) for key in ('blocks','hits','skips','errors')}
    assert counters==dict(blocks=128,hits=127,skips=124,errors=0),counters
    return dict(bank=bank.name,scenario=scenario,disabled_modules=sorted(disabled),
                disabled_callbacks=callbacks_only,LFO3_hooks=lfo,LFO3_callback=bool(lfo_callback),
                native_slots=slots,native_interest=interest.hex(),**counters,
                executable_bytes_and_layout_unchanged=True,AUDIO_identical=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('core','reference','catalog','parser','out'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--variant',type=Path,action='append',default=[])
    a=p.parse_args()
    report=verify(a.core,a.reference,a.catalog,a.parser)
    report['group_variants']=[verify_variant(a.core,a.reference,bank,a.parser) for bank in a.variant]
    report['bank_sha256']={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (a.reference,a.catalog)}
    report['bank_sha256'].update({p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in a.variant})
    a.out.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
