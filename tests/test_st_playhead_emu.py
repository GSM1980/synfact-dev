"""playhead-st's step sites against the stock sequencer code, in Unicorn.

The stock MAIN image runs its own code at the five places where playhead-st
mirrors a backwards track's step (mods/playhead-st/seq-direction.s): the two
pushes of the step for the trig preparation (4008cf00..cf08, 4008cf14..cf1c),
the push for a pattern started mid-way (4008c204..c20e), the UI's copy of
every track's step and last step (4008d166..d1d2, a whole tick's loop) and the
accessor the trig LEDs' playhead reads (4008b734, called as the UI calls it).
Three runs per case: stock (the image as Elektron ships it); forwards, after
the module's real `playhead_seq_set` put the sites in for another track; and
backwards. Forwards must equal stock, registers, pushed arguments and memory;
backwards the same with the step mirrored, length - 1 - step, a step at or
past the length as it is. Both length modes (the pattern's, the track's).

Then the sites come out with the last backwards track (stock bytes back), and
a changed context is refused with nothing written.

The other modes (playhead_step in mods/playhead-st/seq-direction.c), called as
the stubs call it: pendulum goes 1 .. L .. 2 and again, no end twice; random
stays in the track and reaches every step; walk moves one step either way,
around the ends; a count asked again gets the same step, the count before it
the step before, and a count that is not the next one starts the mode again.
Through the stubs as the sequencer runs them (the tick's UI copy after the
advance, then the trig at tick 0, then the LEDs): all three see the same step.

Run with the Unicorn build from docs/SYNTAKT-BUILD.md:
  "$PY" -B tests/test_st_playhead_emu.py --bank <bank with playhead-st> --stock "$SYNTAKT_STOCK"
"""
import argparse
import json
import random
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from elekloader import syx                                   # noqa: E402
from elekloader.sdk import elf                               # noqa: E402
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN     # noqa: E402
from unicorn.m68k_const import *                             # noqa: E402,F403

MAIN = 0x40000400
BANK = 0x46000000
STACK = 0x48010000
RETURN = 0x4f000000
SEQ, UI = 0x43bd0000, 0x444e0000                  # the sequencer's and the UI copies' RAM
STEP, LAST, TICK = 0x43bd83fc, 0x43bd8416, 0x43bd8409
CURRENT = 0x43bd8430                              # the pattern playing
CURRENT_KIT = 0x43bd843c                          # its kit (43bd7438 / 43bd7444: the next ones)
KITS, KIT_BYTES, KIT_INDEX = 0x41ad6134, 0x1618, 5  # the live kits; the tests' pattern uses kit 5
UI_LAST, UI_STEP = 0x444e3370, 0x444e337d
PATTERN = 0x50000000                              # + 21379h flag, + 21374h length, tracks + 499h * t
SITES = (0x4008cf78, 0x4008cf8c, 0x4008c27c, 0x4008d230, 0x4008b7be)
TRACKS = 13
D = [UC_M68K_REG_D0 + i for i in range(8)]
A = [UC_M68K_REG_A0 + i for i in range(7)]


def bank_image(bank):
    raw = bank.read_bytes()
    size, nrel = struct.unpack_from('>2I', raw, 52)
    image = bytearray(raw[80 + 4 * nrel:80 + 4 * nrel + size])
    for off in struct.unpack_from('>%dI' % nrel, raw, 80):
        struct.pack_into('>I', image, off, struct.unpack_from('>I', image, off)[0] + BANK)
    e = elf.Elf.load(bank.parent / (bank.stem + '.work') / 'main.elf')
    return bytes(image), {s.name: s.value + BANK for s in e.symbols if s.name}


def mirror(step, length):
    return length - 1 - step if step < length else step


class Machine:
    def __init__(self, main, bank, symbols):
        self.u = u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
        u.mem_map(0x40000000, (len(main) + 0x103ff) & ~0xffff)
        u.mem_write(MAIN, main)
        u.mem_map(BANK, (len(bank) + 0xffff) & ~0xffff)
        u.mem_write(BANK, bank)
        for at, size in ((STACK - 0x10000, 0x20000), (RETURN, 0x1000), (SEQ, 0x10000), (0x43bc0000, 0x10000),
                         (UI, 0x10000), (PATTERN, 0x30000)):
            u.mem_map(at, size)
        self.sym = symbols

    def w8(self, at, v): self.u.mem_write(at, bytes([v & 0xff]))
    def w16(self, at, v): self.u.mem_write(at, struct.pack('>h', v))
    def w32(self, at, v): self.u.mem_write(at, struct.pack('>I', v & 0xffffffff))

    def call(self, fn, *args):
        sp = STACK - 0x100 - 4 * len(args)
        self.u.reg_write(UC_M68K_REG_SR, 0x2000)     # supervisor first: it selects A7
        self.u.mem_write(sp, struct.pack('>%dI' % (len(args) + 1), RETURN, *args))
        self.u.reg_write(UC_M68K_REG_A7, sp)
        self.u.emu_start(fn, RETURN, count=200000)
        return self.u.reg_read(UC_M68K_REG_D0)

    def song(self, flag, pattern_length, lengths):
        self.u.mem_write(PATTERN, bytes(0x30000))
        self.w8(PATTERN + 0x21379, flag)
        self.w16(PATTERN + 0x21374, pattern_length)
        for t, n in enumerate(lengths):
            self.w16(PATTERN + 0x499 * t + 969, n)
        self.w32(CURRENT, PATTERN)
        self.w32(CURRENT_KIT, KITS + KIT_INDEX * KIT_BYTES)       # the playing pattern's kit
        self.w32(CURRENT + 8, 0)                                   # no next pattern
        self.w32(CURRENT_KIT + 8, 0)

    def run(self, start, end, regs):
        u = self.u
        u.reg_write(UC_M68K_REG_SR, 0x2000)
        for r, v in regs.items():
            u.reg_write(r, v)
        sp = STACK - 0x200
        u.mem_write(sp - 0x40, bytes(0x80))
        u.reg_write(UC_M68K_REG_A7, sp)
        u.emu_start(start, end, count=5000)
        assert u.reg_read(UC_M68K_REG_PC) == end
        state = {r: u.reg_read(r) for r in D + A + [UC_M68K_REG_A7]}
        top = state[UC_M68K_REG_A7]                   # below it is dead: the stubs' scratch
        return state, bytes(u.mem_read(top, sp + 0x40 - top))

    def site(self, at):
        return bytes(self.u.mem_read(at, 6))


