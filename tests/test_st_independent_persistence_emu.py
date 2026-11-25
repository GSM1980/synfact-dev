"""Real shared service, native KitActive jobs/codecs, fresh module RAM.

Run with --core <build folder> and one or more --bank <smz4>. The queue,
allocator and mutex are explicit boundaries from the existing fixture.
No complete boot, hardware timing or device IO is simulated.
"""
import argparse,json,struct
from pathlib import Path
from test_st_lfo3_activity_emu import Activity
from test_st_module_startup_emu import DESCRIPTORS
from test_st_comp_emu import KIT,ID

APP,VECTORS,CONTAINER,SINGLE,SCRATCH=0x45500000,0x45000000,0x44c00000,0x45100000,0x45020000
ARRAY=CONTAINER+0x2e0200
LFO=struct.pack('>8H',0x2200,0x500,0x4000,0,0x100,0,0,0x6000)
ARP=struct.pack('>5BxH16b',2,13,2,42,15,0xfffb,*range(1,17))
COMP=[0x1234,0x2345,0x3456,0x4567,0xa00,0x5678,0x7f00]
CHORUS=[0x1234,0x2345,0x3456,0x4567,0x5678,0x6789,0x7000]

class Persistence(Activity):
    def __init__(self,core,bank):
        self.jobs=[]
        super().__init__(core,bank,full_catalog=True)
        self.api=DESCRIPTORS+0xf000;self.w(self.api,3);self.w(self.api+4,self.sym['core:core_machine_ui_api'])
        self.sr=0x2700
        if 'chorus_module' in self.bs:assert self.call('chorus_ui_init',self.api)==0
        if 'comp_module' in self.bs:
            self.call('comp_params_init');assert self.call('comp_ui_init',self.api)==0
        for name,at in [('kitnew',0x400b3384),('kitenc',0x400a78f4),('kitdec',0x400a78a0)]:self.bs[name]=at
        self.bs['kit_changed']=self.word(self.sym['core:core_persistence_api']+16)
        self.sr=0x2000;self.w(0x800030b8,KIT);self.w(0x800030bc,KIT+0xdce)
        self.call('kitnew',KIT)

    def code(self,u,pc,n,a):
        if pc==0x4016ec18:self.ret(SCRATCH+0x1000);return
        if pc==0x400bdc48:
            closure=self.args(4)[2]
            self.jobs.append((self.word(closure+12),bytes(u.mem_read(self.word(closure),12))))
            self.ret(0);return
        if pc in (0x4016dc10,0x40171eda):self.ret(0);return
        super().code(u,pc,n,a)

    def drain(self):
        jobs,self.jobs=self.jobs,[]
        for callback,data in jobs:
            self.u.mem_write(SCRATCH+0x1100,data);self.w(SCRATCH+0x1120,SCRATCH+0x1100)
            assert callback in (0x4000ea98,0x4000eac8)
            self.bs['job']=callback;assert self.call('job',SCRATCH+0x1120)==1
        return len(jobs)

    def mirrors(self):
        # Native application singleton and its KitActive member, as
        # 4016e9c8/4001bb10 construct them. No fixed KitActive address.
        self.w(0x444e23f4,APP);self.w(APP,0x401c5708)
        active=APP+0x130
        self.w(active,0x401c3694);self.w(active+0x10,KIT)
        self.w(active+0x20,VECTORS);self.w(active+0x24,VECTORS+4)
        self.w(active+0x2c,VECTORS+4);self.w(active+0x30,VECTORS+8)
        self.w(VECTORS,ARRAY);self.w(VECTORS+4,SINGLE)

    def values(self):
        out={};read=lambda a,n:bytes(self.u.mem_read(a,n))
        names=[name for name,_,_ in self.modules]
        if 'LFO3' in names:out['LFO3']=read(self.bs['kit_shadow'],16).hex()
        if 'ARP EXPERIMENT' in names:out['ARP']=read(self.call('arp_sound_settings',KIT+0x2a),24).hex()
        if 'PLAYHEAD' in names:out['Playhead']=read(self.bs['playhead_modes'],13).hex()
        if 'comp_module' in self.bs:out['COMP']=[self.call('comp_param_get',ID+i) for i in range(7)]
        if 'chorus_module' in self.bs:out['Chorus']=[self.call('chorus_param_get',self.api,0x403+i) for i in range(7)]
        return out

def verify(core,bank):
    m=Persistence(core,bank)
    names=[name for name,_,_ in m.modules]
    assert names.count('STATE')==1
    enabled='LFO3' in names
    assert m.word(m.bs['lfo3_feature_enabled'])==int(enabled)
    if not enabled:
        # Service-only banks expose neither a third LFO page nor its control
        # metadata, and claim neither of its SRAM reservations.
        assert not m.word(m.lfo['in_sram'])
        m.w(SCRATCH,0x4e0);assert m.call('lfo3_parameter_query',6,SCRATCH,0,SCRATCH+16)==0
        assert m.call('lfo3_page_sync',0)==0
    if 'LFO3' in names:m.u.mem_write(m.bs['kit_shadow'],LFO)
    if 'ARP EXPERIMENT' in names:
        m.u.mem_write(SCRATCH,ARP);assert m.call('arp_sound_store',KIT+0x2a,SCRATCH)==1
    if 'PLAYHEAD' in names:
        m.u.mem_write(SCRATCH,bytes([1])+bytes(12));m.call('playhead_seq_load',0,SCRATCH)
    if 'comp_module' in m.bs:
        for i,v in enumerate(COMP):assert m.call('comp_param_set',ID+i,v)==0
    if 'chorus_module' in m.bs:
        for i,v in enumerate(CHORUS):assert m.call('chorus_param_set',m.api,0x403+i,v)==0
    expected=m.values()
    m.u.mem_write(CONTAINER,bytes(0x3e6604));m.u.mem_write(SINGLE,bytes(0x1600));m.mirrors()
    assert m.call('kit_changed',0)==1
    assert m.drain()==2
    for slot in (ARRAY,SINGLE):
        boot=Persistence(core,bank);boot.mirrors()
        boot.u.mem_write(CONTAINER,bytes(m.u.mem_read(CONTAINER,0x3e6604)))
        boot.u.mem_write(SINGLE,bytes(m.u.mem_read(SINGLE,0x1600)))
        assert boot.call('kitdec',KIT,slot)&255
        assert boot.values()==expected,(bank.name,hex(slot),expected,boot.values())
    # Unknown short exports never acquire tail capacity from metadata tags.
    small=SCRATCH+0x4000;m.u.mem_write(small,b'\xa7'*0x1600)
    assert m.call('kitenc',small,KIT,0)&255
    assert bytes(m.u.mem_read(small+0x14d2,0x12e))==b'\xa7'*0x12e
    return dict(bank=str(bank),modules=names,restored=list(expected),lfo_enabled=enabled,
                mirrors=['array','individual'],fresh_module_ram=True,device_access=False)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core',type=Path,required=True);p.add_argument('--bank',type=Path,action='append',required=True)
    p.add_argument('--out',type=Path);a=p.parse_args()
    rows=[]
    for bank in a.bank:
        row=verify(a.core,bank);rows.append(row);print('PASS independent persistence:',row['modules'],row['restored'],flush=True)
    if a.out:a.out.write_text(json.dumps(dict(ok=True,cases=rows),indent=2)+'\n')
