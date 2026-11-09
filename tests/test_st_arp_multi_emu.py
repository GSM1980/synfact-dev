"""Eight concurrent independent digital ARPs through real UI note producers.
No synthesis/device test. Native producer fixture boundaries are inherited.
"""
import argparse,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_arp_keyboard_emu import ProducerMachine
from test_st_arp_generator_emu import W,OWNER,OWNERMAP,LIST
from test_st_arp_view_emu import ViewMachine,ROOT,MAIN

def verify(core,bank,out):
    m=ProducerMachine(core,bank);pools=m.pool_counts();counts=[]
    for t in range(8):
        assert m.configure(mode=1 if t&1 else 2,track=t,range_=0)
        for note in [60+t,64+t,67+t]:m.key(note,track=t)
    assert m.word(m.sym['arp_running'])==255
    for now in [0,5400000,10800000,16200000]:
        m.frame(now)
    for t in range(8):
        assert [n['pitch'] for n in m.log if n['track']==t]==[60+t,64+t,67+t,60+t]
    assert len(m.log)==32 and m.pool_counts()[0]==pools[0]-8
    # Disable one track without touching the other seven roots or schedules.
    assert m.configure(mode=0,track=3);assert m.word(m.sym['arp_running'])==247
    m.frame(21600000);assert len(m.log)==39
    for t in range(8):
        if t==3:continue
        assert [n['pitch'] for n in m.log if n['track']==t][-1]==64+t
    for t in range(8):
        for note in [60+t,64+t,67+t]:m.key(note,2,track=t)
    m.frame(21628800);assert not m.word(m.sym['arp_running']) and m.pool_counts()==pools
    # Distinct clock rates and settings stay attached to their track.
    m=ProducerMachine(core,bank);pools=m.pool_counts()
    for t in range(8):
        assert m.configure(mode=2,track=t,range_=0,note_length=t)
        m.u.mem_write(W+0x101,bytes([min(17,9+t)]));assert m.call('arp_experiment_configure',t,W+0x100)
        m.key(60+t,track=t)
    for now in range(0,10828800,28800):m.frame(now)
    for t in range(8):
        notes=[n for n in m.log if n['track']==t];counts.append(len(notes))
        assert len(notes)>1 and all(n['pitch']==60+t for n in notes)
        assert m.read(m.track_address('arp_parameters',t),5)==bytes([2,min(17,9+t),0,t,15])
    assert counts==[3,3,4,5,6,7,9,11],counts
    # STOP command cancels every independent voice, keeping enabled settings.
    m.u.mem_write(W+0x140,bytes.fromhex('4152503153544f50')+bytes(16))
    m.call(0x4011a722,W+0x140);assert not m.word(m.sym['arp_running']) and m.pool_counts()==pools
    # Callback instruction growth at 0/1/2/4/8 active tracks. Not CPU cycles.
    costs=[]
    for count in [0,1,2,4,8]:
        m=ProducerMachine(core,bank);pools=m.pool_counts()
        for t in range(count):m.configure(track=t);m.key(60+t,track=t)
        m.frame(0);m.w(0x800046ec,28800);m.count=0;m.call('arp_frame');idle=m.count
        m.w(0x800046ec,5400000);m.count=0;m.call('arp_frame');due=m.count;m.drain(5400000,5428800)
        costs.append({'active_tracks':count,'not_due_instructions':idle,'due_instructions':due})
        for t in range(count):m.key(60+t,2,track=t)
        assert m.pool_counts()==pools
    # The UI selects settings, never the engine's one global voice. Opening
    # another track must neither copy settings from the last page nor stop it.
    m=ViewMachine(core,bank)
    for t in range(8):
        m.track=t;at=1000*t
        assert m.open(at)==1
        p=m.track_address('arp_parameters',t)
        assert m.read(p,1)==b'\x00'
        m.encoder(4,t);assert m.read(p+3,1)==bytes([30+t])
        m.key(13,1,at+120);m.dispose()
    for t in range(8):assert m.read(m.track_address('arp_parameters',t)+3,1)==bytes([30+t])
    result={'ok':True,'concurrent_tracks':8,'independent_rates_note_counts':counts,'callback_instructions':costs,
            'cases':['eight simultaneous chords','one track disabled independently','independent rates and gates',
                     'STOP releases all templates','0/1/2/4/8 bounded callback measurements','eight independent UI pages'],
            'poly_initialized':False,'device_access':False,'cpu_percentage_measured':False,
            'boundaries':['native UI producer fixture boundaries; synthesis and physical scheduling not run']}
    out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['core','bank','out']:p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();verify(a.core,a.bank,a.out)