def garbage(rnd):
    return {r: rnd.getrandbits(32) for r in D + A}


def song_cases(rnd):
    for flag in (0, 1):
        for pattern_length in (1, 2, 16, 33, 64):
            lengths = [rnd.choice((1, 2, 3, 7, 16, 31, 64)) for _ in range(TRACKS)]
            yield flag, pattern_length, lengths


def run(stock_path, bank_path):
    main = syx.Syx.load(stock_path).section(3)
    bank, sym = bank_image(bank_path)
    stock = Machine(main, bank, sym)
    hooked = Machine(main, bank, sym)
    stock_sites = [stock.site(at) for at in SITES]
    dirs = sym['playhead_modes'] + 16 * KIT_INDEX
    stubs = [sym[n] for n in ('playhead_trig_a', 'playhead_trig_b', 'playhead_trig_c', 'playhead_ui_copy',
                                'playhead_step_shown')]
    assert hooked.call(sym['playhead_seq_set'], KIT_INDEX, 12, 1) == 0
    for at, stub in zip(SITES, stubs):
        assert hooked.site(at) == b'\x4e\xf9' + struct.pack('>I', stub), hex(at)
    rnd = random.Random(7)
    n = 0
    for flag, pattern_length, lengths in song_cases(rnd):
        for m in (stock, hooked):
            m.song(flag, pattern_length, lengths)

        def length(t):
            return lengths[t] if flag else pattern_length
        for t in range(TRACKS):
            for step in sorted({0, 1, length(t) - 1, length(t), length(t) + 3, 255}):
                want_step = mirror(step, length(t))
                # The trig preparation's pushes at a step's tick 0, both calls.
                for start, end in ((0x4008cf78, 0x4008cf80), (0x4008cf8c, 0x4008cf94)):
                    regs = garbage(rnd)
                    regs.update({D[0]: PATTERN, D[6]: t, A[6]: STEP + t})
                    for m in (stock, hooked):
                        m.w8(STEP + t, step)
                    want, want_stack = stock.run(start, end, regs)
                    for backwards in (0, 1):
                        if t != 12:
                            hooked.w8(dirs + t, backwards)
                        elif not backwards:
                            continue
                        got, stack = hooked.run(start, end, regs)
                        if not backwards:
                            assert got == want and stack == want_stack, ('forwards', hex(start), t, step)
                            continue
                        pushed = struct.unpack_from('>3I', stack, 0)
                        assert pushed == (want_step, 0, regs[D[5]]), (hex(start), t, step, pushed)
                        assert got == {**want, D[1]: want_step}, ('backwards', hex(start), t, step)
                        assert stack[12:] == want_stack[12:]
                # A pattern started mid-way: the step is d0, sign-extended.
                regs = garbage(rnd)
                regs.update({D[0]: (step - 256 if step >= 128 else step) & 0xffffffff, D[2]: t})
                want, want_stack = stock.run(0x4008c27c, 0x4008c286, regs)
                for backwards in (0, 1):
                    if t != 12:
                        hooked.w8(dirs + t, backwards)
                    elif not backwards:
                        continue
                    got, stack = hooked.run(0x4008c27c, 0x4008c286, regs)
                    if not backwards or want_step == step:
                        assert got == want and stack == want_stack, ('mid-way', t, step, backwards)
                        continue
                    assert struct.unpack_from('>3I', stack, 0) == (PATTERN, regs[A[0]], want_step)
                    assert got == {**want, D[0]: want_step}, ('mid-way backwards', t, step)
                    assert stack[12:] == want_stack[12:]
                # The step the LEDs show: 4008b734(t) -> last[t].
                for m in (stock, hooked):
                    m.w8(LAST + t, step)
                    m.w16(0x43bd83c4, 0x1234)
                want = stock.call(0x4008b7ac, t)
                assert want == step
                for backwards in (0, 1):
                    if t != 12:
                        hooked.w8(dirs + t, backwards)
                    elif not backwards:
                        continue
                    got = hooked.call(0x4008b7ac, t)
                    assert got == (want_step if backwards else want), ('LEDs', t, step, backwards, got)
                n += 4
                if t != 12:
                    hooked.w8(dirs + t, 0)
        # The UI's copy, a whole tick's loop, with a random set of backwards tracks.
        for _ in range(8):
            back = [rnd.random() < 0.5 for _ in range(TRACKS)]
            back[12] = True                              # keeps the sites in
            steps = [rnd.choice((0, 1, length(t) - 1, length(t), 255, rnd.randrange(0, 64)))
                     for t in range(TRACKS)]
            lasts = [rnd.choice((255, 0, max(0, steps[t] - 1), rnd.randrange(0, 64))) for t in range(TRACKS)]
            for m in (stock, hooked):
                m.u.mem_write(UI, bytes(0x10000))
                for t in range(TRACKS):
                    m.w8(STEP + t, steps[t])
                    m.w8(LAST + t, lasts[t])
                    m.w8(TICK + t, t * 3)
                    m.w32(0x444e3404 + 4 * t, 0x1000 + t)
            for t in range(TRACKS):
                hooked.w8(dirs + t, back[t])
            regs = garbage(rnd)
            regs[D[0]] = 13                             # the advance loop leaves d0 = 13; 4008d166 clears its byte
            want, want_stack = stock.run(0x4008d1de, 0x4008d24a, regs)
            want_ui = bytearray(stock.u.mem_read(0x444e3300, 0x140))
            got, stack = hooked.run(0x4008d1de, 0x4008d24a, regs)
            got_ui = bytes(hooked.u.mem_read(0x444e3300, 0x140))
            assert got == want and stack == want_stack, 'ui copy registers'
            for t in range(TRACKS):
                if back[t]:
                    want_ui[UI_LAST - 0x444e3300 + t] = mirror(lasts[t], length(t))
                    want_ui[UI_STEP - 0x444e3300 + t] = mirror(steps[t], length(t))
            assert got_ui == bytes(want_ui), ('ui copy', back, steps, lasts)
            n += 1
        for t in range(TRACKS - 1):
            hooked.w8(dirs + t, 0)
    # Past the tracks the accessor gives the pattern's step, mirrored or not.
    assert stock.call(0x4008b7ac, 13) == hooked.call(0x4008b7ac, 13) == 0x1234
    # The sites stay while another track is backwards, and go out with the last.
    assert hooked.call(sym['playhead_seq_set'], KIT_INDEX, 3, 1) == 0
    assert hooked.call(sym['playhead_seq_set'], KIT_INDEX, 12, 0) == 0
    assert all(hooked.site(at)[:2] == b'\x4e\xf9' for at in SITES)
    assert hooked.call(sym['playhead_seq_set'], KIT_INDEX, 3, 0) == 0
    assert [hooked.site(at) for at in SITES] == stock_sites
    assert hooked.call(sym['playhead_seq_set'], KIT_INDEX, 13, 1) == 0
    assert [hooked.site(at) for at in SITES] == stock_sites
    # A context that is not the stock one: refused, nothing written.
    refused = Machine(main, bank, sym)
    refused.u.mem_write(0x4008d242, b'\x4e\x71')
    assert refused.call(sym['playhead_seq_set'], KIT_INDEX, 0, 1) == 1
    assert [refused.site(at) for at in SITES] == stock_sites and refused.u.mem_read(dirs, 1) == b'\0'
    return n


