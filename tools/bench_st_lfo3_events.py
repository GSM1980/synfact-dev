"""Measure linked LFO3/ARP event bookkeeping with empty extra-lock storage.

Native apply/restore/convert functions are explicit constant-return boundaries.
Counts include the complete module wrapper, owner/row lookup, queue lookup and
copies; they are per call, not per audio block or hardware CPU percentages.
"""
import argparse,hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
from test_st_lfo3_publication_emu import Runtime,SCRATCH

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('core','bank','out'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();r=Runtime((a.core/'main.bin').read_bytes(),a.bank);r.irq=0x500
    assert r.call('lfo3_locks_setup',[])[0]==1
    r.u.mem_map(0x44000000,0x1000000);r.write(0x444db730,0x44000000)
    context=SCRATCH+0x4000;r.write(r.sym['lfo3_events_context'],context)
    r.call('lfo3_events_init',[context,71])
    for name in ('apply','restore','clear','fx_restore','convert'):
        r.native[r.sym['lfo3_events_native_'+name]]='native_'+name
    cases=[('clear','lfo3_events_clear',[0]),
           ('restore','lfo3_events_restore',[0,0x800021b0,0]),
           ('FX_restore','lfo3_events_fx_restore',[0x800021b0,0]),
           ('apply_empty','lfo3_events_apply',[0,0x44000000,0x800021b0]),
           ('ARP_read_empty','synfact_extra_locks',[0,0x44000000,SCRATCH+0x5000])]
    for track in (0,4,8,12):
        cases.append(('convert_empty_track'+str(track),'lfo3_events_convert',
                      [0x44000000,0x40a38b34+0x3bc5+track*0x2447,0,0,3,0]))
    rows=[]
    for label,name,args in cases:
        result,cost=r.call(name,args);rows.append(dict(case=label,result=result,**cost))
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    report=dict(ok=True,cases=rows,core_sha256=sha(a.core/'main.bin'),bank_sha256=sha(a.bank),
                device_access=False,units='instructions per call with constant native-function boundaries',
                limits=['Real compiled event wrappers, lock storage, queue/owner lookups and copies.',
                        'Native allocator/mutex and native event bodies are boundary doubles.',
                        'No frequency assumption, complete IRQ, DMA/scheduler or physical CPU percentage attribution.'])
    a.out.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(rows,indent=2))
if __name__=='__main__':main()
