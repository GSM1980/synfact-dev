"""Real LFO3/MSKIP installation after resident copy; isolated modulation blocks.

Native allocator/mutex and the other module initializers are explicit doubles.
The real LFO3 initializer installs SRAM code, lock storage and native hooks;
MSKIP must then install and keep skipping a synthetic zero-depth matrix.
This is not a complete boot, scheduler, DMA or hardware CPU measurement.
"""
import argparse,hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
from test_st_module_startup_emu import Startup,BANK
from elekloader.sdk.elf import Elf,STT_FILE

HEAP=0x49100000
class Patchers(Startup):
    def __init__(self,core,bank):
        self.real={};self.alloc_entry=None;self.release_entry=None
        super().__init__(core,bank,'full')
        self.u.mem_map(HEAP,0x400000);self.u.mem_map(0x80000000,0x10000)
        self.heap_next=HEAP
        self.w(0x402d4fd8,HEAP);self.w(0x402d4fe0,HEAP+0x400000)
        e=Elf.load(bank.with_suffix('.work')/'main.elf')
        self.bs={s.name:BANK+s.value for s in e.symbols if s.name}
        for name,init,_ in self.modules:
            if name in ('LFO3','MODULATION SKIP'):self.real[init]=name
        self.alloc_entry=self.bs.get('lfo3_owned_native_allocate')
        self.release_entry=self.bs.get('lfo3_owned_native_release')
        group=None;self.lfo={}
        for s in e.symbols:
            if s.type==STT_FILE:
                if group and 'ddr_bank' in group:self.lfo=group
                group={}
            elif group is not None and s.name:group[s.name]=BANK+s.value
        if group and 'ddr_bank' in group:self.lfo=group

    def code(self,u,pc,n,user):
        if pc==self.sym['core:fw_malloc'] or pc==self.alloc_entry:
            size=self.args(1)[0];block=16
            while block<size:block*=2
            at=self.heap_next;self.heap_next+=block
            assert self.heap_next<=HEAP+0x400000
            self.allocations.append((at,size));self.ret(at);return
        if pc in (self.release_entry,0x40001980,0x40001aa2):self.ret(0);return
        if pc in self.real:
            self.initialized.append(self.real[pc]);return
        super().code(u,pc,n,user)

def verify(core,bank,refuse=False):
    m=Patchers(core,bank)
    writer=bytes(m.u.mem_read(0x400a30c4,8))
    if refuse:m.u.mem_write(0x40097b04,b'\x4e\x71')
    m.run('full')
    mode=m.word(m.bs['modulation_skip_mode'])
    if m.lfo:
        assert m.word(m.lfo['hooks_installed'])==1
        assert m.word(m.lfo['in_sram'])==1
    if refuse:
        assert mode==3 and bytes(m.u.mem_read(0x400a30c4,8))==writer
        return dict(bank_sha256=hashlib.sha256(bank.read_bytes()).hexdigest(),
                    negative_control='bad modulation preimage refused without partial writer patch',mode=mode)
    assert mode==2,mode
    # Isolate the installed native modulation path. Other native subscriptions
    # are not initialized by this fixture; no POLY replacement is requested.
    m.u.mem_write(m.sym['core:core_native_interest']+16,b'\0')
    m.sym['core:probe_modulation']=0x40097afc
    for _ in range(128):m.call('probe_modulation',0x80006348)
    counts={key:m.word(m.bs['modulation_skip_'+key]) for key in ('mode','blocks','hits','skips','errors')}
    assert counts==dict(mode=2,blocks=128,hits=127,skips=124,errors=0),counts
    return dict(bank_sha256=hashlib.sha256(bank.read_bytes()).hexdigest(),
                initializers=m.initialized,lfo_hooks=bool(m.lfo),**counts)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core',type=Path,required=True)
    p.add_argument('--bank',type=Path,action='append',required=True)
    p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    rows=[verify(a.core,bank) for bank in a.bank]
    negative=verify(a.core,a.bank[-1],True)
    report=dict(ok=True,cases=rows,negative_control=negative,device_access=False,
                core_sha256=hashlib.sha256((a.core/'main.bin').read_bytes()).hexdigest(),
                limits=['Actual LFO3 and MSKIP initializers, resident binder, SRAM copy and zero-depth modulation execute.',
                        'Other initializers, native allocator/mutex and DMA/scheduler are boundaries.',
                        'No complete IRQ, native boot or hardware CPU percentage claim.'])
    a.out.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS real patchers:',len(rows),'banks; 128 blocks each; preimage-refusal negative control')
