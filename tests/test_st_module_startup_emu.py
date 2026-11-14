"""Linked module startup after native BSS clearing; no device access.

Copy resident MAIN first, then invalidate the appended boot/load image that
native startup reuses. Execute the actual binder, input reset, list builder
and atomic publisher. Heap and module initializers are explicit boundaries.
The broken 1.8.1 build must fault at its destroyed binder entry.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
from test_st_safe_boot_emu import Core,BASE,SP
from elekloader.sdk import elf
from unicorn.m68k_const import *

BANK,DESCRIPTORS=0x47080000,0x49000000

class Startup(Core):
    def __init__(self,folder,bank,mode):
        self.allocations=[];self.frees=[];self.initialized=[]
        self.init_entries={};self.fail_name=None;self.heap_next=0x47000000
        super().__init__(folder)
        self.u.mem_map(0x47000000,0x200000)
        self.u.mem_map(DESCRIPTORS,0x10000)
        self.folder=Path(folder)
        report=json.loads((self.folder/'verification.json').read_text())
        raw=(self.folder/'main.bin').read_bytes()
        # The documented stock copy sources end at 40347b90; native BSS
        # initialization destroys appended storage after the early copier.
        start=report['link']['boot'][0]
        self.u.mem_write(start,bytes(len(raw)-(start-BASE)))
        self.u.ctl_remove_cache(start,start+len(raw)-(start-BASE))
        self.original=[self.event_list(i) for i in range(15)]
        self.expected=[list(x) for x in self.original]
        self.modules=[];self.count=0
        if mode!='empty':
            raw=bank.read_bytes();size,nrel=struct.unpack_from('>2I',raw,52)
            data=bytearray(raw[80+4*nrel:80+4*nrel+size])
            for off in struct.unpack_from('>%dI'%nrel,raw,80):
                struct.pack_into('>I',data,off,struct.unpack_from('>I',data,off)[0]+BANK)
            self.u.mem_write(BANK,bytes(data))
            e=elf.Elf.load(bank.with_suffix('.work')/'main.elf')
            symbols={x.name:BANK+x.value for x in e.symbols if x.name}
            names=json.loads(bank.with_suffix('.smz4.json').read_text())['modules']
            for index,symbol in enumerate(names):
                row=bytes(self.u.mem_read(symbols[symbol],16))
                name,init,subs,count=struct.unpack('>4I',row)
                label=self.c_string(name)
                self.u.mem_write(DESCRIPTORS+index*16,row)
                subscriptions=[]
                for i in range(count):
                    event,fn=struct.unpack('>2I',bytes(self.u.mem_read(subs+8*i,8)))
                    tagged=fn|(0x80000000 if event&0x100 else 0)|(1 if event&0x200 else 0)
                    self.expected[event&255].append(tagged)
                    subscriptions.append(event)
                self.modules.append((label,init,subscriptions))
                if init!=0xffffffff:self.init_entries[init]=label
            self.count=len(self.modules)
            if mode=='reject':self.fail_name=next(n for n,_,_ in self.modules if n not in ('LFO3','MODULATION SKIP'))
        self.fail_alloc=mode=='no_memory'

    def c_string(self,at):
        text=bytearray()
        for i in range(100):
            b=bytes(self.u.mem_read(at+i,1))
            if b==b'\0':return text.decode('ascii')
            text+=b
        raise AssertionError('unbounded module name')

    def event_list(self,index):
        p=self.word(self.sym['core:events']+8*index)
        result=[]
        for i in range(128):
            word=self.word(p+4*i)
            if not word:return result
            result.append(word)
        raise AssertionError('unbounded event list')

    def code(self,u,pc,size,user):
        if pc==self.sym.get('core:fw_malloc'):
            n=self.args(1)[0]
            if self.fail_alloc:self.ret(0);return
            at=self.heap_next;self.heap_next+=(n+15)&~15
            assert self.heap_next<BANK
            self.allocations.append((at,n));self.ret(at);return
        if pc==self.sym.get('core:fw_free'):
            self.frees.append(self.args(1)[0]);self.ret(0);return
        if pc in self.init_entries:
            api=self.args(1)[0];version,ui=struct.unpack('>2I',bytes(u.mem_read(api,8)))
            assert 1<=version<=3 and ui==self.sym['core:core_machine_ui_api']
            label=self.init_entries[pc];self.initialized.append(label)
            self.ret(1 if label==self.fail_name else 0);return
        super().code(u,pc,size,user)

    def run(self,mode):
        saved=(UC_M68K_REG_D2,UC_M68K_REG_D3,UC_M68K_REG_D4,UC_M68K_REG_D5,UC_M68K_REG_D6,UC_M68K_REG_D7,
               UC_M68K_REG_A2,UC_M68K_REG_A3,UC_M68K_REG_A4,UC_M68K_REG_A5,UC_M68K_REG_A6)
        registers=[0x12340000+i*256 for i in range(len(saved))]
        for reg,value in zip(saved,registers):self.u.reg_write(reg,value)
        value=self.call('core_modules_bind',DESCRIPTORS,self.count)
        assert [self.u.reg_read(r) for r in saved]==registers
        assert self.u.reg_read(UC_M68K_REG_A7)==SP+4
        if mode in ('reject','no_memory'):
            assert value==0
            assert [self.event_list(i) for i in range(15)]==self.original
            if mode=='reject':assert self.frees and self.initialized==[self.fail_name]
            else:assert not self.initialized
        else:
            assert value==1
            assert [self.event_list(i) for i in range(15)]==self.expected
            assert self.word(self.sym['core:core_module_count'])==self.count
            assert self.word(self.sym['core:core_module_subscriptions'])==sum(len(x) for _,_,x in self.modules)
            if mode!='empty':
                ordinary=[n for n,init,_ in self.modules if init!=0xffffffff and n not in ('LFO3','MODULATION SKIP')]
                patchers=[n for n,init,_ in self.modules if init!=0xffffffff and n in ('LFO3','MODULATION SKIP')]
                assert self.initialized==ordinary+patchers
                for i in range(15):
                    ptr=self.word(self.sym['core:events']+8*i)
                    if i!=11:
                        assert ptr==self.word(self.word(self.sym['core:events']+8*i+4))
        return dict(mode=mode,result=value,initializers=self.initialized,allocations=self.allocations,
                    binder=hex(self.sym['core:core_modules_bind']))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('core','bank','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--broken-core',type=Path)
    a=p.parse_args();rows=[]
    for mode in ('empty','full','reject','no_memory'):
        rows.append(Startup(a.core,a.bank,mode).run(mode))
    negative=None
    if a.broken_core:
        m=Startup(a.broken_core,a.bank,'full')
        try:m.run('full')
        except AssertionError as exc:
            pc=m.u.reg_read(UC_M68K_REG_PC)
            assert pc==m.sym['core:core_modules_bind'],str(exc)
            negative=dict(reproduced=True,pc=hex(pc),error=str(exc))
        else:raise AssertionError('broken binder did not fail after native BSS invalidation')
    sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
    result=dict(ok=True,cases=rows,negative_control=negative,core_sha256=sha(a.core/'main.bin'),
                bank_sha256=sha(a.bank),fixture_sha256=sha(Path(__file__)),device_access=False,
                limits=['Native BSS reuse is modeled by clearing appended boot/load storage after resident copying.',
                        'Actual binder, input reset, event-list construction and publisher execute.',
                        'Heap allocation and module initializers are boundaries; this is not complete native boot.'])
    a.out.write_text(json.dumps(result,indent=2)+'\n')
    print('PASS linked startup after native clearing:',len(rows),'cases; negative:',negative)
if __name__=='__main__':main()
