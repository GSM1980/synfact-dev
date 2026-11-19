"""ARP Sound settings/NOTE_ACCEPT differential, including OFF -> locked ON.

The linked persistence, ARP, core dispatch, native event pools/queue and note
consumers execute. Native Sound publication, smoothing and lock application
retain the established fixture boundaries; no synthesis/device measurement.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
from test_st_arp_analog_emu import AnalogMachine
from test_st_arp_generator_emu import W, KIT, LOCKPOOL
from bench_st_arp_main import snapshot

def settings(mode=0, speed=9, range_=1, length=30, pattern=15, scale=0,
             enabled=65535, offsets=None):
    return struct.pack('>6BH16b', mode, speed, range_, length, pattern, scale,
                       enabled, *(offsets or [0]*16))

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for n in ('core','before','after','out'):
        p.add_argument('--'+n, required=True, type=Path)
    p.add_argument('--before-core',type=Path,help='Optional reference core for cross-core comparisons')
    a = p.parse_args()
    changes = [settings(), settings(mode=2), settings(mode=2,speed=17),
               settings(mode=2,speed=17,range_=6), settings(mode=2,length=127),
               settings(mode=2,pattern=0), settings(mode=2,enabled=0xaaaa),
               settings(mode=2,offsets=[i-8 for i in range(16)]),
               settings(), settings(mode=1), settings(mode=4), settings()]
    rows = []
    for track in (0, 8):
        machines = [AnalogMachine(core, bank) for core,bank in
                    zip((a.before_core or a.core,a.core),(a.before,a.after))]
        initial = [m.pool_counts() for m in machines]
        for index, raw in enumerate(changes):
            counts = []
            for m in machines:
                sound = KIT+0x2a+track*0x123
                m.u.mem_write(W+0x100, raw)
                assert m.call('arp_sound_store',sound,W+0x100)
                assert m.read(m.sym['arp_lab_override']+track,1)==b'\x00'
                m.event(track=track,length=0,when=28800*index)
                m.count=0
                m.drain(28800*index,28800*(index+1))
                counts.append(m.count)
            assert snapshot(machines[0]) == snapshot(machines[1]), (track,index)
            assert counts[1] <= counts[0], ('acceptance regression',track,index,counts)
            rows.append(dict(track=track+1,case=index,before=counts[0],after=counts[1]))
        # Release any retained sequence source before the OFF -> mode-lock case.
        for m in machines:
            m.configure(track=track, mode=0)
            m.u.mem_write(m.sym['arp_lab_override']+track,b'\x00')
            m.u.mem_write(W+0x100,settings())
            assert m.call('arp_sound_store',KIT+0x2a+track*0x123,W+0x100)
            context=W+0x2000
            m.w(m.sym['lfo3_events_context'],context)
            m.call('lfo3_events_init',context,71)
            _,lock=m.event(track=track,length=0,when=500000)
            slot=(lock-LOCKPOOL)//(20+8*71)
            m.w(context+4+slot*36,1<<8)
            m.u.mem_write(context+4+slot*36+4+8*2,b'\x00\x02')
        counts=[]
        for m in machines:
            n=len(m.log);m.count=0;m.drain(500000,528800);counts.append(m.count)
            assert len(m.log)>n
            assert m.word(m.sym['arp_running'])&(1<<track)
            assert m.read(m.sym['arp_parameters']+24*track,1)==b'\x00'
            assert m.word(m.sym['arp_root_lock']+20*track)&1
        assert snapshot(machines[0])==snapshot(machines[1])
        assert counts[1]<=counts[0], ('locked mode count',track,counts)
        rows.append(dict(track=track+1,case='OFF to MODE lock ON',before=counts[0],after=counts[1]))
        for m,base in zip(machines,initial):
            m.configure(track=track,mode=0)
            m.drain(2000000,2028800)
            assert m.pool_counts()==base
        assert snapshot(machines[0])==snapshot(machines[1])
    a.out.write_text(json.dumps(dict(ok=True,cases=rows,core_sha256=hashlib.sha256(a.core.read_bytes()).hexdigest(),
        before_core_sha256=hashlib.sha256((a.before_core or a.core).read_bytes()).hexdigest(),
        before_sha256=hashlib.sha256(a.before.read_bytes()).hexdigest(),
        after_sha256=hashlib.sha256(a.after.read_bytes()).hexdigest(),
        fixture_sha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),device_access=False,
        limits=['Instructions, not cycles or CPU percentages.',
                'Native Sound publication, smoothing and lock application are explicit boundaries.',
                'Shared captured lock rows are fixture inputs; real ARP lock service reads them.']),indent=2)+'\n')
    print('PASS Sound edits and OFF->locked ON:',len(rows),'cases; no count increases')
if __name__=='__main__':
    main()
