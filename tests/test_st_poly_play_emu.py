"""Real compiled POLY modulation/LFO/publication vs stock independent tracks.

Runs ColdFire+EMAC, not an audio recording or full device boot. Native note
and controller inputs are explicit fixtures. Includes both native LFOs and
the unchanged LFO3 renderer; C tests separately cover LFO3 owner projection.
"""
import argparse,json,struct,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from elekloader import formats
from elekloader.sdk import elf
from unicorn import Uc,UC_ARCH_M68K,UC_MODE_BIG_ENDIAN,UC_HOOK_CODE
from unicorn.m68k_const import *

BASE,BANK,STACK,RETURN,ARGS,WORK=0x40000400,0x47000000,0x4801f000,0x4f000000,0x48010000,0x80006348
SIZE=0x754+13*124
SAVED=[UC_M68K_REG_D2,UC_M68K_REG_D3,UC_M68K_REG_D4,UC_M68K_REG_D5,UC_M68K_REG_D6,UC_M68K_REG_D7,
       UC_M68K_REG_A2,UC_M68K_REG_A3,UC_M68K_REG_A4,UC_M68K_REG_A5,UC_M68K_REG_A6]

class Run:
    def __init__(self,native,bank):
        self.u=u=Uc(UC_ARCH_M68K,UC_MODE_BIG_ENDIAN);u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        u.reg_write(UC_M68K_REG_SR,0x2700)
        for at,n in [(0x40000000,0x400000),(0x44000000,0x1000000),(BANK,0x100000),
                     (0x48000000,0x20000),(RETURN,0x1000),(0x80000000,0x10000)]:u.mem_map(at,n)
        u.mem_write(BASE,native)
        raw=bank.read_bytes();n,nrel=struct.unpack_from('>2I',raw,52)
        data=bytearray(raw[80+4*nrel:80+4*nrel+n])
        for off in struct.unpack_from('>%dI'%nrel,raw,80):
            struct.pack_into('>I',data,off,struct.unpack_from('>I',data,off)[0]+BANK)
        u.mem_write(BANK,bytes(data))
        e=elf.Elf.load(bank.with_suffix('.work')/'main.elf')
        self.sym={s.name:BANK+s.value for s in e.symbols if s.name}
        # Same MACSR as the native MAIN IRQ; real EMAC is mandatory.
        u.emu_start(0x400a38c2,0x400a38c8,count=10)
        self.call(0x40118e24)
        for t in range(13):
            self.write32(0x800046b8+4*t,60<<16)
            u.mem_write(0x80004670+2*t,struct.pack('>H',0x7f00))
        self.params=ARGS+0x100;self.third=ARGS+0x1000
        for t in range(13):self.write32(self.third+40*t+16,0x3fffffff)
        self.write32(self.third+520,0x12345678);self.write32(self.third+524,0x9abcdef0)
    def write32(self,at,v):self.u.mem_write(at,struct.pack('>I',v))
    def call(self,fn,*args):
        u=self.u;sp=STACK-4*(len(args)+1)
        for i,r in enumerate(SAVED):u.reg_write(r,0x12340000+i)
        u.mem_write(sp,struct.pack('>%dI'%(1+len(args)),RETURN,*args));u.reg_write(UC_M68K_REG_A7,sp)
        try:u.emu_start(self.sym.get(fn,fn),RETURN,count=300000)
        except Exception as error:
            raise RuntimeError((fn,hex(u.reg_read(UC_M68K_REG_PC)),hex(u.reg_read(UC_M68K_REG_A7)))) from error
        assert u.reg_read(UC_M68K_REG_PC)==RETURN,(fn,hex(u.reg_read(UC_M68K_REG_PC)))
        assert u.reg_read(UC_M68K_REG_A7)==sp+4,fn
        assert [u.reg_read(r) for r in SAVED]==[0x12340000+i for i in range(len(SAVED))],fn
        return u.reg_read(UC_M68K_REG_D0)
    def group(self,independent):
        owners=bytes([0,0,0,0,4,5,6,7,8,9,10,11]);identity=bytes(range(12))
        self.u.mem_write(self.sym['poly_seq_config']+8,bytes([5,0,0,0,0,0,0,0]))
        self.u.mem_write(self.sym['poly_owner_map'],owners)
        self.u.mem_write(self.sym['poly_lfo_map'],identity if independent else owners)
        self.u.mem_write(self.sym['poly_pre_map'],owners if independent else identity)
        self.u.mem_write(self.sym['poly_post_map'],identity if independent else owners)
        self.u.mem_write(self.sym['poly_modes'],bytes([int(independent)])+bytes(7))
        self.write32(self.sym.get('poly_independent_mask',self.sym.get('poly_lfo_saved_mask')),14 if independent else 0)
        if 'poly_shared_mask' in self.sym:self.write32(self.sym['poly_shared_mask'],0 if independent else 14)
    def publish(self):
        self.write32(ARGS,WORK);self.call('poly_native',3,ARGS,ARGS+16)
    def block(self,data,trigs,which,third):
        self.u.mem_write(WORK,data)
        self.call('poly_modulate' if which else 0x40097afc,WORK)
        if third:self.call('lfo3_render',WORK,0x3840,trigs,self.params,self.third,self.third+520)
        if which:
            self.u.mem_write(ARGS,struct.pack('>3I',WORK,0x3840,trigs))
            assert self.call('poly_native',17,ARGS,ARGS+16)
            fn=struct.unpack('>I',bytes(self.u.mem_read(ARGS+16,4)))[0]
            self.call(fn,WORK,0x3840,trigs)
        else:self.call(0x40119266,WORK,0x3840,trigs)
        if which:self.publish()
        return bytes(self.u.mem_read(WORK,SIZE))

