"""The linked core's safe-boot record writes, executed in Unicorn.

Core 1.7 crashed on the Syntakt at its first trial boot: machine-boot.c's
eMMC accessor table had no write function, so marking the candidate TRYING
called address 0 (EXCEPTION, vector 3), on every boot. This runs the linked
core's own code on that path: core_mods_transition_io with each accessor
table the core links (`storage` for the boot, `io` for /mods-stage and the
health confirmation), over a simulated eMMC behind the stock read/write entry
points (fw_mmc_read 4010f44e, fw_mmc_write 4010f634). Only the card is a
double. A table that calls anything but those two fails the run.

Usage: test_st_safe_boot_emu.py <core build folder>   (needs LIBUNICORN_PATH)
"""
import json,struct,sys,zlib
from pathlib import Path
from unicorn import Uc,UcError,UC_ARCH_M68K,UC_MODE_BIG_ENDIAN,UC_HOOK_CODE
from unicorn.m68k_const import UC_CPU_M68K_CFV4E,UC_M68K_REG_A7,UC_M68K_REG_D0,UC_M68K_REG_PC,UC_M68K_REG_SR
BASE,SP,RET=0x40000400,0x48010000,0x4f000000
MODS_BASE,SLOT_BYTES=0x02400000,0x00800000
GOOD,CANDIDATE,TRYING=1,2,3
def meta_lba(slot):return (MODS_BASE+(slot+1)*SLOT_BYTES-512)>>9

class Core:
    def __init__(self,folder):
        folder=Path(folder);r=json.loads((folder/'verification.json').read_text())
        self.sym=r['symbols'];layout=r['link']
        u=self.u=Uc(UC_ARCH_M68K,UC_MODE_BIG_ENDIAN);u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        for a,n in [(0x40000000,0x6000000),(0x46000000,0x10000),(SP-0x10000,0x20000),(RET,0x1000)]:u.mem_map(a,n)
        raw=(folder/'main.bin').read_bytes();u.mem_write(BASE,raw)
        lo,hi=layout['ddr'];at=layout['run_load']-BASE;u.mem_write(lo,raw[at:at+hi-lo])
        u.mem_write(RET,b'\x4e\x71')
        self.read_entry,self.write_entry=self.sym['core:fw_mmc_read'],self.sym['core:fw_mmc_write']
        assert (self.read_entry,self.write_entry)==(0x4010f44e,0x4010f634)
        # The card is ready and large enough for the bank slots and legacy record.
        self.w(self.sym['core:fw_mmc_ready'],1);self.w(self.sym['core:fw_mmc_sectors'],0x400000)
        self.card={};self.writes=[];self.reads=0
        u.hook_add(UC_HOOK_CODE,self.code)
    def w(self,a,v):self.u.mem_write(a,struct.pack('>I',v&0xffffffff))
    def word(self,a):return struct.unpack('>I',bytes(self.u.mem_read(a,4)))[0]
    def args(self,n):return struct.unpack('>%dI'%n,bytes(self.u.mem_read(self.u.reg_read(UC_M68K_REG_A7)+4,4*n)))
    def ret(self,v):
        sp=self.u.reg_read(UC_M68K_REG_A7);pc=self.word(sp)
        self.u.reg_write(UC_M68K_REG_D0,v);self.u.reg_write(UC_M68K_REG_A7,sp+4);self.u.reg_write(UC_M68K_REG_PC,pc)
    def code(self,u,pc,n,_):
        if pc==self.read_entry:
            lba,count,out=self.args(3);self.reads+=1
            data=b''.join(self.card.get(lba+i,bytes(512)) for i in range((count+511)//512))
            u.mem_write(out,data[:count]);self.ret(0)
        elif pc==self.write_entry:
            lba,count,src=self.args(3);assert count==512,count
            self.card[lba]=bytes(u.mem_read(src,512));self.writes.append(lba);self.ret(0)
    def call(self,name,*args):
        u=self.u;u.reg_write(UC_M68K_REG_SR,0x2700);u.reg_write(UC_M68K_REG_A7,SP)
        u.mem_write(SP,struct.pack('>%dI'%(1+len(args)),RET,*args))
        try:u.emu_start(self.sym['core:'+name],RET,count=2000000)
        except UcError as e:
            raise AssertionError(f'{name} faulted at PC {u.reg_read(UC_M68K_REG_PC):#010x}: {e}') from None
        assert u.reg_read(UC_M68K_REG_PC)==RET,(name,hex(u.reg_read(UC_M68K_REG_PC)))
        return u.reg_read(UC_M68K_REG_D0)
    def record(self,slot,state,sequence=7,size=0x1234,stamp=0x5ca1ab1e):
        b=bytearray(512);struct.pack_into('>7I',b,0,0x454d4232,2,sequence,state,size,~size&0xffffffff,stamp)
        struct.pack_into('>I',b,32,zlib.crc32(bytes(b[:32])));self.card[meta_lba(slot)]=bytes(b)
    def state(self,slot):
        b=self.card.get(meta_lba(slot),bytes(512))
        if struct.unpack_from('>I',b,32)[0]!=zlib.crc32(b[:32]):return 0
        return struct.unpack_from('>I',b,12)[0]

def verify(folder):
    c=Core(folder);cases=0
    for table in ('storage','io'):
        read,write,sectors=struct.unpack('>3I',bytes(c.u.mem_read(c.sym['core:'+table],12)))
        assert (read,write)==(c.read_entry,c.write_entry),(table,hex(read),hex(write))
        for slot in (0,1):
            c.card.clear();c.writes.clear();c.record(slot,CANDIDATE)
            # The boot's step (machine-boot.c), then the health confirmation (files.c).
            assert c.call('core_mods_transition_io',c.sym['core:'+table],slot,CANDIDATE,TRYING)==1,(table,slot)
            assert c.state(slot)==TRYING and c.writes==[meta_lba(slot)]
            assert c.call('core_mods_transition_io',c.sym['core:'+table],slot,TRYING,GOOD)==1
            assert c.state(slot)==GOOD and c.writes==[meta_lba(slot)]*2
            # Only those two transitions exist; a wrong one writes nothing.
            assert c.call('core_mods_transition_io',c.sym['core:'+table],slot,GOOD,TRYING)==0 and len(c.writes)==2
            cases+=1
    print(f'PASS: linked core record writes in Unicorn: boot and stage tables, {cases} trial/confirm cycles on both slots; no call outside fw_mmc_read/fw_mmc_write')

if __name__=='__main__':verify(sys.argv[1])
