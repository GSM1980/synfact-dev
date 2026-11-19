# SPDX-License-Identifier: GPL-2.0-or-later
"""Mute/TRIG wrap with a native shared owner retaining the closed ARP view.

Original manager key traversal, deferred removal, shared refcounts/destruction
and LED-frame gating/walk execute linked instructions. Native model, unrelated
LED handlers, allocation/base ARP construction and panel IO are boundaries.
No device access; the extra shared owner models a legal native callback lifetime.
"""
import argparse
import json
from pathlib import Path

from investigate_st_arp_ui import MUTE
from test_st_arp_native_stack_emu import NativeStackMachine, EX
from test_st_arp_view_emu import APP, PARAM, ROOT, EV

HOLD=EX+0x900


class RetainedMachine(NativeStackMachine):
    def __init__(self,core,bank):
        self.roots=[]
        super().__init__(core,bank)

    def code(self,u,pc,n,ctx):
        if self.native:
            if pc==self.alloc:
                assert self.args(1)==(16+0x50+12,)
                root=ROOT if not self.roots else EX+0x4000+0x100*(len(self.roots)-1)
                self.roots.append(root);self.allocs+=1;self.ret(root);return
            if pc==0x400ffd6a:
                view,title=self.args(2)
                assert view==self.roots[-1]+16 and self.string(title)=='ARPEGGIATOR'
                u.mem_write(view,bytes(80));self.w(view,0x40241efc);self.ret();return
            if pc==0x400ffcb8:
                assert self.args(2)==(self.roots[-1]+16,0)
                self.ret();return
            if pc in (0x400ffe22,0x400ffe5a):
                return  # Real redraw/LED invalidation, including manager flags.
            if pc==0x400082ce:
                self.leds={};self.ret();return  # Panel buffer clear boundary.
            if pc==0x400082f6:
                self.ret();return  # Panel commit boundary.
            if pc==0x401ba614 and self.args(4)[0] not in [MUTE]+[r+16 for r in self.roots]:
                self.ret(0);return  # Unrelated native LED handlers are omitted.
        return super().code(u,pc,n,ctx)

    def pulse(self,key=0x13):
        for flags in (1,16):
            if not self.key(key,flags,20):
                self.call(0x40101498,APP+0x40,EV)
            self.call('arp_view_tick',APP)

    def led_frame(self):
        self.call(0x400094f4,APP)
        return dict(self.leds)

    def retain(self):
        view=self.word(self.sym['arp_view']);root=view-16
        self.w(HOLD,view);self.w(HOLD+4,root)
        before=self.word(root+4)
        self.call(0x4017cfb2,HOLD+8,HOLD)
        assert self.word(root+4)==before+1
        return view

    def release(self):
        self.call(0x4016dfac,HOLD+12)


def cycle(core,bank,track,poly,machine=RetainedMachine):
    m=machine(core,bank);m.track=track
    first=2+int(poly)
    m.pages([1 if track>=8 else 2,3]+([3] if poly else [])+[3,3])
    m.w(PARAM+0x90,0);m.push_mute()
    for _ in range(first):m.pulse()
    assert m.index()==first and m.word(m.sym['arp_view'])
    old=m.retain()
    # Chord page, then first native TRIG, while mute remains above ARP.
    m.pulse();m.pulse();assert m.index()==0
    m.remove_mute();m.call('arp_view_tick',APP)
    for _ in range(first):m.pulse()
    assert m.index()==first and old not in m.stack()
    assert m.disposals==0 and m.word(old-16+4)==1
    return m,old


def verify(core,bank,machine=RetainedMachine):
    cases=[]
    for track,poly in ((0,False),(0,True),(8,False),(9,True),(11,True)):
        m,old=cycle(core,bank,track,poly,machine)
        current=m.word(m.sym['arp_view'])
        assert current and current!=old and current==m.stack()[-1]
        assert m.allocs==2 and 'MODE' in m.texts()
        leds=m.led_frame()
        assert len(leds)==16 and all(v==(2,14) for v in leds.values()),leds
        # A delayed old LED callback cannot claim the new keyboard.
        m.leds={};m.call('arp_view_leds',old+0x50,0,0);assert not m.leds
        assert m.key(27,1,40)==1 and m.key(27,16,41)==1
        assert int.from_bytes(m.read(m.sym['arp_ui_parameters']+6,2),'big')==0xfff7
        assert m.key(27,1,42)==1
        m.release()
        assert m.disposals==1 and m.word(m.sym['arp_view'])==current
        assert m.word(m.sym['arp_steps_held'])==8
        leds=m.led_frame();assert len(leds)==16 and leds[4]==(2,3)
        assert 'MODE' in m.texts()
        assert m.key(27,16,43)==1
        assert int.from_bytes(m.read(m.sym['arp_ui_parameters']+6,2),'big')==0xffff
        cases.append(dict(track=track+1,poly=poly,blue_without_modifier_key=True,
                          old_owner_and_destructor_do_not_clear_new_session=True))
    return cases


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('core','bank','out'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--before',type=Path)
    a=p.parse_args();result=dict(ok=True,cases=verify(a.core,a.bank),device_access=False)
    if a.before:
        m,old=cycle(a.core,a.before,0,False)
        assert m.word(m.sym['arp_view'])==old and m.allocs==1
        assert 'MODE' in m.texts() and not m.led_frame()
        m.release();m.call('arp_view_tick',APP)
        assert m.allocs==2 and len(m.led_frame())==16
        result['previous_blank_keyboard_reproduced_until_owner_release']=True
    a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