def fixture(lfo,mode,wave):
    data=bytearray(SIZE);p=[0x7800,0x300,0x4000,0x2f00,wave<<8,0,mode<<8,0x6000]
    for t in range(13):
        at=0x1c+t*142
        for k in range(2):struct.pack_into('>8H',data,at+2+16*k,*([0x7000,0x300,0x4000,0x7f00,0,0,0,0x4000]))
        struct.pack_into('>H',data,at+2*0x2f,0x4000)
        struct.pack_into('>I',data,0x7b4+t*124,2)
        if t<4 and lfo<3:struct.pack_into('>8H',data,at+2+16*(lfo-1),*p)
    return bytes(data),struct.pack('>8H',*p)

def costs(native,bank,stock):
    counts={};data,_=fixture(1,1,0)
    r=Run(stock,bank);total=[0]
    h=r.u.hook_add(UC_HOOK_CODE,lambda *unused:total.__setitem__(0,total[0]+1))
    r.u.mem_write(WORK,data);r.call(0x40097afc,WORK);r.call(0x40119266,WORK,0x3840,3)
    r.u.hook_del(h);counts['stock_independent']=total[0]
    for independent in (False,True):
        r=Run(native,bank);r.group(independent);total=[0]
        h=r.u.hook_add(UC_HOOK_CODE,lambda *unused:total.__setitem__(0,total[0]+1))
        r.u.mem_write(WORK,data)
        r.call('poly_modulate' if independent else 'poly_mod_shared',WORK)
        r.call('poly_native_all' if independent and 'poly_native_all' in r.sym else 'poly_native_shared',WORK,0x3840,3)
        if not independent or 'poly_shared_mask' not in r.sym:r.publish()
        r.u.hook_del(h);counts['poly' if independent else 'master_lfo']=total[0]
    return counts

def mixed_probe(native,bank,stock):
    ref=Run(stock,bank);actual=Run(native,bank);actual.group(True)
    owners=bytes([0,0,2,2,4,5,6,7,8,9,10,11]);post=bytes([0,1,2,2,4,5,6,7,8,9,10,11])
    actual.u.mem_write(actual.sym['poly_seq_config']+8,bytes([3,0,3,0,0,0,0,0]))
    for key,value in [('poly_owner_map',owners),('poly_lfo_map',post),
                      ('poly_pre_map',bytes([0,0,2,3,4,5,6,7,8,9,10,11])),('poly_post_map',post)]:
        actual.u.mem_write(actual.sym[key],value)
    actual.u.mem_write(actual.sym['poly_modes'],bytes([1,0,0,0,0,0,0,0]))
    actual.write32(actual.sym['poly_independent_mask'],2);actual.write32(actual.sym['poly_shared_mask'],8)
    ref.u.mem_write(ref.sym['poly_post_map'],post);data,_=fixture(1,1,0)
    for block in range(80):
        trigs={0:1,11:2,29:4,43:8}.get(block,0)
        ref.block(data,trigs,False,False);ref.call('poly_fast_publish',WORK,ref.sym['poly_post_map'])
        expected=bytes(ref.u.mem_read(WORK,SIZE));poisoned=bytearray(data)
        for t in (1,3):poisoned[0x1c+t*142:0x1c+(t+1)*142]=bytes(142)
        assert actual.block(bytes(poisoned),trigs,True,False)==expected,('mixed',block)
    return 80