FORWARDS, BACKWARDS, PENDULUM, RANDOM, WALK, ANCHOR, STUTTER, FOLD, WALK_PLUS = range(9)


def run_modes(stock_path, bank_path):
    main = syx.Syx.load(stock_path).section(3)
    bank, sym = bank_image(bank_path)
    m = Machine(main, bank, sym)
    step, dirs = sym['playhead_step'], sym['playhead_modes'] + 16 * KIT_INDEX
    lengths = [16, 1, 2, 3, 7, 64, 5, 8, 4, 16, 31, 12, 16]
    m.song(1, 16, lengths)
    m.w32(0x43bcc3a4, 0x1234567)
    n = 0
    for t, L in enumerate(lengths):
        def play(mode, steps):
            m.call(sym['playhead_seq_set'], KIT_INDEX, t, mode)
            got = []
            for i in range(steps):
                k = i % L
                p = m.call(step, t, k, PATTERN, 1)
                assert m.call(step, t, k, PATTERN, 1) == p == m.call(step, t, k, PATTERN, 0)
                if i:
                    assert m.call(step, t, (i - 1) % L, PATTERN, 0) == got[-1] or L == 1
                assert 0 <= p < L, (mode, t, L, p)
                got.append(p)
            return got
        assert play(FORWARDS, 3 * L) == [i % L for i in range(3 * L)]
        assert play(BACKWARDS, 3 * L) == [L - 1 - i % L for i in range(3 * L)]
        if L > 1:
            cycle = list(range(L)) + list(range(L - 2, 0, -1))
        else:
            cycle = [0]
        assert play(PENDULUM, 4 * L) == [cycle[i % len(cycle)] for i in range(4 * L)], (t, L)
        got = play(RANDOM, 40 * L)
        assert set(got) == set(range(L)), (t, L, sorted(set(got)))
        got = play(WALK, 40 * L)
        assert got[0] == 0
        for a, b in zip(got, got[1:]):
            assert L == 1 and b == 0 or (b - a) % L in (1, L - 1), (t, L, a, b)
        if L > 2:
            assert len(set(got)) > 2
        anchor = [0] if L == 1 else [v for j in range(1, L) for v in (0, j)]
        assert play(ANCHOR, 6 * L) == [anchor[i % len(anchor)] for i in range(6 * L)]
        fold = [v for pair in zip(range(L), reversed(range(L))) for v in pair][:L]
        fold += fold[-2:0:-1]
        assert play(FOLD, 6 * L) == [fold[i % len(fold)] for i in range(6 * L)]
        assert play(STUTTER, 10 * L) == [(i // 5 * 4 + min(i % 5, 3)) % L for i in range(10 * L)]
        got = play(WALK_PLUS, 400 * L)
        assert got[0] == 0
        if L > 2:
            moves = [(b - a) % L for a, b in zip(got, got[1:])]
            assert set(moves) == {0, 1, L - 1}
            # Broad bounds verify forward gravity without pinning a PRNG stream.
            assert .5 < moves.count(1) / len(moves) < .7
            assert .12 < moves.count(0) / len(moves) < .28
            assert .12 < moves.count(L - 1) / len(moves) < .28
        # A count that is not the next one starts again from it (pendulum: up).
        m.call(sym['playhead_seq_set'], KIT_INDEX, t, PENDULUM)
        for k in range(3):
            m.call(step, t, k % L, PATTERN, 1)
        if L > 4:
            assert m.call(step, t, 4, PATTERN, 1) == 4 and m.call(step, t, 5, PATTERN, 1) == 5
        assert m.call(step, t, L + 2, PATTERN, 1) == L + 2           # past the length: as it is
        m.call(sym['playhead_seq_set'], KIT_INDEX, t, FORWARDS)
        n += 1
    # Real tick-copy, all three trig sites and LEDs agree for every new mode,
    # including the two-step case where previous and next counts coincide.
    for t in (4, 2):
        L = lengths[t]
        for mode in (PENDULUM, ANCHOR, STUTTER, FOLD, WALK_PLUS):
            m.call(sym['playhead_seq_set'], KIT_INDEX, t, mode)
            shown = []
            for i in range(6 * L):
                k = i % L
                m.w8(LAST + t, (k - 1) % L if i else 0)
                m.w8(STEP + t, k)
                regs = {r: 0 for r in D + A}
                regs[D[0]] = 13
                m.run(0x4008d1de, 0x4008d24a, regs)
                ui = m.u.mem_read(UI_STEP + t, 1)[0]
                if i:
                    assert m.u.mem_read(UI_LAST + t, 1)[0] == shown[-1]
                # Six native ticks per ordinary sixteenth: the remaining
                # tick copies must hit the pair cache without advancing.
                before_ui = bytes(m.u.mem_read(UI_LAST, 26))
                for _ in range(5):
                    m.run(0x4008d1de, 0x4008d24a, regs)
                    assert bytes(m.u.mem_read(UI_LAST, 26)) == before_ui
                regs = {r: 0 for r in D + A}
                regs.update({D[0]: PATTERN, D[6]: t, A[6]: STEP + t, D[5]: 0x5555})
                for site in (0x4008cf78, 0x4008cf8c):
                    _, stack = m.run(site, site + 8, regs)
                    assert struct.unpack_from('>I', stack, 0)[0] == ui, (mode, i, ui)
                regs.update({D[2]: t, A[0]: PATTERN, D[0]: k})
                _, stack = m.run(0x4008c27c, 0x4008c286, regs)
                assert struct.unpack_from('>I', stack, 8)[0] == ui, (mode, i, ui)
                m.w8(LAST + t, k)
                shown.append(m.call(0x4008b7ac, t))
                assert shown[-1] == ui
            m.call(sym['playhead_seq_set'], KIT_INDEX, t, FORWARDS)
    return n


ICONS = {BACKWARDS: [".......", "..x....", ".x.....", "xxxxxxx", ".x.....", "..x....", "......."],
         PENDULUM: [".........", "..x...x..", ".x.....x.", "xxxxxxxxx", ".x.....x.", "..x...x..", "........."],
         RANDOM: [".xxxxx.", "x.....x", "x.x.x.x", "x..x..x", "x.x.x.x", "x.....x", ".xxxxx."],
         WALK: ["....xx.", ".......", ".xx....", ".......", "....xx.", ".......", ".xx...."],
         ANCHOR: ["...x...", "..xxx..", "...x...", "...x...", "x..x..x", ".x.x.x.", "..xxx.."],
         STUTTER: [".......", "x.x.x..", "x.x.xx.", "x.x.xxx", "x.x.xx.", "x.x.x..", "......."],
         FOLD: ["x.....x", ".x...x.", "..x.x..", "...x...", "..x.x..", ".x...x.", "x.....x"],
         WALK_PLUS: [".xx......", "......x..", "...xx..x.", ".....xxxx", ".xx....x.", "......x..", "...xx...."]}
FAKE = 0x51000000          # the UI controller, the MainScreen, the app, a font


def run_header(stock_path, bank_path):
    """playhead_draw (DRAW | VIEW) on a fake MainScreen: the icon of the active
    track's mode, its pixels inverted (XOR) and nothing else, two pixels left of
    where the firmware puts the metronome (x 117 - the tempo's width), rows
    56..62; nothing for FORWARD, for another view or a MainScreen not on top. The tempo's whole
    BPM comes from the stock 4013273e; the font is fixed width (5 + 1)."""
    from unicorn import UC_HOOK_CODE
    main = syx.Syx.load(stock_path).section(3)
    bank, sym = bank_image(bank_path)
    m = Machine(main, bank, sym)
    u = m.u
    u.mem_map(FAKE, 0x10000)
    u.mem_map(0x443d0000, 0x10000)                    # the header font object
    ui, screen, app, font = FAKE, FAKE + 0x100, FAKE + 0x200, FAKE + 0x300
    m.w32(ui + 0x40 + 0x1c, FAKE + 0x400)            # the top opaque node
    m.w32(FAKE + 0x400 + 8, screen)
    m.w32(screen, 0x401c849c)
    m.w32(0x443db1f0 + 4, font)
    m.w32(font, 5)
    m.w32(font + 8, 0)
    state = {'track': 0, 'fills': []}

    def hook(uc, at, size, user):
        stub = {0x4016e044: ui, 0x4016e9c8: app, 0x4001ff74: None, 0x400f7fb6: 0}
        if at not in stub:
            return
        sp = uc.reg_read(UC_M68K_REG_A7)
        ret, *args = struct.unpack('>7I', bytes(uc.mem_read(sp, 28)))
        if at == 0x400f7fb6:
            state['fills'].append(tuple(v - (1 << 32) if v >= 1 << 31 else v for v in args[1:6]))
        value = state['track'] if at == 0x4001ff74 else stub[at]
        uc.reg_write(UC_M68K_REG_D0, value)
        uc.reg_write(UC_M68K_REG_A7, sp + 4)
        uc.reg_write(UC_M68K_REG_PC, ret)
    u.hook_add(UC_HOOK_CODE, hook)
    m.song(0, 16, [16] * TRACKS)
    n = 0
    for bpm, digits in ((120, 3), (75, 2), (300, 3)):
        m.w32(0x402d7360, bpm * 120)
        width = 6 * (digits + 2) - 1
        for mode in range(9):
            t = 12 if mode % 2 else 0
            m.call(sym['playhead_seq_set'], KIT_INDEX, 1, PENDULUM)           # keeps the sites in
            m.call(sym['playhead_seq_set'], KIT_INDEX, t, mode)
            state.update(track=t, fills=[])
            m.call(sym['playhead_draw'], screen, 0x7000, 0)
            if mode == FORWARDS:
                assert state['fills'] == [], state['fills']
                continue
            x = 115 - width - len(ICONS[mode][0])                     # its right column 2 left of the metronome
            lit = {(f[0] - x, 62 - f[1]) for f in state['fills']}
            assert all(f[0] == f[2] and f[1] == f[3] and f[4] == -1 for f in state['fills']), state['fills']
            want = {(c, r) for r, row in enumerate(ICONS[mode]) for c, ch in enumerate(row) if ch == 'x'}
            assert lit == want, (mode, sorted(lit ^ want))
            assert 117 - width - (x + len(ICONS[mode][0])) == 2       # two free columns before the metronome
            rows = {r for c, r in lit}
            assert min(rows) + max(rows) == 6                          # centred in rows 56..62
            n += 1
    # Another view's layer, the MainScreen not on top, every track FORWARD: nothing.
    state.update(track=0, fills=[])
    m.call(sym['playhead_seq_set'], KIT_INDEX, 0, BACKWARDS)
    m.w32(FAKE + 0x500, 0x401c9910)
    m.call(sym['playhead_draw'], FAKE + 0x500, 0x7000, 0)
    assert state['fills'] == []
    m.w32(screen, 0x401c9910)
    m.call(sym['playhead_draw'], screen, 0x7000, 0)
    assert state['fills'] == []
    m.w32(screen, 0x401c849c)
    m.call(sym['playhead_draw'], screen, 0x7000, 0)
    assert state['fills']
    state['fills'] = []
    for t in range(TRACKS):
        m.call(sym['playhead_seq_set'], KIT_INDEX, t, FORWARDS)
    m.call(sym['playhead_draw'], screen, 0x7000, 0)
    assert state['fills'] == []
    return n


SOUND_META, KIT_ENCODE, KIT_DECODE = 21, 5, 6
COPY, NEW, FREE, RELOCATE, PROJECT_NEW = 1, 2, 3, 4, 11
CONTAINER, KIT_SLOTS, KIT_SLOT, RECORD = 0x43c0dfc0, 0x2e0200, 0x1600, 0x15e0
SAVE_BUFFER = 0x40600554
BUFFER = 0x52000000        # a kit outside the live array (undo, clipboard)


def fnv(data):
    h = 0x811c9dc5
    for b in data:
        h = ((h ^ b) * 0x1000193) & 0xffffffff
    return h


def run_persist(stock_path, bank_path):
    """persist.c through the Sound metadata bus as LFO3 publishes it: the modes
    of a pattern's kit go into its slot's free tail (15e0h) on KIT_ENCODE and
    come back on KIT_DECODE; unknown slots are never written and give FORWARD;
    kit copies carry the modes, through a buffer too, and a changed buffer
    gives FORWARD; NEW clears a kit; PROJECT_NEW clears every slot's record;
    a damaged record gives FORWARD; the sites follow the modes; a kit's modes
    play only for its pattern (the next pattern's kit at 43bd7444)."""
    from unicorn import UC_HOOK_CODE
    main = syx.Syx.load(stock_path).section(3)
    bank, sym = bank_image(bank_path)
    m = Machine(main, bank, sym)
    u = m.u
    for at, size in ((0x41ad0000, 0x100000), (0x43c00000, 0x400000), (0x405f0000, 0x20000), (BUFFER, 0x10000)):
        u.mem_map(at, size)

    def hook(uc, at, size, user):
        if at in (0x40001980, 0x40001aa2):                    # the native mutex: one task here
            sp = uc.reg_read(UC_M68K_REG_A7)
            uc.reg_write(UC_M68K_REG_PC, struct.unpack('>I', bytes(uc.mem_read(sp, 4)))[0])
            uc.reg_write(UC_M68K_REG_A7, sp + 4)
    u.hook_add(UC_HOOK_CODE, hook)

    def kit(k):
        return KITS + k * KIT_BYTES

    def slot(k):
        return CONTAINER + KIT_SLOTS + k * KIT_SLOT

    def meta(kind, a, b, n):
        m.u.mem_write(BUFFER + 0x8000, struct.pack('>4I', kind, a, b, n))
        return m.call(sym['playhead_native'], SOUND_META, BUFFER + 0x8000, 0)

    def modes(k):
        return list(m.u.mem_read(sym['playhead_modes'] + 16 * k, 13))

    def installed():
        return m.site(0x4008cf78)[:2] == bytes([0x4e, 0xf9])

    m.song(1, 16, [16] * TRACKS)
    want = [0] * 13
    want[:9] = range(9)
    want[3], want[9] = PENDULUM, RANDOM
    want[12] = WALK
    for t in range(TRACKS):
        assert m.call(sym['playhead_seq_set'], 7, t, want[t]) == 0
    assert installed()
    # Encode into a known slot: the record.
    u.mem_write(slot(7) + RECORD, bytes([0xaa]) * 32)
    meta(KIT_ENCODE, slot(7), kit(7), CONTAINER)
    rec = bytes(u.mem_read(slot(7) + RECORD, 24))
    assert rec[:4] == b'PLH1' and list(rec[4:17]) == want and rec[17:20] == bytes(3), rec.hex()
    assert struct.unpack('>I', rec[20:])[0] == fnv(rec[:20])
    assert bytes(u.mem_read(slot(7) + RECORD + 24, 8)) == bytes([0xaa]) * 8   # nothing past it
    # The save buffer is known; an unknown buffer is never written.
    meta(KIT_ENCODE, SAVE_BUFFER, kit(7), 0)
    assert bytes(u.mem_read(SAVE_BUFFER + RECORD, 4)) == b'PLH1'
    u.mem_write(BUFFER + RECORD, b'\x55' * 24)
    meta(KIT_ENCODE, BUFFER, kit(7), 0)
    assert bytes(u.mem_read(BUFFER + RECORD, 24)) == b'\x55' * 24
    # Decode into another live kit: its modes; from an unknown buffer: FORWARD.
    meta(KIT_DECODE, kit(9), slot(7), CONTAINER)
    assert modes(9) == want
    meta(KIT_DECODE, kit(9), BUFFER, 0)
    assert modes(9) == [0] * 13
    # A damaged record gives FORWARD.
    u.mem_write(slot(7) + RECORD + 6, b'\x01')
    meta(KIT_DECODE, kit(9), slot(7), CONTAINER)
    assert modes(9) == [0] * 13
    # A well-checksummed unknown ID is refused; legacy 0..4 still decode.
    for ids, expected in (([9] + want[1:], [0] * 13), ([t % 5 for t in range(13)], [t % 5 for t in range(13)])):
        record = bytearray(rec)
        record[4:17] = bytes(ids)
        struct.pack_into('>I', record, 20, fnv(record[:20]))
        u.mem_write(slot(7) + RECORD, bytes(record))
        meta(KIT_DECODE, kit(9), slot(7), CONTAINER)
        assert modes(9) == expected
    meta(NEW, kit(9), 0, KIT_BYTES)
    # Copies: live to live, live to a buffer and back, a changed buffer.
    meta(COPY, kit(10), kit(7), KIT_BYTES)
    assert modes(10) == want
    u.mem_write(BUFFER, bytes(range(256)) * 23)
    meta(COPY, BUFFER, kit(10), KIT_BYTES)
    meta(NEW, kit(10), 0, KIT_BYTES)
    assert modes(10) == [0] * 13
    meta(COPY, kit(10), BUFFER, KIT_BYTES)
    assert modes(10) == want
    meta(COPY, BUFFER, kit(7), KIT_BYTES)
    u.mem_write(BUFFER + 100, b'\x01')                         # the buffer changed since
    meta(COPY, kit(10), BUFFER, KIT_BYTES)
    assert modes(10) == [0] * 13
    meta(COPY, kit(10), kit(7), 0x123)                          # not a kit: nothing
    assert modes(10) == [0] * 13
    # Encoding a kit with every track FORWARD clears the record.
    meta(KIT_ENCODE, slot(7), kit(10), CONTAINER)
    assert bytes(u.mem_read(slot(7) + RECORD, 24)) == bytes(24)
    # PROJECT_NEW clears every slot's record.
    meta(KIT_ENCODE, slot(0), kit(7), CONTAINER)
    meta(KIT_ENCODE, slot(127), kit(7), CONTAINER)
    meta(PROJECT_NEW, CONTAINER, 0, 0)
    assert bytes(u.mem_read(slot(0) + RECORD, 24)) == bytes(24) == bytes(u.mem_read(slot(127) + RECORD, 24))
    # A kit's modes play only for its pattern; the next pattern's kit is 43bd7444.
    m.w32(CURRENT_KIT, kit(7))
    assert m.call(sym['playhead_step'], 3, 2, PATTERN, 1) == 2         # pendulum from 2
    m.w32(CURRENT + 8, PATTERN + 0x100)
    m.w32(CURRENT_KIT + 8, kit(9))
    assert m.call(sym['playhead_step'], 3, 2, PATTERN + 0x100, 1) == 2  # kit 9: FORWARD
    m.w32(CURRENT_KIT + 8, kit(7))
    u.mem_write(PATTERN + 0x100 + 0x21379, b'\x01')
    u.mem_write(PATTERN + 0x100 + 0x499 * 12 + 969, struct.pack('>h', 16))
    assert m.call(sym['playhead_step'], 12, 2, PATTERN + 0x100, 1) == 2  # walk starts at its count
    # The sites go out when no kit has a mode left.
    meta(NEW, kit(7), 0, KIT_BYTES)
    assert modes(7) == [0] * 13 and not installed()
    meta(KIT_DECODE, kit(8), SAVE_BUFFER, 0)
    assert modes(8) == want and installed()
    return 1


def run_key(stock_path, bank_path):
    """Real selector: cancel, commit, context ownership, capture and cached blit.

    Only the native app/UI getters, held-key lookup, mutex and kitChanged
    boundary are doubled. Native Bitmap construction and masked blit execute.
    """
    from unicorn import UC_HOOK_CODE, UC_HOOK_MEM_WRITE
    main = syx.Syx.load(stock_path).section(3)
    bank, sym = bank_image(bank_path)
    # The actual bank must dispatch global modal input before other optional
    # UI modules. Machine-owned VIEW subscribers can keep their prefix.
    report = json.loads(Path(str(bank_path) + '.json').read_text())
    for event_id in (2, 3):
        subscribers = []
        for module in report['modules']:
            _, _, subs, count = struct.unpack_from('>4I', bank, sym[module]-BANK)
            if any(struct.unpack_from('>I', bank, subs-BANK+8*i)[0] == event_id for i in range(count)):
                subscribers.append(module)
        assert subscribers[0] == 'playhead_module', (event_id, subscribers)
    m = Machine(main, bank, sym)
    u = m.u
    u.mem_map(0x40a70000, 0x10000)
    u.mem_map(FAKE, 0x10000)
    app_model = 0x40a79000  # application model must be in native DDR
    m.w32(0x444e23f4, app_model)
    m.w32(app_model, 0x401c5708)
    kit_active = app_model + 0x130
    m.w32(kit_active, 0x401c3694)
    m.w32(kit_active + 16, KITS)
    ui, screen, app = FAKE, FAKE + 0x100, FAKE + 0x200
    event, bitmap, pixels, mask = FAKE + 0x800, FAKE + 0x5000, FAKE + 0x6000, FAKE + 0x7000
    m.w32(ui + 0x40 + 0x1c, FAKE + 0x400)
    m.w32(FAKE + 0x400 + 8, screen)
    m.w32(screen, 0x401c849c)
    controller = ui + 0x40
    # Real singleton getter and native compose gate: root+20h is NOT the
    # controller's dirty byte. The previous UI double hid this regression.
    m.w32(0x444e2334, ui)
    m.w8(ui + 0x20, 0xa5)
    state = {'track': 4, 'changed': [], 'held': True, 'writes': 0, 'blits': 0, 'instructions': 0}

    def hook(uc, at, size, user):
        state['instructions'] += 1
        if at == 0x400f8f70:
            state['blits'] += 1
        stub = {0x40001980: 0, 0x40001aa2: 0, 0x40102f40: None, 0x4016e9c8: app,
                0x4001ff74: None, 0x40172414: 0}
        if at not in stub:
            return
        sp = uc.reg_read(UC_M68K_REG_A7)
        ret, *args = struct.unpack('>4I', bytes(uc.mem_read(sp, 16)))
        if at == 0x40172414:
            state['changed'].append(tuple(args))
        value = state['track'] if at == 0x4001ff74 else int(state['held']) if at == 0x40102f40 else stub[at]
        uc.reg_write(UC_M68K_REG_D0, value)
        uc.reg_write(UC_M68K_REG_A7, sp + 4)
        uc.reg_write(UC_M68K_REG_PC, ret)

    def write_hook(uc, access, address, size, value, user):
        state['writes'] += 1
    u.hook_add(UC_HOOK_CODE, hook)
    u.hook_add(UC_HOOK_MEM_WRITE, write_hook, begin=sym['selector_pixels'], end=sym['selector_pixels']+1023)
    m.song(1, 16, [16] * TRACKS)
    u.mem_write(mask, b'\xff' * 1024)
    m.call(0x400f72be, bitmap, 128, 64, pixels, mask)
    selector = sym['playhead_selector']

    def field(offset):
        return struct.unpack('>I', u.mem_read(selector + offset, 4))[0]

    def send(key, flags, expected=1):
        m.w32(event + 12, key)
        m.w32(event + 16, flags)
        assert m.call(sym['playhead_key'], 0, event) == expected, (key, flags)

    def press(key):
        m.w8(controller + 0x20, 0)
        send(key, 1)
        assert m.call(0x4010095c, controller) & 255 == 1, ('native compose gate not requested', key)
        assert u.mem_read(ui + 0x20, 1) == b'\xa5', 'root owner must not be modified'
        send(key, 0x10)

    def mode(t=4, k=KIT_INDEX):
        return m.u.mem_read(sym['playhead_modes'] + 16 * k + t, 1)[0]

    def draw():
        u.mem_write(pixels, b'\xaa' * 1024)
        state['writes'] = 0
        state['instructions'] = 0
        m.call(sym['playhead_selector_draw'], bitmap, controller)
        return bytes(u.mem_read(pixels, 1024))

    def lit(frame, x, y):
        return bool(struct.unpack_from('>I', frame, 8*x+4*(y//32))[0] & (0x80000000 >> (y % 32)))

    # Ordinary arrows and other keys stay native while the selector is closed.
    state['held'] = False
    send(14, 1, 0); send(14, 0x10, 0); send(20, 1, 0)
    assert m.call(sym['playhead_selector_enc'], 0, event) == 0
    assert draw() == b'\xaa' * 1024 and state['writes'] == state['blits'] == 0
    state['held'] = True
    send(14, 1)
    assert m.call(0x4010095c, controller) & 255 == 1, 'opening must reach the real compose gate'
    assert u.mem_read(ui + 0x20, 1) == b'\xa5', 'root owner was clobbered'
    assert field(0) == 1 and field(12) == FORWARDS and mode() == FORWARDS and not state['changed']
    send(14, 9)  # held opening arrow must not navigate or apply
    assert field(12) == FORWARDS
    send(14, 0x10)
    state['held'] = False
    send(2, 0x10, 0)  # TRK began before the modal: its release stays native
    frame = draw()
    first_draw_cost = state['instructions']
    assert state['writes'] > 0
    # All nine icons, including FORWARD, rendered with selection inversion.
    forward_icon = ['.......', '....x..', '.....x.', 'xxxxxxx', '.....x.', '....x..', '.......']
    for mode_id in range(9):
        x = 8 if mode_id < 5 else 66
        top = 48 - 8 * (mode_id if mode_id < 5 else mode_id - 5)
        icon = forward_icon if mode_id == 0 else ICONS[mode_id]
        left = x + 3 + (9-len(icon[0]))//2
        for r, row in enumerate(icon):
            for c, ch in enumerate(row):
                assert lit(frame, left+c, top-r) == ((ch == 'x') != (mode_id == 0)), (mode_id, r, c)
        assert any(lit(frame, col, top-1-r) != (mode_id == 0)
                   for col in range(x+16, x+47) for r in range(5)), ('label', mode_id)
    # The opaque box is centered; every pixel outside its mask stays native.
    for x in range(128):
        for y in range(64):
            if not (4 <= x <= 123 and 2 <= y <= 61):
                assert lit(frame, x, y) == bool(0xaa & (0x80 >> (y % 8)))
    assert all(not lit(frame, x, 3) for x in range(5, 123)), 'footer must not touch the border'
    assert draw() == frame and state['writes'] == 0  # unchanged frame: only one blit
    cached_draw_cost = state['instructions']
    assert m.call(sym['playhead_selector_enc'], 0, event) == 1
    send(15, 1)
    send(15, 9)  # navigation repeat works after the opener was released
    send(15, 0x10)
    assert field(12) == PENDULUM and mode() == FORWARDS and not state['changed']
    assert draw() != frame and state['writes'] > 0
    cursor_draw_cost = state['instructions']
    assert state['writes'] <= 216, 'only the old and new eight-pixel rows may be inverted'
    assert cursor_draw_cost < first_draw_cost // 3, ('cursor rebuild too expensive', first_draw_cost, cursor_draw_cost)
    press(13)
    assert not field(0) and mode() == FORWARDS and not state['changed']
    assert draw() == b'\xaa' * 1024
    # Confirming the current option makes no kit edit or persistent write.
    state['held'] = True
    press(15); press(12)
    assert not field(0) and mode() == FORWARDS and not state['changed']
    # The incremental highlight must equal a clean native-rasterized frame
    # across every mode and the column/end wrap, in both directions.
    def highlighted(mode_id):
        expected = bytearray(frame)
        for current in (0, mode_id) if mode_id else ():
            x = 8 if current < 5 else 66
            top = 48-8*(current if current < 5 else current-5)
            for col in range(x, x+54):
                for y in range(top-6, top+2):
                    expected[8*col+y//8] ^= 0x80 >> (y % 8)
        return bytes(expected)
    press(14)
    assert draw() == frame
    for key in (15, 14):
        for _ in range(18):
            press(key)
            assert draw() == highlighted(field(12)), ('incremental highlight', key, field(12))
            assert state['writes'] <= 216
    press(13)
    # 1.3: LEFT / RIGHT move to the other column on the same row; RIGHT from
    # WALK (row 4, no neighbour) goes to WALK+. The highlight stays incremental.
    press(14)
    assert field(12) == FORWARDS
    for key, expected in ((17, 5), (17, 5), (16, 0), (16, 0), (15, 1), (17, 6), (15, 7), (16, 2),
                          (15, 3), (15, 4), (17, 8), (16, 3)):
        press(key)
        assert field(12) == expected, ('column move', key, expected, field(12))
        assert draw() == highlighted(field(12)) and state['writes'] <= 216
    assert mode() == FORWARDS and not state['changed']
    press(13)
    # Commit each option with YES, once, to the track captured at opening.
    for expected in range(1, 9):
        press(14)
        assert field(12) == mode()
        press(15)
        assert field(12) == expected and mode() == expected-1
        press(12)
        assert not field(0) and mode() == expected
        assert len(state['changed']) == expected
        assert state['changed'][-1] == (kit_active, KIT_INDEX, 0)
    press(14); press(15)
    assert field(12) == FORWARDS and mode() == WALK_PLUS
    press(13)
    assert mode() == WALK_PLUS
    # 1.3: TRK, and a trig key while TRK is held, stay native with the selector
    # open (the stock track change); the selector then follows the new track,
    # its highlight on that track's mode, nothing applied. A lone trig key
    # stays modal.
    modes_at = sym['playhead_modes'] + 16 * KIT_INDEX
    u.mem_write(modes_at + 5, bytes([PENDULUM]))
    changes = len(state['changed'])
    press(14)
    assert field(0) and field(8) == 4 and field(12) == WALK_PLUS
    send(2, 1, 0); send(24, 1, 0); send(24, 0x10, 0); send(2, 0x10, 0)
    state['track'] = 5
    frame5 = draw()
    assert field(0) and field(8) == 5 and field(12) == PENDULUM and frame5 != b'\xaa' * 1024
    state['held'] = False
    send(24, 1); send(24, 0x10)
    state['held'] = True
    assert field(0) and field(8) == 5
    press(15); press(13)
    assert not field(0) and mode() == WALK_PLUS and mode(5) == PENDULUM and len(state['changed']) == changes
    u.mem_write(modes_at + 5, bytes([FORWARDS]))
    # On a track without modes (past the FX track) it closes; YES there applies nothing.
    press(14); state['track'] = 13; press(12)
    assert not field(0) and mode() == WALK_PLUS and len(state['changed']) == changes
    state['track'] = 4
    # Pattern or view changes dismiss the stale selector, with no selection.
    press(14); m.w32(CURRENT_KIT, KITS + (KIT_INDEX+1)*KIT_BYTES)
    assert draw() == b'\xaa' * 1024 and not field(0)
    m.w32(CURRENT_KIT, KITS + KIT_INDEX*KIT_BYTES)
    press(14);m.w32(screen,0x401c9910)
    assert draw() == b'\xaa' * 1024 and not field(0)
    send(15,1,0);send(15,0x10,0)  # another opaque view owns its own input
    m.w32(screen,0x401c849c)
    # Modal consumes unrelated presses and their releases, even after closing.
    press(14); send(20, 1); press(13); send(20, 0x10)
    # FX track owns its own choice, label, icon and highlighted row.
    state['track'] = 12
    press(15)
    for _ in range(FOLD): press(15)
    preview = draw()
    preview_dir = Path('out/playhead-selector-preview')
    preview_dir.mkdir(parents=True, exist_ok=True)
    (preview_dir/'selector.raw').write_bytes(preview)
    press(12)
    assert mode(12) == FOLD and mode() == WALK_PLUS
    # A bad native KitActive object is never called.
    m.w32(kit_active + 16, 0)
    n = len(state['changed'])
    press(14); press(15); press(12)
    assert mode(12) == WALK_PLUS and len(state['changed']) == n
    # Refused patches leave the old mode, show an error and allow NO to exit.
    m.call(sym['playhead_seq_set'], KIT_INDEX, 4, FORWARDS)
    m.call(sym['playhead_seq_set'], KIT_INDEX, 12, FORWARDS)
    original = m.site(SITES[0])
    u.mem_write(SITES[0], b'\x4e\x71' + original[2:])
    press(14); press(15); press(12)
    assert field(0) and field(20) == 1 and mode(12) == FORWARDS
    press(13)
    assert not field(0) and len(state['changed']) == n
    (preview_dir/'latency-validation.json').write_text(json.dumps(dict(
        native_ui_getter=True, native_compose_gate=True, root_owner_preserved=True,
        full_draw_instructions=first_draw_cost, cached_draw_instructions=cached_draw_cost,
        cursor_draw_instructions=cursor_draw_cost, incremental_highlight_cases=36,
        key_commit_boundary='native kitChanged is doubled; no hardware latency measurement'), indent=2)+'\n')
    return 1


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bank', type=Path, required=True)
    p.add_argument('--stock', type=Path, required=True)
    a = p.parse_args()
    n = run(a.stock, a.bank)
    print(f'PASS: playhead-st step sites: {n} cases forwards equal stock, backwards mirrored; '
          'sites in and out; a changed context refused')
    n = run_modes(a.stock, a.bank)
    print(f'PASS: playhead-st modes on {n} tracks: all nine modes as specified, '
          'one step per count for the UI copy, the trig and the LEDs')
    run_key(a.stock, a.bank)
    print('PASS: playhead-st selector: no live edits; YES commits, NO cancels; captured track/kit; '
          'nine icons and labels, native masked blit, cached frames, key/encoder capture and safe refusal')
    run_persist(a.stock, a.bank)
    print('PASS: playhead-st persistence: kit encode/decode in the slot tail, copies, NEW, '
          'PROJECT_NEW, damaged records, per-pattern kits, sites following the modes')
    n = run_header(a.stock, a.bank)
    print(f'PASS: playhead-st header: {n} icons left of the metronome for three tempos, '
          'none for FORWARD or another view')


if __name__ == '__main__':
    main()
