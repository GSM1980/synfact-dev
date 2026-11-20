"""Linked manager initializer and core UI-provider lifecycle, no device IO."""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_poly_play_emu import Run,ARGS,BANK
from unicorn import Uc,UC_ARCH_M68K,UC_MODE_BIG_ENDIAN
from unicorn.m68k_const import UC_CPU_M68K_CFV4E,UC_M68K_REG_SR

def main():
    p=argparse.ArgumentParser();p.add_argument('folder',type=Path);a=p.parse_args();f=a.folder
    raw=(f/'synfact-machines.smz4').read_bytes();report=json.loads((f/'synfact-machines.smz4.json').read_text())
    v=json.loads((f/'verification.json').read_text());native=(f/'main.bin').read_bytes()
    # This initializer needs no audio bootstrap. Run's POLY constructor enters
    # an OS 1.41 MAC site; map only this test's OS 1.42 MAIN/core/bank instead.
    r=Run.__new__(Run);r.sym={};r.u=Uc(UC_ARCH_M68K,UC_MODE_BIG_ENDIAN)
    r.u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E);r.u.reg_write(UC_M68K_REG_SR,0x2700)
    for start,size in ((0x40000000,0x400000),(0x44000000,0x1000000),(BANK,0x100000),
                       (0x48000000,0x20000),(0x4f000000,0x1000)):
        r.u.mem_map(start,size)
    r.u.mem_write(0x40000400,native)
    n,nrel=struct.unpack_from('>2I',raw,52);data=bytearray(raw[80+4*nrel:80+4*nrel+n])
    for offset in struct.unpack_from('>%dI'%nrel,raw,80):
        struct.pack_into('>I',data,offset,struct.unpack_from('>I',data,offset)[0]+BANK)
    r.u.mem_write(BANK,bytes(data))
    lo,hi=v['link']['ddr'];r.u.mem_map(lo,0x10000)
    at=v['link']['run_load']-0x40000400;r.u.mem_write(lo,native[at:at+hi-lo])
    sym=v['symbols'];pointer=sym['core:pool_ui'];host=sym['core:core_machine_host_api']
    def word(at):return struct.unpack('>I',bytes(r.u.mem_read(at,4)))[0]
    image=80+4*struct.unpack_from('>I',raw,56)[0]
    directory,count=struct.unpack_from('>2I',raw,32);idx=report['modules'].index('sample_manager_module')
    name,init,subs,n=struct.unpack_from('>4I',raw,image+directory+16*idx)
    assert raw[image+name:raw.index(0,image+name)]==b'Sample Manager' and n==2
    events=[struct.unpack_from('>2I',raw,image+subs+8*i)[0] for i in range(n)]
    assert events==[2,0],events
    assert word(host)>=11 and word(host+88)==sym['core:core_sample_pool_api']
    assert word(pointer)==0
    # API 10 advertises ARP support but has no appended pool pointer.
    # Borrow the real native UI/host table; no OS view is constructed at init.
    r.u.mem_write(ARGS,struct.pack('>2I',1,sym['core:core_machine_ui_api']))
    r.write32(host,10);assert r.call(BANK+init,ARGS)!=0 and word(pointer)==0
    r.write32(host,11)
    pool=word(host+88);assert word(pool)==2
    r.write32(pool,1);assert r.call(BANK+init,ARGS)!=0 and word(pointer)==0
    r.write32(pool,2);memory=word(pool+32);r.write32(pool+32,0)
    assert r.call(BANK+init,ARGS)!=0 and word(pointer)==0
    r.write32(pool+32,memory);assert r.call(BANK+init,ARGS)==0
    published=word(pointer);assert BANK<=published<BANK+report['main_bytes']
    assert r.call(BANK+init,ARGS)!=0 and word(pointer)==published
    r.call(sym['core:core_pool_ui_reset']);assert word(pointer)==0
    assert r.call(BANK+init,ARGS)==0
    r.call(sym['core:core_pool_ui_reset']);assert word(pointer)==0
    for name in ('core_hzfs_open','core_hzfs_get','core_pool_read','core_pool_sync','manager_preview','st_pool_preview_mix','core_pool_ui_reset'):
        assert lo<=sym['core:'+name]<hi,('infrastructure outside core',name)
    for name in ('st_pool_ui_open','st_pool_item_label','st_pool_ui_draw'):
        assert 'core:'+name not in sym,('UI still resident',name)
    print('PASS: linked optional manager uses host 11 / pool API 2; old-host/pool and missing-memory refusal, one provider, rollback/reset and rebind; all infrastructure in core')
if __name__=='__main__':main()
