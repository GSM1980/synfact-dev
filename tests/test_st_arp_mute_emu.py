# SPDX-License-Identifier: GPL-2.0-or-later
"""ARP transparent-view regressions and before/after MAIN instruction counts.

Native forwarding/model boundaries are those of investigate_st_arp_ui.py.
Counts are guest instructions, not hardware CPU percentages or cache cycles.
"""
import argparse
import json
from pathlib import Path

from investigate_st_arp_ui import InvestigationMachine, MUTE, SYN
from test_st_arp_view_emu import APP, MAIN, PARAM, ROOT, EV
from test_st_arp_keyboard_emu import ProducerMachine


def fixed_cases(core, bank):
    cases = []
    for track, poly in ((0,False), (0,True), (8,True), (9,False), (11,False)):
        m = InvestigationMachine(core, bank); m.track = track
        m.start(poly=poly); first = 3 if poly else 2
        m.enter_mute(); m.native_trig()
        assert m.index() == first+1
        texts = m.texts()
        assert 'NOTE' in texts and 'NOT2' in texts and 'MODE' not in texts, texts
        m.arp_leds(); assert not m.leds
        m.call('arp_view_tick', APP)
        assert m.index() == first+1
        m.stack_top(ROOT+16); m.call('arp_view_tick', APP)
        assert m.index() == first+1 and m.word(m.sym['arp_page']) == 1
        assert m.key(24,1,30) == 0 and m.key(24,16,31) == 0
        # Native navigation can also wrap from the last page while covered.
        m.native_publications = []; m.enter_mute(); m.native_trig()
        assert m.index() == 0
        m.stack_top(ROOT+16); m.call('arp_view_tick', APP)
        assert m.index() == 0 and m.closed == 1
        cases.append(dict(track=track+1, poly=poly, native_chord_and_wrap=True))

    for grid in (False, True):
        m = InvestigationMachine(core, bank); m.start()
        if grid: m.u.mem_write(m.selection+0x168,b'\x01')
        m.enter_mute(); m.arp_leds(); assert not m.leds

    # Exact native REC getter semantics, including inactive selection with
    # stale REC bytes: direct reads must retain the +161h gate.
    for active in (0,1):
        for grid in (0,1):
            for live in (0,1):
                m = InvestigationMachine(core,bank); m.start()
                for at,value in ((0x161,active),(0x168,grid),(0x16a,live)):
                    m.u.mem_write(m.selection+at,bytes([value]))
                native = bool((m.call(0x400c2e56,m.selection)&255) or
                              (m.call(0x400c2fb6,m.selection)&255))
                m.arp_leds()
                assert bool(m.leds) != native

    for tick_before_release in (False, True):
        m = InvestigationMachine(core, bank); m.start()
        assert m.key(27,1,40) == 1
        m.enter_mute()
        if tick_before_release: m.call('arp_view_tick', APP)
        assert m.key(27,16,41) == 1
        assert m.word(m.sym['arp_steps_held']) == 0
        assert m.word(m.sym['arp_steps_cancelled']) == 0
        m.stack_top(ROOT+16); m.call('arp_view_tick', APP)
        assert m.encoder(2,1) == 1
        assert m.read(m.sym['arp_ui_parameters']+1,1) == b'\x0a'
        assert m.read(m.sym['arp_ui_parameters']+8+3,1) == b'\x00'
        assert int.from_bytes(m.read(m.sym['arp_ui_parameters']+6,2),'big') == 0xffff

    for key in (0x14,0x15,0x16,0x17):
        m = InvestigationMachine(core, bank); m.start(); m.enter_mute()
        assert m.key(key,1,50) == 0 and m.closed == 1
        # Closing the covered ARP must preserve the native mute above it.
        assert m.word(m.word(APP+0x58)+8) == MUTE
        if key == 0x14:
            m.w(EV+12,key); m.w(EV+16,1)
            assert m.call(0x4003cdbc,MUTE,EV) & 255 == 0
            m.call(0x40033ee0,MAIN,EV)
            assert m.word(MAIN+0xc0) == SYN
        m.dispose()
        assert m.key(24,1,51) == 0 and m.key(24,16,52) == 0
        m.arp_leds(); assert not m.leds

    # Unrelated windows still block opening. Mute-first opening is checked
    # with the real native list, not this publication double.
    m = InvestigationMachine(core, bank); m.enter_mute()
    m.stack_top(SYN)
    m.w(PARAM+0x90,2); m.call('arp_view_tick',APP)
    assert not m.word(m.sym['arp_view']) and m.index() == 2 and m.allocs == 0
    m.top(MAIN); m.call('arp_view_tick',APP)
    assert m.word(m.sym['arp_view']) == ROOT+16 and m.index() == 2

    # Track rebuild keeps the subpage, but ordinary native navigation is adopted.
    m = InvestigationMachine(core, bank); m.start(chord=True)
    m.track=1; m.pages([2,3]); m.w(PARAM+0x90,0)
    m.call('arp_view_tick',APP)
    assert m.index() == 3 and m.word(m.sym['arp_track']) == 1
    m.w(PARAM+0x90,2)
    assert m.encoder(2,1) == 1 and m.word(m.sym['arp_page']) == 0
    # External parameter replacement without a key event is handled by the tick.
    m.w(MAIN+0xc0,SYN); m.w(SYN,0x401c8d58)
    m.call('arp_view_tick',APP); assert m.closed == 1
    return cases


