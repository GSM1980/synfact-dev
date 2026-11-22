"""Full native catalog's real LFO3/ARP/POLY/Playhead initialization; no IO.

Uses the activity fixture's explicit renderer, Chorus UI, allocator and mutex
boundaries. The binder, registration, hooks and activity transitions execute.
"""
import argparse,json
from pathlib import Path
from test_st_lfo3_activity_emu import Activity

def verify(core,bank,overflow_reference=None):
    m=Activity(core,bank,full_catalog=True)
    assert any(name=='PLAYHEAD' and 11 in subscriptions for name,_,subscriptions in m.modules)
    assert 'PLAYHEAD' in m.initialized
    expected={'lfo3_native','arp_native','poly_native','chorus_native','playhead_native'}
    if 'comp_native' in m.bs:expected.add('comp_native')
    start=m.sym['core:core_native_masks'];end=m.sym['core:core_native_masks_end']
    registered={m.word(at) for at in range(start,end,12)}
    assert {m.bs[name] for name in expected}<=registered
    cases=[]
    for track in range(13):
        for label,depth,dest,active in [('inactive',0x4000,0,False),('active',0x7000,0x2f00,True),
                                      ('neutral',0x4000,0x2f00,False)]:
            m.publish(track,depth,dest);frame=m.frame()
            assert bool(frame.get('render',0))==active,(track,label,frame)
            if not active:assert not frame.get('dispatch',0),frame
            cases.append(dict(track=track,case=label,**frame))
        m.publish(track)
    negative=None
    if overflow_reference:
        try:Activity(overflow_reference,bank,full_catalog=True)
        except AssertionError as exc:
            assert isinstance(exc.args[0],tuple) and exc.args[0][0]=='initial_inactive_route',exc
            negative=dict(detected=True,route=exc.args[0][1])
        else:raise AssertionError('old four-slot core failed to reproduce the inactive LFO route')
    return dict(ok=True,native_slots=(end-start)//12,real_playhead_initializer=True,
                registered_native_subscribers=len(expected),cases=cases,overflow_negative_control=negative,
                device_access=False,limits=['Renderer and Chorus UI/DSP, allocator and mutex are explicit boundaries.',
                                           'Not a full RTOS boot or a physical CPU percentage measurement.'])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for key in ('core','bank','out'):p.add_argument('--'+key,type=Path,required=True)
    p.add_argument('--overflow-reference',type=Path)
    a=p.parse_args();result=verify(a.core,a.bank,a.overflow_reference)
    a.out.write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: full native catalog; real Playhead initialization; 39 LFO activity cases; no device access')