def controller_probe(native,bank,stock):
    ref=Run(stock,bank);actual=Run(native,bank);actual.group(True)
    data,_=fixture(1,1,0);data=bytearray(data)
    inputs=(0x800046f8,0x80004712,0x8000472c,0x80004746)
    for t in range(4):
        for j in range(5):struct.pack_into('>I',data,0x754+t*124+16*j,(0x1000<<16)|0x2f)
        for r in (ref,actual):
            r.u.mem_write(0x80004670+2*t,struct.pack('>H',0x1000+t*0x1000))
            r.write32(0x800046b8+4*t,(48+t*7)<<16)
        for j,at in enumerate(inputs):
            ref.u.mem_write(at+2*t,struct.pack('>H',0x700*(j+1)))
            actual.u.mem_write(at+2*t,struct.pack('>H',0x700*(j+1) if not t else 0x3000+t*0x100))
    original=bytearray(actual.u.mem_read(inputs[0],104))
    # Direct inheritance is deliberately retained in the slaves after POLY.
    for at in inputs:
        for t in range(1,4):original[at-inputs[0]+2*t:at-inputs[0]+2*t+2]=original[at-inputs[0]:at-inputs[0]+2]
    for block in range(8):
        expected=ref.block(bytes(data),1<<block if block<4 else 0,False,False)
        poisoned=bytearray(data)
        for t in range(1,4):
            poisoned[0x1c+t*142:0x1c+(t+1)*142]=bytes(142)
            poisoned[0x754+t*124:0x754+(t+1)*124]=bytes(124)
        assert actual.block(bytes(poisoned),1<<block if block<4 else 0,True,False)==expected
        assert bytes(actual.u.mem_read(inputs[0],104))==original
    assert len({struct.unpack_from('>H',expected,0x1c+t*142+0x5e)[0] for t in range(4)})>1
    return 8

def main():
    p=argparse.ArgumentParser();p.add_argument('--bank',required=True,type=Path);p.add_argument('--stock',required=True)
    p.add_argument('--core',type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
    stock=formats.load(a.stock)[0].section(3);images=[('stock',stock)]
    if a.core:images.append(('core',a.core.read_bytes()))
    results=[];negative=0;controllers=0;mixed=0;instructions={}
    for image,native in images:
        print('Checking',image,'controllers',flush=True)
        controllers+=controller_probe(native,a.bank,stock)
        mixed+=mixed_probe(native,a.bank,stock)
        instructions[image]=costs(native,a.bank,stock)
        for lfo in (1,2,3):
            print('Checking',image,'LFO',lfo,flush=True)
            for mode in range(5):
                for wave in (0,1,2,3):
                    ref=Run(stock,a.bank);actual=Run(native,a.bank);actual.group(True)
                    data,params=fixture(lfo,mode,wave)
                    for r in (ref,actual):r.u.mem_write(r.params,params*4+struct.pack('>8H',0x7000,0x300,0x4000,0x7f00,0,0,0,0x4000)*9)
                    different=False
                    for block in range(80):
                        trigs={0:1,11:2,29:4,43:8,64:1}.get(block,0)
                        expected=ref.block(data,trigs,False,lfo==3)
                        # Poison saved slave configs: they must never leak into POLY.
                        poisoned=bytearray(data)
                        for t in range(1,4):poisoned[0x1c+t*142:0x1c+(t+1)*142]=bytes(142)
                        got=actual.block(bytes(poisoned),trigs,True,lfo==3)
                        assert got==expected,(image,lfo,mode,wave,block,next(i for i,(x,y) in enumerate(zip(got,expected)) if x!=y))
                        pans=[struct.unpack_from('>H',got,0x1c+t*142+0x5e)[0] for t in range(4)]
                        different|=len(set(pans))>1
                    if different:
                        # Former late master copy would destroy this independent output.
                        actual.call('poly_fast_publish',WORK,actual.sym['poly_owner_map'])
                        assert bytes(actual.u.mem_read(WORK,SIZE))!=expected;negative+=1
                    results.append(dict(image=image,lfo=lfo,mode=mode,wave=wave,different_pan=different))
    assert negative>0
    a.out.write_text(json.dumps(dict(ok=True,cases=len(results),blocks=len(results)*80,negative_controls=negative,
                                   pipeline_instructions=instructions,
                                   controller_blocks=controllers,
                                   mixed_blocks=mixed,
                                   fixtures=results,device_access=False),indent=2)+'\n')
    print('PASS:',len(results),'native differential cases;',len(results)*80,'blocks;',negative,'late-copy negative controls')
if __name__=='__main__':main()
