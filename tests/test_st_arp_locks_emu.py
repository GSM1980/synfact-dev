"""Execute shared LFO3/ARP storage in the compiled bank; native heap is a double."""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_lfo3_publication_emu import Runtime,SCRATCH,BUDGET
from test_st_arp_generator_emu import Machine,W,LOCKPOOL

def verify(core,bank,out):
    r=Runtime(core.read_bytes(),bank)
    def call(name,*args):return r.call(name,list(args))[0]
    assert call('lfo3_locks_setup')==1
    assert call('lfo3_owned_bank_bytes')==11072
    matrix=0x40a38b34+0x3bc5
    s=call('lfo3_owned_lookup',matrix)
    d=call('lfo3_owned_lookup',matrix+0x2138c)
    assert s and d
    # LFO3 and ARP occupy distinct lanes on the same track and step.
    assert call('lfo3_owned_set',s,0,3,0,0x1234,0)==1
    assert call('lfo3_owned_set',s,0,3,8,2,0)==1
    assert call('lfo3_owned_set',s,11,63,13,15*64+48,0)==1
    assert call('lfo3_locks_get',s,0,3,0)==0x1234
    assert call('lfo3_locks_get',s,0,3,8)==2
    assert call('lfo3_owned_clone',d,s)==1
    assert call('lfo3_locks_get',d,11,63,13)==15*64+48
    # Same on-disk 80-lane block; preserve unrelated future/native identities.
    encoded=SCRATCH
    r.u.mem_write(encoded,b'\xff'*(80*130))
    r.u.mem_write(encoded,bytes([1,0])+struct.pack('>64H',*([42]*64)))
    assert call('lfo3_locks_merge',s,encoded)==1
    assert r.read(encoded,130)==bytes([1,0])+struct.pack('>64H',*([42]*64))
    call('lfo3_owned_clear',d)
    assert call('lfo3_owned_decode',d,encoded)==1
    assert call('lfo3_locks_get',d,0,3,0)==0x1234
    assert call('lfo3_locks_get',d,0,3,8)==2
    # Capacity remains shared; deleting a lane makes exactly one lane available.
    call('lfo3_owned_clear',s)
    lanes=[(t,p) for t in range(13) for p in range(16) if p!=3][:79]
    for t,p in lanes:assert call('lfo3_owned_set',s,t,1,p,1,0)==1
    t,p=lanes[-1]
    assert call('lfo3_owned_set',s,12,1,15,1,0)==0
    assert call('lfo3_owned_set',s,t,1,p,-1,0)==1
    assert call('lfo3_owned_set',s,12,1,15,1,0)==1
    # Native lanes participate in the same guard.
    assert call('lfo3_owned_set',d,1,1,8,1,79)==0
    # A lock object captures both families and keeps their payloads separate.
    r.u.mem_map(0x44000000,0x1000000)
    r.write(0x444db730,0x44000000)
    context=SCRATCH+0x4000
    r.write(r.sym['lfo3_events_context'],context)
    call('lfo3_events_init',context,71)
    r.u.mem_write(SCRATCH+0x5010,b'GUARD123')
    call('lfo3_events_capture',0x44000000,0x40a38b34+0x2138c,0,3)
    mask=call('synfact_extra_locks',0,0x44000000,SCRATCH+0x5000)
    assert mask==1 and r.read(SCRATCH+0x5000,2)==b'\x00\x02'
    assert r.read(SCRATCH+0x5010,8)==b'GUARD123'
    assert int.from_bytes(r.read(context+4,4),'big')==0x101
    r.native[r.sym['lfo3_events_native_apply']]='event_boundary'
    call('lfo3_events_apply_body',0,0x44000000,0)
    assert call('synfact_extra_locks',0,0xffffffff,SCRATCH+0x5000)==1
    overlay=call('lfo3_events_bank')
    assert r.read(overlay,2)==b'\x12\x34'
    call('lfo3_events_clear_active',0)
    assert call('synfact_extra_locks',0,0xffffffff,SCRATCH+0x5000)==0
    call('lfo3_events_allocate',0x44000000)
    assert call('synfact_extra_locks',0,0x44000000,SCRATCH+0x5000)==0
    # Actual ARP acceptance uses its event's extra LEN, leaving base unchanged.
    m=Machine(core,bank);m.configure(note_length=30)
    ctx=W+0x2000;m.w(m.sym['lfo3_events_context'],ctx)
    m.call('lfo3_events_init',ctx,71)
    _,lock=m.event(length=0)
    slot=(lock-LOCKPOOL)//(20+8*71)
    m.w(ctx+4+slot*36,1<<11)
    m.u.mem_write(ctx+4+slot*36+4+11*2,b'\x00\x00')
    m.frame(0)
    assert m.log[0]['gate']==m.call('arp_note_gate',0)
    assert m.read(m.sym['arp_parameters']+3,1)==b'\x1e'
    result=dict(ok=True,bank_bytes=11072,allocator_block_bytes=16384,
                cases=['LFO3 and ARP isolation','analog track 12','clone','merge preserves native lane',
                       'decode roundtrip','79 lane shared capacity','deleted lane reused','native lane capacity',
                       'event capture/reuse','active locks/restore','LFO3 overlay','ARP LEN acceptance'],
                limits=['Native allocator/mutex are doubles; no device access.'])
    out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result))

if __name__=='__main__':
    p=argparse.ArgumentParser()
    for n in ('core','bank','out'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();verify(a.core,a.bank,a.out)
