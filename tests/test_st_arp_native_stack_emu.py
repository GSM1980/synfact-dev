# SPDX-License-Identifier: GPL-2.0-or-later
"""Real MainScreen publication, view-manager list and core contextual drawing.

MainScreen::show, shared ownership, manager push and draw traversal run linked
firmware instructions. Model IO, bitmap primitives, base ARP construction and
optional MainScreen maintenance after its normal children are boundaries.
No device access or physical-panel assertion.
"""
import argparse
import json
from pathlib import Path

from investigate_st_arp_ui import InvestigationMachine, MUTE
from test_st_arp_view_emu import APP, MAIN, PARAM, ROOT, BMP
from unicorn.m68k_const import UC_M68K_REG_PC

EX = 0x49100000
CHILDREN = (0x228, 0xc0, 0xe8, 0x218, 0xf0, 0x220)


class NativeStackMachine(InvestigationMachine):
    def __init__(self, core, bank):
        self.native = False
        self.publications = []
        self.draw_walk = []
        super().__init__(core, bank)
        self.u.mem_map(EX, 0x10000)
        self.bump = EX + 0x8000
        manager = APP + 0x40
        head = manager + 0x14
        self.w(head, head); self.w(head+4, head); self.w(manager+0x1c, head)
        self.w(MAIN+0x2c, 0); self.u.mem_write(MAIN+0x16, b'\x01')
        self.w(MAIN+0x98, APP); self.w(MAIN+0x9c, self.selection)
        self.w(MAIN+0xc0, 0); self.w(MAIN+0xc4, 0); self.w(MAIN+0xb8, 1)
        self.w(MAIN+0x140, PARAM)
        self.w(MAIN+0x144, self.owner(PARAM, EX+0x100))
        self.layers = {}
        for i, off in enumerate(o for o in CHILDREN if o != 0xc0):
            view = EX+0x1000+i*0x100
            self.w(view, 0x40241efc)
            self.w(MAIN+off, view)
            self.w(MAIN+off+4, self.owner(view, EX+0x200+i*0x20))
            self.layers[view] = off
        self.w(EX, MAIN); self.w(EX+4, self.owner(MAIN, EX+0x400))
        # Contextual DRAW entry goes through the real core hook/event bus.
        self.w(EX+0x600, self.sym['arp_view_draw'] | 0x80000000)
        self.w(EX+0x604, 0); self.w(self.cs['core:events']+8, EX+0x600)
        self.native = True
        assert self.call(0x40100d86, manager, EX, 0) == 1
        assert self.stack() == [MAIN] + [self.word(MAIN+o) for o in CHILDREN]

    def owner(self, view, root):
        for off, value in ((0,0x401c9910),(4,1),(8,1),(12,view)):
            self.w(root+off, value)
        return root

    def stack(self):
        head = APP+0x54; node = self.word(head); previous=head; views = []
        while node != head:
            assert node and len(views) < 20
            assert self.word(node+4)==previous
            views.append(self.word(node+8));previous=node;node=self.word(node)
        assert self.word(head+4)==previous
        return views

    def draw(self):
        self.rows = []; self.draw_walk = []
        self.u.mem_write(APP+0x60, b'\x01')
        self.call(0x40100992, APP+0x40, BMP)
        assert PARAM in self.draw_walk, self.draw_walk
        return self.rows

    def push_mute(self):
        self.w(EX+0x700, MUTE)
        self.w(EX+0x704, self.owner(MUTE, EX+0x720))
        self.call(0x40100d86, APP+0x40, EX+0x700, 0)
        assert self.stack()[-1] == MUTE

    def remove_mute(self):
        assert self.call(0x40100ee4,APP+0x40,EX+0x700)&255 == 1
        assert MUTE not in self.stack()

    def code(self, u, pc, n, ctx):
        if self.native:
            if pc == 0x40100d86:
                self.publications.append(self.word(self.args(3)[1]))
                return  # Execute the actual manager, bypass the old fixture.
            if pc in (0x4016dfac,0x4017cfb2,0x4003384a,0x400fff26):
                return  # Actual shared ownership and MainScreen publication.
            if pc == 0x400ffec6:
                self.closed += 1
                return  # Actual deferred close; preserve the real list.
            if pc == 0x40033c44:
                # All normal children are now published; remaining native code
                # manages optional application dialogs, outside this scenario.
                u.reg_write(UC_M68K_REG_PC,0x40033db2); return
            if pc in (0x40032692,0x40041076,self.word(0x401c92b0+0x10)):
                self.draw_walk.append(self.args(2)[0]); self.ret(); return
            if pc in (0x4000a0e6,0x400200ae,0x400c2ede,0x400ffe5a,0x400ffe22):
                self.ret(); return
            if pc in (0x40020058,0x4000f5ae): self.ret(0); return
            if pc == 0x4006f432: self.ret(1); return
        return super().code(u, pc, n, ctx)


