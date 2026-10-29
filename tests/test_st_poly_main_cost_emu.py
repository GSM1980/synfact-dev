"""Three-voice MAIN cost attribution through the real core native bus.

Includes native adapters, LFO3 callback, POLY, type queries and extended sender.
Descriptor/heap publication and scheduler/note inputs are fixtures. Counts are
instructions and code regions, never hardware cycles or MAIN CPU percentages.
"""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_poly_lfo3_source_emu import Callback,ARGS,WORK,RETURN,BANK,SRAM_CODE
from test_st_poly_play_emu import fixture,UC_HOOK_CODE,UC_M68K_REG_PC

class Main(Callback):
    def __init__(self,folder,bank,mode,three_fm=False):
        native=(folder/'main.bin').read_bytes();super().__init__(native,bank)
        v=json.loads((folder/'verification.json').read_text());self.core=v['symbols']
        layout=v['link'];lo,hi=layout['ddr'];self.u.mem_map(lo,0x10000)
        self.u.mem_write(lo,native[layout['run_load']-0x40000400:layout['run_load']-0x40000400+hi-lo])
        self.u.mem_map(0x10000000,0x10000);self.u.mem_map(0xfc070000,0x1000)
        self.u.mem_map(0x43000000,0x10000)
        self.u.mem_map(0x43bd0000,0x10000)
        self.copy_on_change='poly_copy_pending' in self.sym
        self.mode=mode;identity=bytes(range(12));owners=bytes([0,0,0,3,4,5,6,7,8,9,10,11])
        shared=mode=='shared';on=mode!='off'
        if 'poly_has_groups' in self.sym:self.u.mem_write(self.sym['poly_has_groups'],bytes([int(on)]))
        self.u.mem_write(self.sym['poly_seq_config']+8,bytes([4 if on else 0])+bytes(7))
        for key,value in [('poly_owner_map',owners if on else identity),('poly_lfo_map',owners if shared else identity),
                          ('poly_post_map',owners if shared else identity),('poly_pre_map',owners if mode=='poly' else identity)]:
            if key in self.sym:self.u.mem_write(self.sym[key],value)
        self.u.mem_write(self.sym['poly_modes'],bytes([int(mode=='poly')])+bytes(7))
        self.write32(self.sym['poly_shared_mask'],6 if shared else 0)
        if 'poly_publish_span' in self.sym:self.write32(self.sym['poly_publish_span'],0x0103 if shared else 0x0808)
        if 'poly_independent_mask' in self.sym:self.write32(self.sym['poly_independent_mask'],6 if mode=='poly' else 0)
        self.write32(0x43bd7430,0x44000000);self.write32(0x43bd743c,0x43000000)
        self.u.mem_write(0x43001550,bytes(2))
        # Native lists are already published, as after a successful bank bind.
        subscribers=[self.sym['lfo3_native'],self.sym['poly_native'],0]
        self.u.mem_write(ARGS+0x4000,struct.pack('>%dI'%len(subscribers),*subscribers))
        self.write32(self.core['core:events']+11*8,ARGS+0x4000)
        interest=bytearray(20);interest[17]=1
        if on:
            for op in (3,15,16):interest[op]=1
            if 'poly_copy_pending' not in self.sym:interest[4]=1
            if mode=='poly':
                interest[3]=0
                if 'poly_copy_pending' in self.sym:interest[15]=interest[16]=0
            interest[18]=6
            if self.copy_on_change:interest[16]=0
        self.u.mem_write(self.core['core:core_native_interest'],bytes(interest))
        # Replace the query stub by the actual core dispatcher for both modules.
        for name in ('host','poly_host'):
            if name=='host':continue
            self.write32(self.sym[name],ARGS+0x2000)
        self.write32(ARGS+0x2000+84,self.core['core:core_native'])
        self.write32(self.core['core:core_machine_active'],1)
        self.write32(self.core['core:directory'],ARGS+0x4100)
        self.write32(ARGS+0x4100+4,self.sym['fm_tone'])
        self.write32(self.core['core:core_machine_shadow'],ARGS+0x4200)
        self.write32(0x43bdc824,1)
        self.u.mem_write(0x80002f50,bytes([65,65 if three_fm or (on and self.copy_on_change) else 1,65 if three_fm or (on and self.copy_on_change) else 1,1,1,1,1,1,8,9,10,22]))
        # Neutral LFO3: CPU rise with no active modulation must also be visible.
        for t in range(13):self.u.mem_write(self.params+16*t,struct.pack('>8H',0x7000,0x300,0x4000,0x7f00,0,0,0,0x4000))
        self.regions={};self.current='setup';self.stages={}
        self.u.hook_add(UC_HOOK_CODE,self.count)
        self.u.ctl_flush_tb()
    def count(self,u,at,size,user):
        key='sram' if 0x80000000<=at<0x80010000 else 'bank_ddr' if BANK<=at<BANK+0x100000 else 'core_ddr' if 0x46000000<=at<0x46010000 else 'stock_rom'
        row=self.stages.setdefault(self.current,{});row[key]=row.get(key,0)+1
    def stage(self,name,fn,*args):
        self.current=name;self.call(fn,*args)
    def run(self,blocks=16):
        data,_=fixture(1,0,0);data=bytearray(data)
        for t in range(13):
            struct.pack_into('>H',data,0x1c+t*142+8*2,0x4000)
            for slot in range(0x36,0x47):
                struct.pack_into('>H',data,0x1c+t*142+2*slot,0x1000+slot*16)
        # Native working data has already been prepared; conversion is common
        # to all modes, before POLY's existing early projection.
        for b in range(blocks):
            self.u.mem_write(WORK,bytes(data));self.stage('modulation',0x40097afc,WORK)
            self.stage('lfo_dispatch_and_lfo3',0x40119266,WORK,0x3840,0)
            if self.mode=='shared':
                self.write32(ARGS,WORK);self.stage('late_publish',self.core['core:core_native'],3,ARGS)
            if self.mode!='off' and 'poly_copy_pending' not in self.sym:
                for t in (1,2):
                    self.write32(ARGS,0x80002f50+t)
                    self.stage('type_queries',self.core['core:core_native'],4,ARGS)
                    self.stage('type_queries',self.core['core:core_native'],4,ARGS)
            self.stage('extended_sender',self.core['core:core_machine_snapshot'],0)
        return dict(stages={k:dict(instructions=sum(v.values())//blocks,regions={r:n//blocks for r,n in v.items()}) for k,v in self.stages.items()},
                    total=sum(sum(v.values()) for v in self.stages.values())//blocks)

def main():
    p=argparse.ArgumentParser();p.add_argument('--folder',required=True,type=Path);p.add_argument('--bank',required=True,type=Path)
    p.add_argument('--previous-bank',type=Path)
    p.add_argument('--out',required=True,type=Path);a=p.parse_args();result={}
    for three in (False,True):
        for mode in ('off','shared','poly'):
            key=('three_fm' if three else 'one_fm')+'_'+mode
            result[key]=Main(a.folder,a.bank,mode,three).run()
    if Main(a.folder,a.bank,'poly',True).copy_on_change:
        assert result['three_fm_off']==result['three_fm_poly'], 'POLY added steady-state block work'
        assert 'type_queries' not in result['three_fm_poly']['stages']
        assert 'late_publish' not in result['three_fm_poly']['stages']
        assert result['three_fm_shared']['total']>result['three_fm_poly']['total']
    comparison=None
    if a.previous_bank:
        previous=Main(a.folder,a.previous_bank,'shared',True).run()
        current=result['three_fm_shared']
        assert current['total']<=previous['total'], 'M.LFO cost regressed'
        previous_off=Main(a.folder,a.previous_bank,'off',True).run()
        assert current['total']-result['three_fm_off']['total']<=previous['total']-previous_off['total'], 'shared increment regressed'
        assert current['stages']['modulation']==previous['stages']['modulation']
        # Analog support adds a bounded dispatch test even when off. Native
        # ROM and LFO3 SRAM work must stay exact; current OFF/POLY must agree.
        for region in ('stock_rom','core_ddr','sram'):
            assert current['stages']['lfo_dispatch_and_lfo3']['regions'][region]==previous['stages']['lfo_dispatch_and_lfo3']['regions'][region]
        assert current['stages']['lfo_dispatch_and_lfo3']==result['three_fm_off']['stages']['lfo_dispatch_and_lfo3']
        assert current['stages']['extended_sender']['instructions']<=previous['stages']['extended_sender']['instructions']
        comparison=dict(previous=previous,current=current)
    a.out.write_text(json.dumps(dict(cases=result,comparison=comparison,device_access=False,units='guest instructions, not CPU percent'),indent=2)+'\n')
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
