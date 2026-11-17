# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline reproductions of ARP 1.9 page/transparent-view ownership failures.

This is a diagnostic for the unmodified bank, not a passing regression for a
fix. Assertions establish the reported broken states. View construction,
publication/removal, native title strings and model IO are explicit doubles.
ARP handlers, stock key decoding, stock TRIG navigation and stock mute LED
selection execute linked target instructions. No device IO.
"""
import argparse
import json
from pathlib import Path

from test_st_arp_view_emu import (
    ChordViewMachine, APP, MAIN, MODEL, PARAM, EV, W, ROOT,
)
from unicorn.m68k_const import UC_M68K_REG_A0

MUTE = W + 0x7000
SYN = W + 0x6800
MAP = W + 0x7400
HEAP = W + 0x7600


class InvestigationMachine(ChordViewMachine):
    def __init__(self, core, bank):
        super().__init__(core, bank)
        self.selection = W + 0x1500
        self.w(PARAM + 0x6c, self.selection)
        self.u.mem_write(self.selection + 0x161, b'\x01')
        # PermanentMuteMenuView's native vptr and the fields its handlers read.
        # Its native ctor uses the transparent base View default (+16h = 0).
        self.w(MUTE, 0x401c92b0)
        self.w(MUTE + 0x68, 0x401c9360)
        self.w(MUTE + 0x74, APP)
        self.w(MUTE + 0x78, self.selection)
        self.w(MUTE + 0x80, MODEL)
        self.w(MAIN + 0x230, MUTE)
        for k in range(16):
            self.w(MAP + 4*k, k+1)
        self.w(0x405d4600, MAP)
        self.w(0x405d4604, MAP + 64)
        self.bump = HEAP
        self.native_publications = []
        self.page_handoffs = []

    def code(self, u, pc, n, ctx):
        if self.ready:
            # Stock parameter-view title/COW-string construction and popup IO.
            if pc in (0x40037dd0, 0x400c3422, 0x401bd86e):
                self.ret(); return
            # Native base key chain, and the parameter page's transient editor.
            if pc in (0x400ffcdc, 0x4003b1aa):
                self.ret(0); return
            if pc == 0x400fff26:
                parent, pair, secondary = self.args(3)
                view = self.word(pair)
                assert parent == MAIN and view == MUTE and secondary == 1
                self.native_publications.append(view)
                self.stack_top(view)  # explicit manager publication boundary
                self.ret(); return
            if pc == 0x400ffec6:
                assert self.args(1) == (ROOT+16,)
                self.closed += 1
                self.u.mem_write(ROOT+16+0x30, b'\x01')
                if self.word(self.word(APP+0x58)+8) == ROOT+16:
                    self.top(MAIN)
                self.ret(); return
            if pc == 0x4017cfb2:
                dst, src = self.args(2)
                self.w(dst, self.word(src)); self.w(dst+4, 0)
                self.ret(); return
            if pc == 0x4003384a:
                main, page, secondary = self.args(3)
                assert main == MAIN and page == 1 and secondary == 0
                self.page_handoffs.append(page)
                self.w(SYN, 0x401c8ff8)
                self.w(SYN+0x88, 0x14)
                self.w(SYN+0x6c, self.selection)
                self.w(MAIN+0xc0, SYN)
                self.ret(); return
            if pc == 0x4016dfac:
                self.ret(); return  # borrowed shared owner boundary
            if pc == 0x4016f060:
                out = u.reg_read(UC_M68K_REG_A0)
                self.w(out, 0); self.w(out+4, 0)
                self.ret(); return
            if pc == 0x40031ff4:
                self.ret(0); return  # no active quick-mute gesture
            if pc == 0x40020342:
                model, track = self.args(2)
                assert model == MODEL
                self.ret(track & 1); return  # alternating native track mutes
            if pc == 0x4000828a:
                self.ret(); return  # non-trig LED IO
            if pc == 0x4010394c:
                size, = self.args(1)
                result = self.bump
                self.bump += (size+3) & ~3
                self.ret(result); return
            if pc == 0x40103958:
                self.ret(); return
        return super().code(u, pc, n, ctx)

    def start(self, poly=False, chord=False):
        self.pages([1 if self.track>=8 else 2, 3] + ([3] if poly else []) + [3, 3])
        first = 3 if poly else 2
        self.w(PARAM+0x90, first-1)
        assert self.key(0x13, 1, 0) == 1
        assert self.key(0x13, 16, 1) == 1
        if chord:
            assert self.key(0x13, 1, 2) == 1
            assert self.key(0x13, 16, 3) == 1
        assert self.index() == first + int(chord)

    def enter_mute(self):
        assert self.key(4, 3, 10) == 0  # ARP passes FUNC+BANK
        self.w(EV+12, 4); self.w(EV+16, 3)
        self.call(0x40033ee0, MAIN, EV)  # real MainScreen FUNC+BANK switch
        assert self.native_publications == [MUTE]
        assert self.word(self.word(APP+0x58)+8) == MUTE

    def native_trig(self):
        for flags in (1, 16):
            assert self.key(0x13, flags, 20) == 0
            self.w(EV+12, 0x13); self.w(EV+16, flags)
            assert self.call(0x4003cdbc, MUTE, EV) & 255 == 0
            self.call(0x4003bc74, PARAM, EV)

    def arp_leds(self):
        self.call('arp_view_leds', ROOT+16+0x50, 0, 0)


def investigate(core, bank):
    pages = []
    for poly in (False, True):
        m = InvestigationMachine(core, bank)
        m.start(poly=poly)
        before = m.draw()
        m.enter_mute()
        m.native_trig()
        first = 3 if poly else 2
        assert m.index() == first+1
        assert m.word(m.sym['arp_page']) == 0
        # Both indexes draw identical page contents; only the indicator differs.
        assert m.draw()[:-1] == before[:-1]
        m.call('arp_view_tick', APP)
        assert m.index() == first+1 and m.word(m.sym['arp_page']) == 0
        m.stack_top(ROOT+16)  # native mute removal is an explicit boundary
        m.call('arp_view_tick', APP)
        assert m.index() == first  # returning from mute snaps back to old page
        pages.append(dict(poly=poly, count=first+2,
                          duplicate_page=first+2, content_from_page=first+1,
                          return_from_mute_page=first+1))

    # The unmodified stock mute handler sets only the 12 audio-track LEDs.
    # ARP then runs below it and supplies blue for the other four trig LEDs.
    m = InvestigationMachine(core, bank)
    m.start(); m.enter_mute()
    m.call(0x400375dc, MUTE, W+0x900, W+0x910)
    native_leds = dict(m.leds)
    assert set(native_leds) == set(range(1, 13)), native_leds
    m.arp_leds()
    assert {k: m.leds[k] for k in range(13, 17)} == {k: (2, 14) for k in range(13, 17)}
    assert {k: m.leds[k] for k in range(1, 13)} == native_leds
    assert m.key(36, 1, 30) == 0  # a blue trig is not handled by ARP

    # A native parameter-page change while mute covers ARP does not close ARP.
    # Real mute and MainScreen switches select SYN; replacement is modeled.
    stale = InvestigationMachine(core, bank)
    stale.start(); stale.enter_mute()
    assert stale.key(0x14, 1, 40) == 0 and stale.closed == 0
    stale.w(EV+12, 0x14); stale.w(EV+16, 1)
    assert stale.call(0x4003cdbc, MUTE, EV) & 255 == 0
    stale.call(0x40033ee0, MAIN, EV)
    assert stale.page_handoffs == [1]
    stale.stack_top(ROOT+16)
    stale.call('arp_view_tick', APP)
    assert stale.closed == 0 and stale.word(MAIN+0xc0) == SYN
    stale.arp_leds()
    assert stale.leds == {k: (2, 14) for k in range(1, 17)}
    old = int.from_bytes(stale.read(stale.sym['arp_ui_parameters']+6, 2), 'big')
    assert stale.key(24, 1, 41) == 1 and stale.key(24, 16, 42) == 1
    new = int.from_bytes(stale.read(stale.sym['arp_ui_parameters']+6, 2), 'big')
    assert old ^ new == 1  # edits ARP from a non-ARP parameter page

    # A held step released while covered remains in ARP's private held mask.
    held = InvestigationMachine(core, bank)
    held.start()
    assert held.key(27, 1, 50) == 1
    held.enter_mute()
    assert held.key(27, 16, 51) == 0
    held.stack_top(ROOT+16); held.call('arp_view_tick', APP)
    assert held.word(held.sym['arp_steps_held']) == 8
    assert held.encoder(2, 1) == 1
    assert held.read(held.sym['arp_ui_parameters']+8+3, 1) == b'\x01'

    # Control: ordinary TRIG with ARP actually on top reaches the chord page.
    for poly in (False, True):
        control = InvestigationMachine(core, bank)
        control.start(poly=poly)
        control.key(0x13, 1, 60); control.key(0x13, 16, 61)
        assert control.index() == 3+int(poly) and control.word(control.sym['arp_page']) == 1
    clean = InvestigationMachine(core, bank)
    clean.start(); clean.enter_mute(); clean.stack_top(ROOT+16)
    clean.call('arp_view_tick', APP)
    assert clean.word(clean.sym['arp_steps_held']) == 0
    assert clean.key(24, 1, 62) == 1 and clean.key(24, 16, 63) == 1
    assert int.from_bytes(clean.read(clean.sym['arp_ui_parameters']+6, 2), 'big') == 0xfffe
    for recording in (0x168, 0x16a):
        rec = InvestigationMachine(core, bank)
        rec.start(); rec.u.mem_write(rec.selection+recording, b'\x01')
        rec.enter_mute(); rec.arp_leds()
        assert not rec.leds
    chord = InvestigationMachine(core, bank)
    chord.start(chord=True); chord.enter_mute(); chord.arp_leds()
    assert not chord.leds
    return dict(
        bugs_reproduced=True, pages=pages,
        mute_leds=dict(native_tracks=native_leds, leaked_blue_trigs=[13,14,15,16]),
        stale_view_after_parameter_change=True,
        lost_release_leaves_held_step=4,
        normal_trig_control_passed=True,
        clean_mute_roundtrip_passed=True,
        no_blue_led_leak_with_rec_or_chord_page=True,
        limits=['Manager publication/removal, constructors, title strings and model IO are doubles.',
                'Native page replacement during mute is modeled; no full boot or physical UI/audio.',
                'POLY page presence is a vector fixture; POLY dispatch is not installed.'],
        device_access=False,
    )


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('core', 'bank', 'out'):
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    result = investigate(a.core, a.bank)
    a.out.write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result, indent=2))
