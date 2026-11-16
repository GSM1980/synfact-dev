# SPDX-License-Identifier: GPL-2.0-or-later
"""Deferred native view cleanup preserves closes raised by synchronous compose.

Executes native close, full cleanup, visibility, compose/core_tick, key manager,
shared owners and LED frame. Uses the model/LCD boundaries of ReentrantMachine.
The negative control uses the same bank and old core: no manual list removal.
"""
import argparse
import json
from pathlib import Path

from test_st_arp_reentrant_view_emu import ReentrantMachine
from test_st_arp_view_emu import APP,PARAM
from investigate_st_arp_ui import MUTE


class CleanupMachine(ReentrantMachine):
    def __init__(self,core,bank):
        self.instructions=0;self.cleanup_returns=[]
        super().__init__(core,bank)

    def code(self,u,pc,n,ctx):
        self.instructions+=1
        if self.native and self.compose and pc==0x40100f50:
            self.cleanup_returns.append(self.read(APP+0x62,1)[0])
        return super().code(u,pc,n,ctx)

    def close_mute(self):
        self.call(0x400ffd02,MUTE)
        assert self.read(APP+0x62,1)==b'\x01'
        start=self.instructions
        self.call(0x40100ea4,APP+0x40)
        count=self.instructions-start
        assert MUTE not in self.stack() and not self.word(MUTE+0x2c)
        return count


def scene(core,bank,track=0,poly=False,retain=False,wrap=True):
    m=CleanupMachine(core,bank);m.track=track;first=2+int(poly)
    m.pages([1 if track>=8 else 2,3]+([3] if poly else [])+[3,3])
    m.w(PARAM+0x90,0);m.push_mute()
    for _ in range(first):m.pulse()
    old=m.word(m.sym['arp_view']);assert old
    if retain:assert m.retain()==old
    if wrap:
        m.pulse();m.pulse();assert m.index()==0
    count=m.close_mute()
    return m,old,first,count


def verify(core,bank):
    cases=[]
    for track,poly in ((0,False),(0,True),(8,False),(9,True),(11,True)):
        for retain in (False,True):
            m,old,first,count=scene(core,bank,track,poly,retain)
            # GC already visited ARP when removing mute triggered a nested
            # tick which closed it. That new request must survive GC return.
            assert m.cleanup_returns[-1]==1 and m.read(APP+0x62,1)==b'\x01'
            assert old in m.stack() and m.read(old+0x30,1)==b'\x01'
            assert not m.word(m.sym['arp_view'])
            for _ in range(first):m.pulse()
            current=m.word(m.sym['arp_view'])
            assert current and current!=old and current==m.stack()[-1]
            assert old not in m.stack() and 'MODE' in m.texts()
            assert not any(m.read(v+0x30,1)!=b'\0' for v in m.stack())
            leds=m.led_frame()
            assert len(leds)==16 and all(v==(2,14) for v in leds.values()),leds
            if retain:
                assert m.disposals==0
                m.release();assert m.word(m.sym['arp_view'])==current
            assert m.disposals==1
            assert m.key(27,1,50)==1 and m.key(27,16,51)==1
            assert int.from_bytes(m.read(m.sym['arp_ui_parameters']+6,2),'big')==0xfff7
            cases.append(dict(track=track+1,poly=poly,retained=retain,
                              nested_close_survives_cleanup=True,cleanup_instructions=count))
        # Removing mute while still on ARP does not request an unnecessary
        # second cleanup and must expose the keyboard immediately.
        m,old,first,count=scene(core,bank,track,poly,wrap=False)
        assert m.cleanup_returns[-1]==0 and m.word(m.sym['arp_view'])==old
        assert 'MODE' in m.texts() and len(m.led_frame())==16
    return cases


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('before','after','bank','out'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args()
    cases=verify(a.after,a.bank)
    old,view,first,old_count=scene(a.before,a.bank)
    assert old.cleanup_returns[-1]==0 and old.read(APP+0x62,1)==b'\0'
    assert view in old.stack() and old.read(view+0x30,1)==b'\x01'
    for _ in range(first):old.pulse()
    assert old.index()==first and not old.word(old.sym['arp_view'])
    assert not old.texts() and not old.led_frame() and view in old.stack()
    assert old_count==cases[0]['cleanup_instructions'],(old_count,cases[0])
    # A later unrelated close would wake GC and recover the old core too.
    old.call(0x4010078c,APP+0x40);old.call(0x40100ea4,APP+0x40)
    old.call('arp_view_tick',APP)
    assert 'MODE' in old.texts() and len(old.led_frame())==16
    result=dict(ok=True,cases=cases,previous_core_full_failure_reproduced=True,
                cleanup_instruction_count_identical=True,device_access=False)
    a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
