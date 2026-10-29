"""Real LFO3 callback/renderer: compare the old DDR copy with in-place SRAM.

Only core ownership queries and the engine-busy boundary are doubles. Both
banks run their real event-bank selector, POLY helper and SRAM renderer.
Memory locations are measured; emulator instruction counts are not CPU cycles.
"""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_poly_play_emu import Run,ARGS,WORK,RETURN,BANK,UC_HOOK_CODE,elf
from unicorn import UC_HOOK_MEM_READ
from unicorn.m68k_const import UC_M68K_REG_A7,UC_M68K_REG_D0,UC_M68K_REG_PC

SRAM_CODE,SRAM_BANK=0x80007340,0x8000f870

class Callback(Run):
    def __init__(self,native,path):
        super().__init__(native,path);self.group(True)
        e=elf.Elf.load(path.with_suffix('.work')/'main.elf');groups=[];g=None
        for s in e.symbols:
            if s.type==4:g={};groups.append(g)
            elif g is not None and s.name and s.bind==0:g[s.name]=BANK+s.value
        loc=next(g for g in groups if 'ddr_bank' in g)
        host=ARGS+0x2000;words=[0]*22;words[0]=8;words[21]=RETURN+0x100
        self.u.mem_write(host,struct.pack('>22I',*words));self.write32(loc['host'],host)
        self.write32(self.sym['lfo3_bank'],SRAM_BANK)
        n=self.sym['lfo3_render_end']-self.sym['lfo3_render']
        self.u.mem_write(SRAM_CODE,bytes(self.u.mem_read(self.sym['lfo3_render'],n)))
        self.write32(loc['render'],SRAM_CODE)
        self.params=SRAM_BANK+528
        for t in range(13):
            self.write32(SRAM_BANK+40*t+16,0x3fffffff)
            p=(0x7800,0x300,0x4000,0x2f00,2<<8,0,1<<8,0x6000) if t==0 else (0x7000,0x300,0x4000,0x7f00,0,0,0,0x4000)
            self.u.mem_write(self.params+t*16,struct.pack('>8H',*p))
        if 'poly_copy_pending' in self.sym:
            for t in range(1,4):self.u.mem_write(self.params+16*t,bytes(self.u.mem_read(self.params,16)))
        self.write32(SRAM_BANK+520,0x12345678);self.write32(SRAM_BANK+524,0x9abcdef0)
        self.source=0;self.instructions=self.parameter_ddr_reads=self.parameter_sram_reads=0
        self.u.hook_add(UC_HOOK_CODE,self.code);self.u.hook_add(UC_HOOK_MEM_READ,self.read)
        self.u.ctl_flush_tb()
    def code(self,u,at,size,user):
        self.instructions+=1
        if at==SRAM_CODE:
            sp=u.reg_read(UC_M68K_REG_A7)
            self.source=struct.unpack('>I',bytes(u.mem_read(sp+16,4)))[0]
        if at not in (RETURN+0x100,self.sym['lfo3_engine_busy']):return
        sp=u.reg_read(UC_M68K_REG_A7);ret=struct.unpack('>I',bytes(u.mem_read(sp,4)))[0];v=0
        if at==RETURN+0x100:
            op,arg=struct.unpack('>2I',bytes(u.mem_read(sp+4,8)));assert op==15
            kind=struct.unpack('>I',bytes(u.mem_read(arg,4)))[0] if arg else 0
            v=self.sym['poly_lfo_map'] if kind else self.sym['poly_owner_map']
        u.reg_write(UC_M68K_REG_D0,v);u.reg_write(UC_M68K_REG_A7,sp+4);u.reg_write(UC_M68K_REG_PC,ret)
    def read(self,u,access,at,size,value,user):
        if self.source<=at<self.source+208 and self.source:
            if 0x80000000<=at<0x80010000:self.parameter_sram_reads+=size
            elif BANK<=at<BANK+0x100000:self.parameter_ddr_reads+=size
    def block(self,trigs):
        data=bytearray(0x754+13*124)
        for t in range(13):struct.pack_into('>H',data,0x1c+t*142+0x5e,0x4000)
        self.u.mem_write(WORK,bytes(data));self.u.mem_write(ARGS,struct.pack('>3I',WORK,0x3840,trigs))
        self.instructions=self.parameter_ddr_reads=self.parameter_sram_reads=0;self.source=0
        self.call('lfo3_native',17,ARGS,ARGS+16)
        return bytes(self.u.mem_read(WORK,len(data))),dict(instructions=self.instructions,source=hex(self.source),
            ddr_parameter_bytes=self.parameter_ddr_reads,sram_parameter_bytes=self.parameter_sram_reads)

def main():
    p=argparse.ArgumentParser();p.add_argument('--before',type=Path,required=True);p.add_argument('--after',type=Path,required=True)
    p.add_argument('--core',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    native=a.core.read_bytes();cases={}
    for name in ('neutral','pan'):
        before=Callback(native,a.before);after=Callback(native,a.after)
        if name=='neutral':
            for r in (before,after):
                r.u.mem_write(r.params,struct.pack('>8H',0x7000,0x300,0x4000,0x7f00,0,0,0,0x4000))
                if 'poly_copy_pending' in r.sym:
                    for t in range(1,4):r.u.mem_write(r.params+16*t,bytes(r.u.mem_read(r.params,16)))
        # First entry includes old snapshot setup; compare steady state after it.
        rows=[]
        for block in range(128):
            trigs={0:1,17:2,43:4,71:8}.get(block,0)
            x,old=before.block(trigs);y,new=after.block(trigs);assert x==y,(name,block)
            assert before.source>=BANK and before.source<BANK+0x100000
            assert after.source==after.params
            assert new['ddr_parameter_bytes']==0 and new['sram_parameter_bytes']>0
            if block>0:rows.append(dict(block=block,before=old,after=new))
        cases[name]=dict(before=rows[0]['before'],after=rows[0]['after'])
    result=dict(ok=True,blocks=256,same_output=True,cases=cases,
                device_access=False,measurement='instructions and parameter memory reads, not cache cycles or CPU percent')
    a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