def verify(core, bank, machine=NativeStackMachine):
    cases = []
    for track, poly in ((0,False),(0,True),(8,False),(9,True),(11,True)):
        m = machine(core,bank); m.track = track
        assert m.stack()[-1] == m.word(MAIN+0x220)
        m.start(poly=poly)
        assert m.stack()[-1] == ROOT+16
        assert 'MODE' in m.texts()
        m.arp_leds(); assert all(value == (2,14) for value in m.leds.values()) and len(m.leds) == 16, m.leds
        assert m.key(27,1,10) == 1 and m.key(27,16,11) == 1
        settings = m.sym['arp_ui_parameters']
        assert int.from_bytes(m.read(settings+6,2),'big') == 0xfff7
        m.leds = {}; m.arp_leds(); assert m.leds[4] == (2,3)
        m.key(0x13,1,2); m.key(0x13,16,3)
        assert 'NOTE' in m.texts() and 'NOT2' in m.texts()
        cases.append(dict(track=track+1,poly=poly,native_stack_draw_and_blue_leds=True))

        q = machine(core,bank); q.track = track
        q.pages([1 if track>=8 else 2,3]+([3] if poly else [])+[3,3])
        q.w(PARAM+0x90,3+int(poly)); q.key(0x12,1,4)
        q.call('arp_view_tick',APP)
        assert q.word(q.sym['arp_view']) == ROOT+16 and 'NOTE' in q.texts()

    return cases


def covered_encoder_edits(m, page):
    """Mute owns trig keys/LEDs while visible ARP knobs still edit their page."""
    settings = m.sym['arp_ui_parameters']
    if page:
        old = [m.lane(i) for i in range(4)]
        for knob in range(1, 5):
            assert m.encoder(knob, 1) == 1
            assert m.lane(knob-1) == (old[knob-1] if old[knob-1] else 64)+1, (knob, old)
            assert m.encoder(knob, -1) == 1
            assert m.lane(knob-1) == (old[knob-1] if old[knob-1] else 64)
        assert m.encoder(5, 1) == 1 and m.read(settings+5, 1) == b'\x01'
        assert m.encoder(5, -1) == 1 and m.read(settings+5, 1) == b'\x00'
    else:
        for knob, offset in ((1, 0), (2, 1), (3, 2), (4, 3), (5, 8+m.word(m.sym['arp_selected'])), (6, 4)):
            old = m.read(settings+offset, 1)[0]
            delta = -1 if knob == 6 else 1
            assert m.encoder(knob, delta) == 1
            assert m.read(settings+offset, 1)[0] == old+delta, (knob, old)
            assert m.encoder(knob, -delta) == 1
            assert m.read(settings+offset, 1)[0] == old
    before = m.read(settings, 24), [m.lane(i) for i in range(4)]
    for knob in (7, 8):
        assert m.encoder(knob, 1) == 1
    assert before == (m.read(settings, 24), [m.lane(i) for i in range(4)])
    m.leds = {}; m.arp_leds(); assert not m.leds