def native_child_opening(core,bank):
    """1.9.1 rejects the ordinary TRIG child and leaves both pages empty."""
    for poly in (False,True):
        for top in (MAIN,PARAM):
            m = InvestigationMachine(core,bank)
            # Keep both native entry states: MainScreen alone during creation,
            # or the published parameter child after the native page switch.
            m.stack_top(top); m.start(poly=poly)
            assert 'MODE' in m.texts()
            m.key(0x13,1,1); m.key(0x13,16,2)
            assert 'NOTE' in m.texts() and 'NOT2' in m.texts()
            # Closing and reopening must work with the native child restored.
            m.key(13,1,3); m.dispose(); m.start(poly=poly)
            assert 'MODE' in m.texts()

            q = InvestigationMachine(core,bank)
            q.pages([2,3]+([3] if poly else [])+[3,3])
            q.w(PARAM+0x90,3+int(poly)); q.key(0x12,1,4)
            q.stack_top(top); q.call('arp_view_tick',APP)
            assert q.word(q.sym['arp_view']) == ROOT+16
            assert q.word(q.sym['arp_page']) == 1 and 'NOTE' in q.texts()


class CountMachine(InvestigationMachine):
    def __init__(self,*args):
        self.instructions = 0
        super().__init__(*args)
    def code(self,*args):
        self.instructions += 1
        return super().code(*args)
    def measured(self,action):
        self.instructions = 0; action(); return self.instructions


def ui_cost(core,bank):
    out = {}
    for state in ('closed','native_page','arp','chord','mute'):
        m = CountMachine(core,bank)
        if state in ('arp','chord','mute'): m.start(chord=state=='chord')
        elif state=='native_page': m.key(0x12,1,0)
        if state=='mute': m.enter_mute()
        m.call('arp_view_tick',APP)
        tick = m.measured(lambda:m.call('arp_view_tick',APP))
        draw = m.measured(m.draw)
        leds = m.measured(m.arp_leds) if state in ('arp','chord','mute') else 0
        out[state] = dict(tick=tick,draw=draw,leds=leds,total=tick+draw+leds)
    return out


def render_cost(core,bank):
    rows = []
    for n in (0,1,2,4,8):
        m = ProducerMachine(core,bank)
        for t in range(n): m.configure(track=t); m.key(60+t,track=t)
        m.frame(0); m.w(0x800046ec,28800)
        m.count=0; m.call('arp_frame'); idle=m.count
        m.w(0x800046ec,5400000); m.count=0; m.call('arp_frame'); due=m.count
        rows.append(dict(tracks=n,not_due=idle,due=due))
    return rows


def verify(core,bank,before,out,blank_before=None):
    native_child_opening(core,bank)
    cases = fixed_cases(core,bank)
    result = dict(ok=True,cases=cases,device_access=False,
                  units='guest instructions; no hardware CPU percentage measured')
    result['native_parameter_child_opening_and_drawing'] = True
    if blank_before:
        try: native_child_opening(core,blank_before)
        except AssertionError: result['negative_control_rejects_blank_1_9_1'] = True
        else: raise AssertionError('Blank-page 1.9.1 unexpectedly passed')
    if before:
        try: fixed_cases(core,before)
        except AssertionError: result['negative_control_rejects_original_bank'] = True
        else: raise AssertionError('Original bank unexpectedly passed the regression')
        old,new = ui_cost(core,before),ui_cost(core,bank)
        result['ui_instructions'] = dict(before=old,after=new)
        old_render,new_render = render_cost(core,before),render_cost(core,bank)
        assert old_render == new_render, (old_render,new_render)
        result['render_instructions_unchanged'] = new_render
        # Aggregate one tick + one draw + one LED frame at the native UI rate.
        for state in old:
            assert new[state]['total'] <= old[state]['total'], (state,old[state],new[state])
    out.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('core','bank','out'): p.add_argument('--'+n,type=Path,required=True)
    p.add_argument('--before',type=Path)
    p.add_argument('--blank-before',type=Path)
    a=p.parse_args(); verify(a.core,a.bank,a.before,a.out,a.blank_before)
