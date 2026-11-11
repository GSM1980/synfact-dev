"""Run native destination enumeration with the real core, bank and Sound proxy.

Only boot-created objects, heap allocation/free and the empty ordering-map
header are fixtures. Model getters, enumeration, sorting, core metadata and
Digi Mono's provider execute actual ColdFire code. No device access.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from elekloader.sdk.elf import Elf
from test_st_safe_boot_emu import Core,SP,RET
from unicorn import UC_HOOK_CODE
from unicorn.m68k_const import UC_M68K_REG_A0,UC_M68K_REG_A5,UC_M68K_REG_A7,UC_M68K_REG_D0,UC_M68K_REG_PC,UC_M68K_REG_SR

BANK=0x46100000
DATA=0x49000000
PROXY,MODEL,SOUND,API,OUT=DATA,DATA+0x1000,DATA+0x2000,DATA+0x3000,DATA+0x4000
ENGINE=0x1f9+(71-64)*32
ACTIVE=((0,),(0,1,2,3),(0,1,2,3,5,6,7),tuple(range(8)),tuple(range(8)),tuple(range(8)),tuple(range(6)),tuple(range(8)))+(tuple(range(8)),)*4


class Destination(Core):
    def __init__(self,folder):
        super().__init__(folder)
        self.u.mem_map(BANK,0x200000)
        self.u.mem_map(0x47000000,0x800000)
        self.u.mem_map(DATA,0x10000)
        self.heap=0x47000000
        self.u.hook_add(UC_HOOK_CODE,self.environment)
        bank=Path(folder)/'synfact-machines.smz4';raw=bank.read_bytes()
        report=json.loads(Path(str(bank)+'.json').read_text())
        assert hashlib.sha256(raw).hexdigest()==report['sha256']
        size,nrel=struct.unpack_from('>II',raw,52)
        image=bytearray(raw[80+4*nrel:80+4*nrel+size])
        for off in struct.unpack_from('>'+str(nrel)+'I',raw,80):
            struct.pack_into('>I',image,off,struct.unpack_from('>I',image,off)[0]+BANK)
        self.u.mem_write(BANK,bytes(image))
        elf=Elf.load(bank.with_suffix('.work')/'main.elf');section=elf.section('.image')
        self.bank_symbols={s.name:s.value+BANK for s in elf.symbols if s.name and s.shndx==section.idx}
        self.w(self.sym['core:core_machine_ui_definitions'],BANK+struct.unpack_from('>I',raw,28)[0])
        directory,count=struct.unpack_from('>II',raw,40)
        assert self.invoke(self.sym['core:core_machine_bind'],BANK+directory,count)==1
        self.u.mem_write(API,struct.pack('>2I',1,self.sym['core:core_machine_ui_api']))
        module=self.bank_symbols['dm_destination_module']
        assert self.invoke(self.word(module+4),API)==0
        assert self.word(self.sym['core:core_parameter_providers'])==self.bank_symbols['dm_destination_query']
        self.w(PROXY,0x401c3aac);self.w(PROXY+0x10,MODEL)
        self.w(MODEL,0x401c61f4);self.w(MODEL+0x10,SOUND)
        self.u.mem_write(SOUND+0xa2,bytes([71]))
        # Native C++ destination-order map: boot's empty header, then allow
        # its real 63-entry population and additional owners to execute.
        self.u.mem_write(0x405fd330,bytes(24))
        self.w(0x405fd33c,0x405fd334);self.w(0x405fd340,0x405fd334)
        self.u.mem_write(0x405fd348,b'\x01')
        self.ui_calls=0

    def environment(self,u,pc,size,data):
        if pc in (0x4010350c,0x40103788):
            count=self.args(1)[0];pointer=self.heap
            self.heap+=(count+31)&~15
            assert self.heap<0x47800000
            self.ret(pointer)
        elif pc in (0x40103712,0x40103794):self.ret(0)
        elif hasattr(self,'bank_symbols') and pc==self.bank_symbols.get('dm_destination_query'):
            q,args,model,value=self.args(4)
            if q==32:
                assert model==PROXY and value and ENGINE<=self.word(args)<=ENGINE+8
                self.ui_calls+=1

    def invoke(self,entry,*args,a0=0,a5=0):
        self.u.reg_write(UC_M68K_REG_SR,0x2700)
        self.u.reg_write(UC_M68K_REG_A7,SP)
        self.u.reg_write(UC_M68K_REG_A0,a0);self.u.reg_write(UC_M68K_REG_A5,a5)
        self.u.mem_write(SP,struct.pack('>'+str(1+len(args))+'I',RET,*(arg&0xffffffff for arg in args)))
        self.u.emu_start(entry,RET,count=3000000)
        assert self.u.reg_read(UC_M68K_REG_PC)==RET,(hex(entry),hex(self.u.reg_read(UC_M68K_REG_PC)))
        assert self.u.reg_read(UC_M68K_REG_A7)==SP+4
        assert self.u.reg_read(UC_M68K_REG_A5)==a5,'callee-save A5 corrupted'
        return self.u.reg_read(UC_M68K_REG_D0)

    def targets(self,kind,mask):
        self.u.mem_write(SOUND+0x80,struct.pack('>H',kind<<8))
        saved=bytes(self.u.mem_read(SOUND,172));before=self.ui_calls
        self.invoke(0x4001471c,PROXY,mask,a0=OUT)
        assert bytes(self.u.mem_read(SOUND,172))==saved,'enumeration modified Sound'
        assert self.ui_calls>before
        begin,end,capacity=struct.unpack('>3I',self.u.mem_read(self.word(OUT),12))
        assert begin<=end<=capacity and not((end-begin)&3)
        return list(struct.unpack('>'+str((end-begin)//4)+'I',self.u.mem_read(begin,end-begin)))


def verify(folder):
    machine=Destination(folder);common=None;cases=0
    for kind in (7,0,6,1,3,2,4,5,0,7,8,9,10,11,0,8):
        for mask in (0x600,0xe00,0x200):
            ids=machine.targets(kind,mask)
            selected=[i-ENGINE for i in ids if ENGINE<=i<=ENGINE+8]
            assert sorted(selected)==list(ACTIVE[kind])+[8],(kind,hex(mask),selected)
            assert len(ids)==len(set(ids))
            # The native relative selector must skip hidden controls too;
            # SIN moves directly from TUNE to TYPE, rather than blank rows.
            ordered=[i for i in ids if ENGINE<=i<=ENGINE+8]
            saved=bytes(machine.u.mem_read(SOUND,172))
            for first,second in zip(ordered,ordered[1:]):
                a=machine.invoke(0x400a5c4a,first)<<8
                b=machine.invoke(0x400a5c4a,second)<<8
                assert machine.invoke(0x40014b90,PROXY,a,0x100,mask)==b
                assert machine.invoke(0x40014b90,PROXY,b,-0x100,mask)==a
            assert bytes(machine.u.mem_read(SOUND,172))==saved
            other=[i for i in ids if not ENGINE<=i<=ENGINE+8]
            if mask==0x600:
                if common is None:common=other
                else:assert common==other,'non-machine targets changed with TYPE'
            for index in range(9):
                assert machine.invoke(0x400a666a,ENGINE+index,a5=PROXY)==0xe00,'runtime flags filtered'
            cases+=1
    print('PASS:',cases,'native destination lists and relative selectors: all 12 TYPEs, LFO1/2/3 masks, no blanks, unchanged common targets/Sound/runtime flags; real model getter, sorting, core and bank')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path)
    verify(p.parse_args().folder)