def mute_first_cases(core,bank,machine=NativeStackMachine):
    cases=[]
    for track,poly in ((0,False),(0,True),(8,False),(9,True),(11,True)):
        for initial_page in (0,1):
            m=machine(core,bank);m.track=track
            m.pages([1 if track>=8 else 2,3]+([3] if poly else [])+[3,3])
            first=2+int(poly);m.w(PARAM+0x90,0)
            m.push_mute()
            # Original mute/TRIG key handlers move the native index. ARP must
            # pass both edges while covered, including the opening press.
            for _ in range(first+initial_page):
                m.native_trig();m.call('arp_view_tick',APP)
            assert m.index()==first+initial_page
            assert m.stack()[-2:]==[ROOT+16,MUTE],m.stack()
            labels=m.texts()
            assert ('MODE' in labels) if not initial_page else ('NOTE' in labels and 'NOT2' in labels),labels
            assert m.draw_walk[-1]==MUTE,m.draw_walk
            m.arp_leds();assert not m.leds
            assert m.key(27,1,30)==0 and m.key(27,16,31)==0
            covered_encoder_edits(m, initial_page)
            m.remove_mute();m.call('arp_view_tick',APP)
            assert m.stack()[-1]==ROOT+16 and m.allocs==1
            assert m.word(m.sym['arp_page'])==initial_page
            labels=m.texts()
            assert ('MODE' in labels) if not initial_page else 'NOTE' in labels,labels
            m.arp_leds()
            if initial_page:assert not m.leds
            else:
                assert len(m.leds)==16 and all(value==(2,14) for value in m.leds.values())
                assert m.key(27,1,32)==1 and m.key(27,16,33)==1
                assert int.from_bytes(m.read(m.sym['arp_ui_parameters']+6,2),'big')==0xfff7
            # Reverse order as well: existing ARP, then mute, then return.
            before_cover=m.texts()
            m.push_mute();assert m.stack()[-2:]==[ROOT+16,MUTE]
            assert m.texts()==before_cover
            m.leds={};m.arp_leds();assert not m.leds
            covered_encoder_edits(m, initial_page)
            m.remove_mute();m.call('arp_view_tick',APP)
            assert m.allocs==1 and m.word(m.word(APP+0x5c)+8)==MAIN
            cases.append(dict(track=track+1,poly=poly,page=initial_page,
                              mute_first_draws_below_and_restores_keyboard=True))
    return cases


def covered_routing_cases(core, bank):
    # A mute/track switch can arrive before the next tick. Edit the newly
    # selected Sound, preserving the previous track and the visible subpage.
    for page in (0, 1):
        m = InvestigationMachine(core, bank); m.start(chord=bool(page))
        m.enter_mute(); m.track = 1; m.pages([2, 3]); m.w(PARAM+0x90, 0)
        old = m.read(m.track_address('arp_parameters', 0), 24)
        assert m.encoder(2, 1) == 1
        assert m.word(m.sym['arp_track']) == 1
        assert m.word(m.sym['arp_ui_sound']) == 0x43000000+0x2a+0x123
        assert m.word(m.sym['arp_page']) == page and m.index() == 2+page
        assert m.read(m.track_address('arp_parameters', 0), 24) == old
        if page:
            assert m.lane(1) == 65
        else:
            assert m.read(m.sym['arp_ui_parameters']+1, 1) == b'\x0a'
    m = InvestigationMachine(core, bank); m.start()
    assert m.key(27, 1, 40) == 1
    m.enter_mute(); assert m.encoder(1, 1) == 1
    assert m.read(m.sym['arp_ui_parameters'], 1) == b'\x01'
    assert m.read(m.sym['arp_ui_parameters']+11, 1) == b'\x00'
    assert m.key(27, 16, 41) == 1
    # Native page navigation while covered must be adopted by the first turn.
    m = InvestigationMachine(core, bank); m.start(); m.enter_mute()
    m.native_trig(); assert m.encoder(2, 1) == 1 and m.lane(1) == 65
    # An unrelated transparent window still blocks ARP's edits.
    m.stack_top(m.word(MAIN+0xc0))
    before = m.read(m.sym['arp_ui_parameters'], 24), [m.lane(i) for i in range(4)]
    m.encoder(1, 1)
    assert before == (m.read(m.sym['arp_ui_parameters'], 24), [m.lane(i) for i in range(4)])
    return dict(selected_track=True, cancelled_steps=True, native_page=True, other_windows=True)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('core','bank','out'): p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--before',type=Path,action='append',default=[])
    p.add_argument('--mute-before',type=Path)
    a = p.parse_args()
    result = dict(ok=True,cases=verify(a.core,a.bank),
                  mute_first=mute_first_cases(a.core,a.bank),
                  covered_routing=covered_routing_cases(a.core,a.bank),device_access=False)
    for old in a.before:
        m = NativeStackMachine(a.core,old)
        assert m.key(0x13,1,0) == 0 and not m.word(m.sym['arp_view'])
        m.w(PARAM+0x90,2); m.key(0x12,1,1); m.call('arp_view_tick',APP)
        assert not m.word(m.sym['arp_view']) and not m.texts()
    result['blank_banks_reproduced'] = len(a.before)
    if a.mute_before:
        m=NativeStackMachine(a.core,a.mute_before)
        m.w(PARAM+0x90,1);m.push_mute();m.native_trig()
        m.call('arp_view_tick',APP)
        assert m.index()==2 and not m.word(m.sym['arp_view']) and m.allocs==0
        assert 'MODE' not in m.texts() and m.stack()[-1]==MUTE
        result['mute_first_blank_1_10_1_reproduced']=True
    a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))
