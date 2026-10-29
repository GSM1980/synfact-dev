"""Linked ColdFire M.LFO projection vs donor bank and independent field oracle.

All disjoint digital topologies and PLAY combinations; padding, source tracks,
analog tracks and callee-saved registers must survive. No device access.
"""
import argparse,json,random,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_poly_play_emu import Run,WORK,SIZE,UC_HOOK_CODE
from elekloader.isa import coldfire as cf
from unicorn import UC_HOOK_MEM_READ,UC_HOOK_MEM_WRITE

def maps(at=0,owners=None):
    owners=list(range(12)) if owners is None else owners
    if at==8:
        yield bytes(owners);return
    yield from maps(at+1,owners)
    for width in (2,3,4):
        if at+width>8:continue
        # Independent groups have identity output ownership. The recursion
        # still reserves their voices, so mixed PLAY groups are exercised.
        for shared in (False,True):
            row=owners.copy()
            if shared:
                for t in range(at+1,at+width):row[t]=at
            yield from maps(at+width,row)

def oracle(data,owners):
    result=bytearray(data)
    for t in range(1,8):
        o=owners[t]
        if o>=t:continue
        result[2*t:2*t+2]=data[2*o:2*o+2]
        d,s=28+142*t,28+142*o
        result[d:d+142]=data[s:s+142]
        d,s=0x754+124*t,0x754+124*o
        for start,end in ((0,101),(104,123)):
            result[d+start:d+end]=data[s+start:s+end]
    return bytes(result)

def measured(r,owners,span=None):
    lengths={};counts=[0,0]
    def code(u,at,size,user):
        if at not in lengths:
            raw=bytes(u.mem_read(at,16))
            lengths[at]=cf.decode(cf.reader(raw,at)(at),at).length
        counts[0]+=1;counts[1]+=lengths[at]
    h=r.u.hook_add(UC_HOOK_CODE,code)
    r.u.ctl_flush_tb()
    try:
        if span is None:r.call('poly_fast_publish',WORK,owners)
        else:r.call('poly_fast_publish_span',WORK,owners,span)
    finally:r.u.hook_del(h)
    return dict(instructions=counts[0],executed_instruction_bytes=counts[1])

def main():
    p=argparse.ArgumentParser();p.add_argument('--bank',type=Path,required=True)
    p.add_argument('--previous-bank',type=Path,required=True)
    p.add_argument('--main',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();native=a.main.read_bytes();old=Run(native,a.previous_bank);new=Run(native,a.bank)
    def alignment(u,access,at,size,value,user):
        if WORK<=at<WORK+SIZE and size==4:assert not at&3,hex(at)
    for r in (old,new):r.u.hook_add(UC_HOOK_MEM_READ|UC_HOOK_MEM_WRITE,alignment)
    rng=random.Random(0x54415);cases=0;topologies=list(maps())
    for owners in topologies:
        shared=[t for t in range(1,8) if owners[t]<t]
        span=(shared[0]<<8)|(shared[-1]+1) if shared else 0x0808
        for seed in range(3):
            data=bytes(rng.getrandbits(8) for _ in range(SIZE));expected=oracle(data,owners)
            for r in (old,new):
                r.u.mem_write(WORK,data);r.u.mem_write(r.sym['poly_post_map'],owners)
            old.call('poly_fast_publish',WORK,old.sym['poly_post_map'])
            assert bytes(old.u.mem_read(WORK,SIZE))==expected,('donor',owners)
            new.call('poly_fast_publish_span',WORK,new.sym['poly_post_map'],span)
            assert bytes(new.u.mem_read(WORK,SIZE))==expected,('span',owners,seed)
            new.u.mem_write(WORK,data)
            new.call('poly_fast_publish',WORK,new.sym['poly_post_map'])
            assert bytes(new.u.mem_read(WORK,SIZE))==expected,('full',owners,seed)
            cases+=1
    costs={}
    # Both track parities, source pointer reuse, late masters, adjacent groups.
    for label,owners in (
        ('three_voices',bytes([0,0,0,3,4,5,6,7,8,9,10,11])),
        ('four_voices',bytes([0,0,0,0,4,5,6,7,8,9,10,11])),
        ('odd_master',bytes([0,1,1,1,4,5,6,7,8,9,10,11])),
        ('last_group',bytes([0,1,2,3,4,5,5,5,8,9,10,11])),
        ('two_groups',bytes([0,0,0,3,4,4,4,7,8,9,10,11]))):
        shared=[t for t in range(1,8) if owners[t]<t];span=(shared[0]<<8)|(shared[-1]+1)
        for r in (old,new):r.u.mem_write(r.sym['poly_post_map'],owners)
        before=measured(old,old.sym['poly_post_map']);after=measured(new,new.sym['poly_post_map'],span)
        assert after['instructions']<before['instructions'],(label,before,after)
        assert after['executed_instruction_bytes']<before['executed_instruction_bytes'],label
        costs[label]=dict(before=before,after=after)
    a.out.write_text(json.dumps(dict(cases=cases,topologies=len(topologies),costs=costs,
        device_access=False,units='guest instructions / executed opcode bytes, not cycles'),indent=2)+'\n')
    print('PASS:',cases,'donor/oracle differential cases;',len(topologies),'topology/PLAY combinations')
    print(json.dumps(costs,indent=2))
if __name__=='__main__':main()
