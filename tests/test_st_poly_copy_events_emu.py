"""Production destructive Sound copies and shared native p-lock application.

Uses the real native Sound memcpy/LFO3 metadata backend and publication engine.
Model/mutex/allocator boundaries come from the existing publication fixture.
The lock probe executes stock 400978b4 with one object applied to three tracks;
its LFO3 wrapper supplies the same captured sparse locks to each destination.
"""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_lfo3_publication_emu import Runtime,LIVE,ENGINE,OBJECT
from test_st_poly_play_emu import Run,ARGS
from unicorn import UC_HOOK_CODE
from unicorn.m68k_const import UC_M68K_REG_A7,UC_M68K_REG_PC

def copy_probe(native,bank):
    r=Runtime(native,bank);r.u.mem_map(0x80000000,0x10000);r.u.mem_map(0x43bd0000,0x10000)
    # Read local production variables without requiring them to be exported.
    from elekloader.sdk import elf
    e=elf.Elf.load(bank.with_suffix('.work')/'main.elf')
    for s in e.symbols:
        if s.name=='poly_copy_ui':r.sym[s.name]=0x47000000+s.value
        if s.name=='poly_copy_pending':pending=0x47000000+s.value
    r.write(0x800030b8,LIVE);r.write(0x43bd743c,LIVE)
    r.u.mem_write(r.sym['poly_owner_map'],bytes([0,0,0,3,4,5,6,7,8,9,10,11]))
    source=LIVE+0x2a;values=[0x7800,0x300,0x4000,0x2f00,0x200,0,0x100,0x6000]
    r.u.mem_write(source,bytes((i*3)&255 for i in range(0x123)))
    r.u.mem_write(source+0xa2,b'\x41');r.record(r.sym['kit_shadow'],values)
    r.publish(source,0)
    # Install the same native memcpy entry that LFO3's initializer installs.
    r.u.mem_write(0x40115f78,struct.pack('>HI',0x4ef9,r.sym['lfo3_memcpy_entry']))
    # Native publication writes only its engine projection; the existing
    # publication boundary double calls the real LFO3 publication functions.
    published=[]
    def pub(u,at,size,user):
        if at==0x4011a722:
            sp=u.reg_read(UC_M68K_REG_A7);ret,event=struct.unpack('>2I',r.read(sp,8))
            assert struct.unpack('>2I',r.read(event,8))==(0x504f4c59,0x434f5059)
            r.write(event+20,struct.unpack('>I',r.read(pending,4))[0]);r.write(pending,0)
            u.reg_write(UC_M68K_REG_A7,sp+4);u.reg_write(UC_M68K_REG_PC,ret);return
        if at!=0x400a31a2:return
        sp=u.reg_read(UC_M68K_REG_A7);ret,sound,t=struct.unpack('>3I',r.read(sp,12))
        published.append((sound,t))
        # Avoid nested emulator calls in hooks; publication is run after this
        # UI copy transaction using the captured exact source/track pairs.
        u.reg_write(UC_M68K_REG_A7,sp+4);u.reg_write(UC_M68K_REG_PC,ret)
    r.u.hook_add(UC_HOOK_CODE,pub)
    r.write(pending,6);r.call('poly_copy_ui',[])
    assert published==[(source+0x123,1),(source+2*0x123,2)]
    for sound,t in published:
        assert r.read(sound,0x123)==r.read(source,0x123)
        r.publish(sound,t)
        assert r.read(ENGINE+528+16*t,16)==r.read(ENGINE+528,16)
    n=len(published);r.call('poly_copy_ui',[]);assert len(published)==n
    # Editing the source after setup copies LFO3's current record, not a stale
    # cached record or the former slave's own record.
    values[7]=0x5000;r.record(r.sym['kit_shadow'],values);r.publish(source,0)
    r.write(pending,6);r.call('poly_copy_ui',[])
    for sound,t in published[n:]:
        r.publish(sound,t);assert r.read(ENGINE+528+16*t,16)==r.read(ENGINE+528,16)
    return dict(sound_copies=4,shadow_records=True,no_idle_copies=True)

def lock_probe(native,bank):
    r=Run(native,bank);r.u.mem_map(0x45500000,0x20000)
    lock=ARGS+0x4000;context=ARGS+0x5000;stride=20+8*80
    # The pinned backend stores sixteen halfwords in every event record.
    # Native lock-pool stride above and LFO3 metadata row stride are distinct.
    event_bytes=4+16*2
    assert r.call('lfo3_events_context_bytes')==4+(64+13)*event_bytes
    # One real native lock object, three references, three destination tracks.
    r.u.mem_write(lock,struct.pack('>5I',0,3,2,0,0)+struct.pack('>4H',0x36,0x2100,0x2100,0)+struct.pack('>4H',0x2f,0x6500,0x6500,0))
    r.write32(0x444db730,lock);r.write32(r.sym['lfo3_events_context'],context)
    r.write32(context,stride)
    # Captured LFO3 MODE/DEP locks attached to that same native object.
    r.write32(context+4,0xc0)
    r.u.mem_write(context+8,struct.pack('>16H',0,0,0,0,0,0,0x100,0x5000,*([0]*8)))
    for t in range(3):r.call('lfo3_events_apply',t,lock,0x800021b0)
    for t in range(3):
        row=0x800021cc+t*142
        assert struct.unpack('>H',bytes(r.u.mem_read(row+2*0x36,2)))[0]==0x2100
        assert struct.unpack('>H',bytes(r.u.mem_read(row+2*0x2f,2)))[0]==0x6500
        at=context+4+64*event_bytes+event_bytes*t
        assert struct.unpack('>I',bytes(r.u.mem_read(at,4)))[0]==0xc0
        assert struct.unpack('>2H',bytes(r.u.mem_read(at+4+12,4)))==(0x100,0x5000)
    assert struct.unpack('>I',bytes(r.u.mem_read(lock+4,4)))[0]==3
    return dict(tracks=3,native_locks=2,lfo3_locks=2,same_refcount=True)

def main():
    p=argparse.ArgumentParser();p.add_argument('--bank',required=True,type=Path);p.add_argument('--core',required=True,type=Path)
    p.add_argument('--out',required=True,type=Path);a=p.parse_args();native=a.core.read_bytes()
    result=dict(ok=True,copy=copy_probe(native,a.bank),locks=lock_probe(native,a.bank),device_access=False)
    a.out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
