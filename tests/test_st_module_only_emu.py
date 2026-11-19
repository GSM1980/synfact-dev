"""Module-only registry and real LFO3 metadata on linked OS 1.42 MAIN.

Runs the real empty-registry bind, module binder and LFO3/ARP initialization.
Native label/cache/ID hooks, range queries and LFO3 record writes execute.
Allocation, mutex, other module initializers and the proxy getter are explicit
boundaries inherited from Patchers. Not a full boot, renderer or physical UI.
"""
import argparse
import json
from pathlib import Path
import struct

from test_st_patchers_emu import Patchers
from test_st_safe_boot_emu import Core
from unicorn.m68k_const import UC_M68K_REG_A0

DATA,PROXY,GETTER,KIT=0x45020000,0x45000000,0x45010000,0x41ad6134


class ModuleOnly(Patchers):
    def __init__(self,core,bank):
        self.fail_registry=False
        super().__init__(core,bank)
        for name,init,_ in self.modules:
            if name=='ARP EXPERIMENT':self.real[init]=name

    def code(self,u,pc,n,user):
        if pc==self.sym['core:fw_malloc'] and self.fail_registry:
            self.ret(0);return
        if pc==GETTER:
            self.ret(KIT+0x2a);return
        super().code(u,pc,n,user)

    def invoke(self,fn,*args):
        self.sym['core:module_only_probe']=self.bs.get(fn,self.sym.get('core:'+str(fn),fn))
        return self.call('module_only_probe',*args)

    def text(self,at):
        return self.c_string(at)

    def bind(self):
        assert not self.word(self.sym['core:core_machine_active'])
        assert self.call('core_machine_bind',0,0)==1
        self.run('full')
        assert self.word(self.lfo['hooks_installed'])==1
        providers=self.sym['core:core_parameter_providers']
        assert self.bs['lfo3_parameter_query'] in [self.word(providers+4*i) for i in range(4)]


def verify(core,bank,reference=None):
    # The file itself has no machine descriptors, including banks that carry
    # the always-included optimization modules in addition to LFO3/ARP.
    assert struct.unpack_from('>I',bank.read_bytes(),44)[0]==0
    m=ModuleOnly(core,bank)
    no_bank=Core(core)
    for pid in (0x60,0x61,0x68,0x3f9):
        assert m.invoke(0x40012b24,0,pid)==no_bank.call('core_machine_parameter_label',0,pid)
    m.bind()
    assert m.word(m.sym['core:core_machine_active'])==1
    assert m.word(m.sym['core:core_machine_count'])==0
    for machine in range(64,80):
        assert m.call('core_machine_present',machine)==0
        for track in (0,4,8,12):
            assert m.call('core_machine_availability',machine,track)==no_bank.call('core_machine_availability',machine,track)
    labels=[];edit_values=[]
    for i in range(10):
        pid,native=0x3f9+i,0x60+i
        short=m.text(m.invoke(0x40012b24,0,pid))
        assert short==m.text(m.invoke(0x40012b24,0,native)) and short!='ERR'
        assert m.text(m.invoke(0x40011a82,0,pid)) not in ('','ERR')
        assert m.invoke(0x400a63e2,pid)==0x4e0+i
        assert m.invoke(0x400a63c6,0x4e0+i)==pid
        assert m.invoke(0x4007ba2a,pid)==m.invoke(0x4007ba2a,native)
        m.w(DATA,pid)
        assert m.call('core_machine_query',13,DATA,DATA+0x20,DATA+0x30)==1
        actual=bytes(m.u.mem_read(DATA+0x20,12))
        low,high,initial=struct.unpack('>3I',actual)
        edit_values.append(low if initial!=low else high)
        # Compare the module's range with the original LFO2 descriptor,
        # resolved by the same native range entry through the core.
        m.u.reg_write(UC_M68K_REG_A0,DATA+0x40)
        assert m.invoke('core_machine_range_copy',native)==DATA+0x40
        assert actual==bytes(m.u.mem_read(DATA+0x40,12))
        labels.append(short)
    # Real Sound proxy -> Kit shadow record. Only its native getter is doubled.
    for at,value in ((PROXY,PROXY+0x100),(PROXY+0x100+0x28,0x40012986),
                     (PROXY+0x10,PROXY+0x200),(PROXY+0x200,PROXY+0x300),
                     (PROXY+0x300+0x28,GETTER)):
        m.w(at,value)
    edits=0
    for i,value in enumerate(edit_values):
        assert m.invoke('lfo3_actions_store',PROXY,0x3f9+i,value,0,0,0)==1
        assert m.invoke('lfo3_actions_get',PROXY,0x3f9+i)==value
        edits+=1
    # Allocation failure closes the query gate rather than activating a
    # directory with no backing storage. Native startup with no bank also
    # remains inactive; the host registry suite covers the recovery path.
    failed=ModuleOnly(core,bank);failed.fail_registry=True
    assert failed.call('core_machine_bind',0,0)==0
    assert failed.word(failed.sym['core:core_machine_active'])==0
    negative=None
    if reference:
        broken=ModuleOnly(reference,bank);broken.bind()
        assert broken.word(broken.sym['core:core_machine_active'])==0
        error=broken.text(broken.invoke(0x40012b24,0,0x3f9))
        assert error=='ERR',error
        negative=dict(reproduced=True,label=error,provider_registered=True)
    return dict(bank=bank.name,labels=labels,record_edits=edits,
                real_initializers=[name for name in m.initialized if name in ('LFO3','ARP EXPERIMENT')],
                old_core_negative_control=negative)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core',required=True,type=Path)
    p.add_argument('--bank',required=True,action='append',type=Path)
    p.add_argument('--reference-core',type=Path)
    p.add_argument('--out',required=True,type=Path)
    a=p.parse_args()
    rows=[verify(a.core,bank,a.reference_core) for bank in a.bank]
    a.out.write_text(json.dumps(dict(ok=True,cases=rows,device_access=False),indent=2)+'\n')
    print('PASS: real module-only registry, LFO3/ARP init, labels/cache/ID/range queries, record edits, no-memory and old-core ERR negative controls')
