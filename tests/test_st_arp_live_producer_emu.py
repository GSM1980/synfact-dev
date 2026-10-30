"""Real factory defaults + native UI note builders -> standalone mono ARP.

Executes 400b3574, 40104c02/4010502c, 40119684, real core bus/pools/queue
and native pitch/gate effects. Explicit boundaries are documented in the
imported ProducerMachine. This is not a synthesis/device recording.
"""
import argparse,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_arp_keyboard_emu import ProducerMachine
from test_st_arp_generator_emu import W,KIT

def verify(core,bank,out,previous_bank=None):
    cases=[]
    if previous_bank:
        m=ProducerMachine(core,previous_bank);m.configure(range_=0);m.key(60)
        assert not m.word(m.sym['arp_live']) and m.word(0x444de434)
        assert m.inputs[-1][9]==0x10781 and m.inputs[-1][11]==m.preview
        cases.append('TEST02-native-preview-input-does-not-start-negative-control')
    for track in range(8):
        m=ProducerMachine(core,bank);pools=m.pool_counts();m.w(0x43bd73bc,2)
        assert m.configure(track=track,range_=0)
        for n in [60,64,67]:m.key(n,track=track)
        assert m.word(m.track_address('arp_live',track)) and not m.word(0x444de434)
        assert all(e[9]==0x10781 and e[11]==m.preview for e in m.inputs)
        root=m.word(m.track_address('arp_voice',track)+200);sound=m.word(root+44)
        assert sound!=m.preview and m.read(sound,0x123)==m.read(m.preview,0x123)
        # UI preview storage can change to another Sound while this template
        # is held. The captured flat record must not alias those mutable bytes.
        m.u.mem_write(m.preview+0x100,b'\x5a');assert m.read(sound+0x100,1)==b'\x00'
        for t in [0,5400000,10800000]:m.frame(t)
        assert m.pitches()==[60,64,67],(track,m.log)
        for other in range(12):
            if other!=track:assert not m.word(0x800046b8+4*other) and not m.word(0x80003084+4*other)
        # Release the newest key: native priority emits an off with flags 0
        # followed by a resend of the previously held note. Keep the chord.
        m.key(67,2,track=track)
        assert any(e[1]==2 and e[9]==0 for e in m.inputs)
        m.frame(16200000);m.frame(21600000)
        assert m.pitches()==[60,64,67,60,64],(track,m.log)
        m.key(60,2,track=track);m.key(64,2,track=track);m.frame(21628800)
        assert not m.word(m.track_address('arp_live',track)) and m.pool_counts()==pools
        assert m.word(0x80003084+4*track)==1
        cases.append('factory-UI-preview-chord-track-'+str(track+1))
    m=ProducerMachine(core,bank);pools=m.pool_counts();m.configure(range_=1);m.key(60)
    for t in [0,5400000,10800000]:m.frame(t)
    assert m.pitches()==[60,72,60];m.key(60,2);m.frame(10828800);assert m.pool_counts()==pools
    cases.append('single-track-pad-note-octave-arpeggio-release')
    m=ProducerMachine(core,bank);pools=m.pool_counts();m.configure(mode=1,range_=0)
    for n in [67,60,64]:m.key(n)
    for t in [0,5400000,10800000,16200000]:m.frame(t)
    assert m.pitches()==[67,60,64,67]
    for n in [60,67,64]:m.key(n,2)
    m.frame(16228800);assert m.pool_counts()==pools
    cases.append('native-UI-TRUE-insertion-order')
    # Same full native factory flags also work for ordinary sequence roots.
    m=ProducerMachine(core,bank);pools=m.pool_counts();m.configure(range_=1)
    m.event(flags=0x10781,length=10800000);m.frame(0);m.frame(5400000)
    assert m.pitches()==[60,72]
    assert m.configure(mode=0) and m.pool_counts()==pools
    cases.append('native-full-trig-flags-sequence-root')
    # OFF retains original producer playback and frees its event normally.
    m=ProducerMachine(core,bank);pools=m.pool_counts();m.key(60);m.frame(0)
    assert m.pitches()==[60] and not m.word(m.sym['arp_live']) and m.pool_counts()==pools
    m.key(60,2);m.drain(0,28800);assert m.pool_counts()==pools
    cases.append('native-UI-OFF-passthrough')
    result={'ok':True,'cases':cases,'native_note_on_flags':'0x10781','native_note_off_flags':'0x0',
            'native_UI_source':64,'native_preview_global':'0x444842d0','Sound_snapshot_bytes':0x123,
            'poly_initialized':False,'synthesis_executed':False,'device_access':False,
            'executed':['factory TRIG defaults','UI note-on producer','UI note-off producer',
                        'held-set and mono priority','core IMMEDIATE','native pools/queue/copy/free','native pitch/gate'],
            'boundaries':['MIDI bookkeeping','record-message delivery/position','Sound lookup/publication',
                          'smoothing/lock apply','complete scheduling and physical UI not run']}
    out.write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: '+str(len(cases))+' native UI producer cases; factory flags, borrowed preview Sound, zero-flag releases, mono outputs and balanced pools')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['core','bank','out']:p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--previous-bank',type=Path)
    a=p.parse_args();verify(a.core,a.bank,a.out,a.previous_bank)
