"""Recorder/KIT BANK preferences through the real core sector transaction.

Only card IO and native mutexes are doubles. Each restore starts a new core
and bank instance; native module initializers execute. No physical device.
"""
import argparse,struct
from pathlib import Path
from test_st_chorus_ui_emu import Machine,W
from unicorn.m68k_const import UC_M68K_REG_A7

FIELDS=['sam_source','sam_rlen','sam_thr','sam_mon','sam_post','sam_stereo']
VALUES=[2,6,72,1,1,1]

class Preferences(Machine):
    def __init__(self,core,bank,card=None):
        self.card={} if card is None else dict(card);self.writes=[]
        super().__init__(core/'main.bin',bank)
        self.w(self.cs['core:fw_mmc_ready'],1);self.w(self.cs['core:fw_mmc_sectors'],0x400000)
        self.w(W,3);self.w(W+4,self.cs['core:core_machine_ui_api'])

    def code(self,u,pc,n,a):
        if pc in (0x40001980,0x40001aa2):self.ret(0);return
        if pc in (self.cs.get('core:fw_mmc_read'),self.cs.get('core:fw_mmc_write')):
            sp=u.reg_read(UC_M68K_REG_A7)
            lba,size,ptr=struct.unpack('>3I',self.read(sp+4,12))
            # No saved bank project in this card double. Its directory reads
            # may fail; the independent 512-byte preferences still commit.
            if size!=512:
                assert pc==self.cs['core:fw_mmc_read']
                self.ret(-1);return
            if pc==self.cs['core:fw_mmc_read']:u.mem_write(ptr,self.card.get(lba,b'\xff'*512))
            else:self.card[lba]=self.read(ptr,512);self.writes.append(lba)
            self.ret(0);return
        super().code(u,pc,n,a)

    def initialize(self,name):
        assert self.call(self.word(self.sym[name]+4),W,sr=0x2000)==0

def verify(core,bank):
    m=Preferences(core,bank)
    recorder='recorder_module' in m.sym;kitbank='kitload_module' in m.sym
    assert recorder or kitbank
    if recorder:
        m.initialize('recorder_module')
        for name,value in zip(FIELDS,VALUES):m.w(m.sym[name],value)
        m.call('recorder_preferences',1,sr=0x2000)
    if kitbank:
        m.initialize('kitload_module');m.w(m.sym['kl_pick'],37)
        m.call('kl_bank_select',0,sr=0x2000)
        assert m.word(m.sym['kl_bank'])==37
    assert m.writes and set(m.writes)<=set((x-512)>>9 for x in (0x02bffe00,0x033ffe00))
    boot=Preferences(core,bank,m.card)
    if recorder:
        boot.initialize('recorder_module')
        assert [boot.word(boot.sym[name]) for name in FIELDS]==VALUES
    if kitbank:
        boot.initialize('kitload_module')
        assert [boot.word(boot.sym[name]) for name in ('kl_user','kl_bank','kl_pick')]==[1,37,37]
        boot.call('kl_bank_select',0,sr=0x2000)
        off=Preferences(core,bank,boot.card);off.initialize('kitload_module')
        assert [off.word(off.sym[name]) for name in ('kl_user','kl_bank','kl_pick')]==[1,0,37]
    print('PASS module preferences:',bank.parent.name,'Recorder' if recorder else '', 'KIT BANK (including OFF)' if kitbank else '',flush=True)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--core',type=Path,required=True)
    p.add_argument('--bank',type=Path,action='append',required=True);a=p.parse_args()
    for bank in a.bank:verify(a.core,bank)
