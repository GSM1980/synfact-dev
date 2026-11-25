# SPDX-License-Identifier: GPL-2.0-or-later
"""SY WAVETABLE's SYN pages (mods/wavetable-st/page.c), executed in Unicorn on
the stock OS 1.42 MAIN image: the bank's own wt_draw/wt_idle with the native
fill, pixel, text routines and fonts. Fixture boundaries: the parameter
values and locks (40039160), the track (4001ff74), the page indicator
(4003a1c2) and the UI API (a real wavetable, from the converter, as
wavetable_level gives it; AUDIO's cursor records). Optional PNGs are enlarged
LCD captures. No device IO.

    python tests/test_wavetable_st_page_emu.py --stock "$SYNTAKT_STOCK" --bank <bank>.smz4 [--out <folder>]
"""
import argparse
import math
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader import formats  # noqa: E402
from elekloader.sdk import elf  # noqa: E402
from elekloader.sdk.syntakt import wavetable as W  # noqa: E402
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE  # noqa: E402
from unicorn.m68k_const import *  # noqa: E402,F403

BASE_ID = 0x1f9 + 8 * 32
API, HOOKS, CURSOR = 0x49000000, 0x4f001000, 0x49000400


def table():
    """64 waves, sine to saw to square, through the real converter."""
    src = []
    for w in range(64):
        m = w / 63
        for i in range(2048):
            x = i / 2048
            sine, saw, sq = math.sin(2 * math.pi * x), 2 * x - 1, 1.0 if x < 0.5 else -1.0
            src.append((1 - 2 * m) * sine + 2 * m * saw if m < 0.5 else (2 - 2 * m) * saw + (2 * m - 1) * sq)
    _, body, _ = W.convert([src], 'view')
    return body, 64


def run(stock, bank, out):
    firmware, device, release = formats.load(stock)
    assert release.version == '1.42'
    u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
    u.reg_write(UC_M68K_REG_SR, 0x2700)
    for at, n in [(0x40000000, 0x400000), (0x44000000, 0x1000000), (0x47000000, 0x100000),
                  (0x48000000, 0x20000), (0x49000000, 0x1000000), (0x4f000000, 0x10000)]:
        u.mem_map(at, n)
    u.mem_write(0x40000400, formats.main_image(firmware, device))
    raw = bank.read_bytes()
    n, rels = struct.unpack_from('>2I', raw, 52)
    image = bytearray(raw[80 + 4 * rels:80 + 4 * rels + n])
    for offset in struct.unpack_from('>%dI' % rels, raw, 80):
        struct.pack_into('>I', image, offset, struct.unpack_from('>I', image, offset)[0] + 0x47000000)
    u.mem_write(0x47000000, bytes(image))
    symbols = {s.name: 0x47000000 + s.value for s in elf.Elf.load(bank.with_suffix('.work') / 'main.elf').symbols if s.name}

    def w(at, *values):
        u.mem_write(at, struct.pack('>%dI' % len(values), *values))

    def read(at):
        return struct.unpack('>I', u.mem_read(at, 4))[0]

    def args(k):
        return struct.unpack('>%dI' % k, u.mem_read(u.reg_read(UC_M68K_REG_A7) + 4, 4 * k))

    def ret(value=0):
        u.reg_write(UC_M68K_REG_D0, value & 0xffffffff)
        if state.get('stub'):
            return                      # the fixture's own API stubs are a real rts
        sp = u.reg_read(UC_M68K_REG_A7)
        u.reg_write(UC_M68K_REG_A7, sp + 4)
        u.reg_write(UC_M68K_REG_PC, read(sp))

    body, waves = table()
    level5 = body[2 * W.offset(waves, 5, 0):2 * W.offset(waves, 5, 0) + 2 * waves * 64]
    state = dict(values={}, locked=set(), cursor=None, level_calls=0, tables={64: (level5, waves)}, track=2)
    heap = [0x49100000]
    texts = []

    def hook(uc, at, size, _):
        state['steps'] = state.get('steps', 0) + 1
        # The UI API stubs (HOOKS..) are each an rts: the hook only sets D0 and
        # the rts returns. Moving PC from a hook, the stub's own instruction
        # could still run and pop twice (seen as a repeated call).
        state['stub'] = HOOKS <= at < HOOKS + 64
        if at in (0x4010394c, 0x40103952, 0x401036d0):          # native allocation (strings)
            k, = args(1)
            result = heap[0]
            heap[0] += (k + 31) & ~31
            ret(result)
        elif at in (0x40103958, 0x4010395e, 0x401038d6):
            ret()
        elif at == 0x40039160:                                     # value and lock of a parameter
            view, pid, locked = args(3)
            index = pid - BASE_ID
            assert 0 <= index < 17, hex(pid)
            u.mem_write(locked, bytes((1 if index in state['locked'] else 0,)))
            ret(state['values'].get(index, 0))
        elif at == 0x4001ff74:
            ret(state['track'])
        elif at == 0x4003a1c2:                                     # native page indicator: drawn by the view
            view, ctx, pages, page, x = args(5)
            assert pages == 2 and page in (0, 1) and x == 7
            ret()
        elif at == 0x400f8bd0:
            # Text is a boundary: the native fonts are objects the boot
            # constructs, which this fixture does not run (as the ARP view
            # test). Record it; the PNG draws it with the test's own font.
            ctx, font, x, y, align, fmt, value = args(7)
            assert bytes(u.mem_read(fmt, 3)) == b'%s\x00' and font in (0x443db210, 0x443db230)
            data = bytearray()
            while u.mem_read(value + len(data), 1) != b'\x00':
                data.extend(u.mem_read(value + len(data), 1))
            texts.append((x, y, data.decode('ascii'), align))
            ret()
        elif at == HOOKS:                                          # wave_idle(slot, budget)
            slot, budget = args(2)
            assert budget == 0
            ret()
        elif at == HOOKS + 4:                                      # cursor(track, slot)
            track, slot = args(2)
            c = state['cursor']
            if c and c[0] == track and c[1] == slot:
                w(CURSOR, 0x44100000, waves * 4088, c[2], 1)
                ret(CURSOR)
            else:
                ret(0)
        elif at == HOOKS + 8:                                      # wavetable_level(slot, level, out, max)
            slot, level, dst, most = args(4)
            assert level == 5 and most == 64
            state['level_calls'] += 1
            entry = state['tables'].get(slot)
            if entry and entry[1] <= most:
                u.mem_write(dst, entry[0])
                ret(entry[1])
            else:
                ret(0)
        elif at == 0x400f8b68:                                     # plain text (the slot list)
            ctx, font, x, y, width_arg, value = args(6)
            data = bytearray()
            while u.mem_read(value + len(data), 1) != b'\x00':
                data.extend(u.mem_read(value + len(data), 1))
            texts.append((x, y, data.decode('ascii'), 0))
            ret()
        elif at == 0x400f9478:                                     # a font object's text font
            font, = args(1)
            ret(font)
        elif at == 0x40030622:                                     # the view's machine type
            ret(72)
        elif at == 0x400c3582:                                     # trigs held
            ret(state['trig'])
        elif at in (0x400c3062,):                                  # live REC
            ret(0)
        elif at == 0x400ffe22:                                     # invalidate
            state['invalidated'] += 1
            ret()
        elif at == 0x400f6866:                                     # an encoder event's delta
            ret(state.get('delta', 1))
        elif at == 0x40102f40:                                     # a key held (1: FUNC)
            key, = args(1)
            ret(1 if key == 1 and state.get('func') else 0)
        elif at == 0x400c2dd6:                                     # a lock edited during this trig hold
            ret(0)
        elif at == HOOKS + 12:                                     # sample_name(slot, out)
            slot, dst = args(2)
            name = state['names'].get(slot)
            if name:
                u.mem_write(dst, name.encode() + b'\x00')
            ret(1 if name else 0)
        elif at == HOOKS + 16:                                     # edit(view, id, delta, knob)
            v, pid, delta, knob = args(4)
            state['edits'].append((pid - BASE_ID, delta - (1 << 32) if delta >> 31 else delta, knob))
            ret()
        elif at == HOOKS + 20:                                     # clear(view, id, whole_page, knob)
            v, pid, whole, knob = args(4)
            state['clears'].append((pid - BASE_ID, whole, knob))
            ret()
        elif at == HOOKS + 24:                                     # sample_channels(slot)
            slot, = args(1)
            ret(2 if slot == 66 else 1)
        elif at == HOOKS + 28:                                     # sample_wavetable(slot)
            slot, = args(1)
            ret(64 if slot in (64, 67) else 0)
        elif at == HOOKS + 32:                                     # sample_read(slot, first, count, out)
            slot, first, count, dst = args(4)
            frames = {64: waves * 4088, 67: waves * 4088, 68: 2048 * 80, 5: 1000}.get(slot, 0)
            if not count:
                ret(frames)
            else:
                assert slot == 68 and first % 2048 == 0 and count == 2048, (slot, first, count)
                state['sample_reads'] += 1
                w0 = first // 2048                                 # a pulse whose width follows the wave
                data = [20000 if i < 2048 * (w0 + 4) // 88 else -20000 for i in range(2048)]
                u.mem_write(dst, struct.pack('>2048h', *data))
                ret(2048)
    u.hook_add(UC_HOOK_CODE, hook)

    def call(fn, *values):
        sp = 0x4801f000 - 4 * (len(values) + 1)
        w(sp, 0x4f000000, *values)
        u.reg_write(UC_M68K_REG_A7, sp)
        try:
            u.emu_start(symbols.get(fn, fn), 0x4f000000, count=20000000)
        except Exception as error:
            pc = u.reg_read(UC_M68K_REG_PC)
            near = max((v, k) for k, v in symbols.items() if v <= pc) if pc >= 0x47000000 else (0, 'firmware')
            raise RuntimeError((fn, hex(pc), near[1], hex(pc - near[0]), [hex(u.reg_read(r)) for r in
                                (UC_M68K_REG_A0, UC_M68K_REG_A1, UC_M68K_REG_A2, UC_M68K_REG_A7)])) from error
        assert u.reg_read(UC_M68K_REG_PC) == 0x4f000000, hex(u.reg_read(UC_M68K_REG_PC))
        return u.reg_read(UC_M68K_REG_D0)

    # UI API 9: version, wave, wave_idle, sample_name, cursor, context, edit, clear,
    # bind_input, bind_codec, host, sample_channels, wavetable_level, sample_wavetable.
    w(API, 9, 0, HOOKS, HOOKS + 12, HOOKS + 4, 0, HOOKS + 16, HOOKS + 20, 0, 0, 0, HOOKS + 24, HOOKS + 8, HOOKS + 28, HOOKS + 32)
    for k in range(9):
        u.mem_write(HOOKS + 4 * k, b'\x4e\x71\x4e\x75')
    state.update(trig=0, invalidated=0, edits=[], clears=[], sample_reads=0,
                 names={64: 'SINE SAW SQUARE', 65: 'KICK', 66: 'PAD', 67: 'HARMONICS'})
    w(0x443db21c, 0x402aa114)
    w(0x443db230, 0x402aa114)
    w(0x443db23c, 0x402aa114)
    bitmap, pixels, view = 0x49001000, 0x49002000, 0x49003000
    machine = symbols['wt_machine']
    # The SYN view the slot list checks: vtable, SYN class 14h, model, page, trig owner.
    w(view, 0x401ca37c)
    w(view + 0x88, 0x14)
    w(view + 0x74, 0x49003800)
    w(view + 0x6c, 0x49003900)
    w(view + 0x90, 0)
    w(symbols['wt_input_api'], API)

    widths = bytes(u.mem_read(0x402aa128, 256))         # the native font's widths (as the manager test)
    font = {
        'A': ['010', '101', '111', '101', '101'], 'B': ['110', '101', '110', '101', '110'],
        'C': ['011', '100', '100', '100', '011'], 'D': ['110', '101', '101', '101', '110'],
        'E': ['111', '100', '110', '100', '111'], 'H': ['101', '101', '111', '101', '101'],
        'I': ['111', '010', '010', '010', '111'], 'L': ['100', '100', '100', '100', '111'],
        'M': ['101', '111', '111', '101', '101'], 'N': ['110', '101', '101', '101', '101'],
        'O': ['010', '101', '101', '101', '010'], 'P': ['110', '101', '110', '100', '100'],
        'S': ['011', '100', '010', '001', '110'], 'T': ['111', '010', '010', '010', '010'],
        'U': ['101', '101', '101', '101', '111'], 'V': ['101', '101', '101', '101', '010'],
        'W': ['101', '101', '111', '111', '101'], 'Y': ['101', '101', '010', '010', '010'],
        '.': ['000', '000', '000', '000', '010'], '+': ['000', '010', '111', '010', '000'],
        '-': ['000', '000', '111', '000', '000'], ' ': ['000'] * 5,
        '0': ['111', '101', '101', '101', '111'], '1': ['010', '110', '010', '010', '111'],
        '2': ['110', '001', '010', '100', '111'], '3': ['110', '001', '010', '001', '110'],
        '4': ['101', '101', '111', '001', '001'], '5': ['111', '100', '110', '001', '110'],
        '6': ['011', '100', '111', '101', '111'], '7': ['111', '001', '010', '010', '010'],
        '8': ['111', '101', '111', '101', '111'], '9': ['111', '101', '111', '001', '110'],
        ':': ['000', '010', '000', '010', '000'], 'K': ['101', '101', '110', '101', '101'],
        'R': ['110', '101', '110', '101', '101'], 'Q': ['010', '101', '101', '110', '011'],
        'G': ['011', '100', '101', '101', '011'], 'F': ['111', '100', '110', '100', '100'],
        'X': ['101', '101', '010', '101', '101'], 'Z': ['111', '001', '010', '100', '111'],
        'J': ['001', '001', '001', '101', '010'], '&': ['010', '101', '010', '101', '011']}
    for ch in 'abcdefghijklmnopqrstuvwxyz':
        font[ch] = font.get(ch.upper(), ['111'] * 5)

    def width(text):
        return sum(widths[ord(c)] + 1 for c in text) - 1

    def overlay(on, t):
        shown = set(on)
        for x, y, text, align in t:
            left = x - width(text) // 2 if align == 2 else x
            for ch in text:
                for r, bits in enumerate(font.get(ch, ['111'] * 5)):
                    for c, bit in enumerate(bits):
                        if bit == '1':
                            shown ^= {(left + c, y + 4 - r)}
                left += widths[ord(ch)] + 1
        return shown

    def frame(page, name, listing=False):
        texts.clear()
        u.mem_write(pixels, bytes(1024))
        call(0x400f72be, bitmap, 128, 64, pixels, 0)
        before = state.get('steps', 0)
        call('wt_draw', API, machine, view, bitmap, page)
        state.setdefault('draw_steps', {})[name] = state.get('steps', 0) - before
        words = struct.unpack('>256I', u.mem_read(pixels, 1024))
        on = {(x, y) for x in range(128) for y in range(64) if words[2 * x + y // 32] & (1 << (31 - y % 32))}
        if listing:     # the list takes x 20..127 and the header row, as the SP list
            assert all(x >= 20 for x, y in on)
        else:
            assert all(19 <= x <= 124 and y <= 51 for x, y in on), sorted(p for p in on if not (19 <= p[0] <= 124 and p[1] <= 51))[:8]
            for x, y, text, align in texts:
                left = x - width(text) // 2 if align == 2 else x
                assert 19 <= left and left + width(text) <= 124 and 0 <= y <= 46, (x, y, text)
        if out:
            out.mkdir(parents=True, exist_ok=True)
            shown = overlay(on, texts)
            rows = bytearray()
            for y in reversed(range(64)):
                row = bytearray(b'\0')
                for x in range(128):
                    row.extend((bytes((218, 240, 235)) if (x, y) in shown else bytes((20, 26, 28))) * 6)
                rows.extend(row * 6)
            def chunk(kind, data):
                return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
            png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 768, 384, 8, 2, 0, 0, 0))
            (out / name).write_bytes(png + chunk(b'IDAT', zlib.compress(bytes(rows))) + chunk(b'IEND', b''))
        return on, list(texts)

    def labels(t):
        return [s for _, _, s, _ in t]

    # Page 1, osc 1: POS a quarter in, LOOP TRI on position, slot 65 holds the table.
    state['values'] = {0: 0x4000 + 12 * 256, 1: 0x6400, 2: 0x2000, 3: 64 << 8, 4: 0x5000, 5: 0x4000, 6: 0x6800, 7: 6 << 8}
    on, t = frame(0, 'wavetable-page1.png')
    assert labels(t)[:1] and {'LEV', 'POS', 'W', '65', '+12', 'SPD', 'A.LEV', 'A.POS', 'ANIM'} <= set(labels(t)), labels(t)
    wave = [p for p in on if 17 <= p[1] <= 41 and 20 <= p[0] <= 121]
    assert len({x for x, _ in wave}) >= 100, 'the wave covers its width'
    assert state['level_calls'] == 1
    # Only a new slot reads the card again.
    frame(0, 'wavetable-page1.png')
    assert state['level_calls'] == 1
    # The position follows AUDIO's cursor while the oscillator sounds: the
    # marker on the track moves to the last wave.
    marker = lambda points: sorted(x for x, y in points if y == 15 and (x + 1, 15) in points and (x + 2, 15) in points)
    before = marker(on)
    state['cursor'] = (2, 64, 63 * 2048)                # position: Q8 waves x 8 (audio.c)
    on2, _ = frame(0, 'wavetable-page1-moving.png')
    assert marker(on2) and before and marker(on2)[-1] > before[-1], (before, marker(on2))
    assert call('wt_idle', API, machine, view, 0) == 1 and call('wt_idle', API, machine, view, 0) == 0
    # Page 2, osc 2: DTUN, its own cursor record (track + 8), locks inverted.
    state['values'] = {8: 0x4000 - 5 * 256, 9: 0x7f00, 10: 0x7f00, 11: 64 << 8, 12: 0, 13: 0x2000, 14: 0x4000, 15: 9 << 8}
    state['cursor'] = (2 + 8, 64, 10 * 2048)
    unlocked, _ = frame(1, 'wavetable-page2.png')
    state['locked'] = {10, 15}                       # POS and ANIM of osc 2
    on, t = frame(1, 'wavetable-page2-locks.png')
    assert {'-5', 'LEV', 'POS', '65'} <= set(labels(t)), labels(t)
    def cell(points, x0, y0, x1, y1):
        return {(x, y) for x, y in points if x0 <= x <= x1 and y0 <= y <= y1}
    # A lock inverts its cell: POS (top row, third) and ANIM (bottom row, fourth);
    # LEV's cell stays as it was.
    assert cell(on, 71, 42, 96, 51) == {(x, y) for x in range(71, 97) for y in range(42, 52)} - cell(unlocked, 71, 42, 96, 51)
    assert cell(on, 97, 0, 120, 12) == {(x, y) for x in range(97, 121) for y in range(0, 13)} - cell(unlocked, 97, 0, 120, 12)
    assert cell(on, 45, 42, 68, 51) == cell(unlocked, 45, 42, 68, 51)
    # An empty slot: no card read finds it, the page says so.
    state['locked'] = set()
    state['values'][11] = 3 << 8
    on, t = frame(1, 'wavetable-empty.png')
    assert 'EMPTY SLOT' in labels(t) and '4' in labels(t)
    state['values'][11] = 5 << 8
    on, t = frame(1, 'wavetable-short.png')
    assert 'SAMPLE TOO SHORT' in labels(t)
    # A plain sample (80 waves of 2048): 64 of its waves, read once, drawn as a wavetable.
    state['values'][11] = 68 << 8
    state['values'][10] = 0x7f00
    state['cursor'] = None
    on, t = frame(1, 'wavetable-sample.png')
    assert state['sample_reads'] == 64 and '69' in labels(t)
    wave = [p for p in on if 17 <= p[1] <= 41 and 20 <= p[0] <= 121]
    assert len({x for x, _ in wave}) >= 100
    frame(1, 'wavetable-sample.png')
    assert state['sample_reads'] == 64, 'one read per slot'
    # Turning SLOT opens the pool list (the module's own encoder subscription),
    # as SAMP does on the SP machines: names, WT and ST tags, the hint row.
    slot_id = BASE_ID + 3
    w(view + 0x90, 0)
    state['values'] = {0: 0x4000, 1: 0x6400, 2: 0, 3: 64 << 8, 4: 0x4000, 5: 0x4000, 6: 0x4000, 7: 6 << 8}
    assert call('wt_input_encoder', view, 0x49004000, BASE_ID + 2) == 0 and not call('slot_list_active', view)
    assert call('wt_input_encoder', view, 0x49004000, slot_id) == 0      # the native edit still turns SLOT
    assert call('slot_list_active', view) == 1
    on, t = frame(0, 'wavetable-list.png', listing=True)
    rows = labels(t)
    assert '65:SINE SAW SQUARE' in rows and '66:KICK' in rows and '67:PAD' in rows and '68:HARMONICS' in rows, rows
    assert rows.count('WT') == 2 and rows.count('ST') == 1 and 'Press NO to revert' in rows, rows
    # The value moved on (native edit) and NO reverts it through the core's edit.
    state['values'][3] = 67 << 8
    event = 0x49004100
    w(event, 0, 0, 0, 13, 1)
    assert call('wt_input_key_pre', view, event, 1) == 1 and not call('slot_list_active', view)
    assert state['edits'][-1] == (3, (64 << 8) - (67 << 8), 3), state['edits']
    w(event, 0, 0, 0, 13, 0)                                           # NO released: swallowed too
    assert call('wt_input_key_pre', view, event, 1) == 1
    assert call('wt_input_key_pre', view, event, 1) == 0               # and only once
    # On page 2 its own SLOT (control 11) opens it; another control closes it.
    w(view + 0x90, 1)
    state['values'] = {8: 0x4000, 9: 0, 10: 0, 11: 66 << 8, 12: 0x4000, 13: 0x4000, 14: 0x4000, 15: 6 << 8}
    call('wt_input_key_post', view, event, 1)
    assert call('wt_input_encoder', view, 0x49004000, BASE_ID + 11) == 0 and call('slot_list_active', view) == 1
    on, t = frame(1, 'wavetable-list-page2.png', listing=True)
    assert '67:PAD' in labels(t)
    assert call('wt_input_encoder', view, 0x49004000, BASE_ID + 10) == 0 and not call('slot_list_active', view)
    # A p-lock: a trig held while SLOT turns; NO clears the new lock.
    state['trig'] = 1
    state['locked'] = set()
    call('wt_input_encoder', view, 0x49004000, BASE_ID + 11)
    state['locked'] = {11}
    state['values'][11] = 70 << 8
    w(event, 0, 0, 0, 13, 1)
    assert call('wt_input_key_pre', view, event, 1) == 1
    assert state['clears'][-1] == (11, 0, 3), state['clears']
    w(event, 0, 0, 0, 13, 0)
    call('wt_input_key_pre', view, event, 1)                          # its release
    state['trig'] = 0
    state['locked'] = set()
    # No native dial: the machine takes every knob's dial and draws nothing
    # (the firmware's dial reached past x 124 and stayed as a stray mark).
    dial = 0x49004200
    w(dial, 0, 0, 0, 0, 0, bitmap, 104, 0)
    u.mem_write(pixels, bytes(1024))
    for index in range(16):
        assert call(symbols['wt_ui'] and read(symbols['wt_ui'] + 20), API, machine, index, dial) == 1
    assert bytes(u.mem_read(pixels, 1024)) == bytes(1024)
    # FUNC + a knob sticks: one stop per 16 units of travel, from the value
    # to the next stop in the turning direction; the core's edit does it.
    w(view + 0x90, 0)
    state['values'] = {0: 0x4000 + 5 * 256, 1: 0x6400, 2: 0x1000, 3: 64 << 8, 4: 64 << 8, 5: 0x4000, 6: 0x4000, 7: 0}
    state['func'] = 1
    state['delta'] = 16
    edits = len(state['edits'])
    for index, delta, expected in ((0, 16, (12 - 5) * 256),          # TUNE: up to the octave
                                   (0, -16, -5 * 256),               # down to 0
                                   (2, 16, 4129 - 0x1000),           # POS: the first value on wave 8 of 64
                                   (4, 16, (68 << 8) - (64 << 8)),   # SPD: the next tempo division
                                   (1, 16, (127 << 8) - 0x6400)):    # LEV: 100 -> 127
        state['delta'] = delta
        assert call('wt_input_encoder', view, 0x49004000, BASE_ID + index) == 1
        assert state['edits'][-1] == (index, expected, index), (index, state['edits'][-1])
    state['delta'] = 5
    assert call('wt_input_encoder', view, 0x49004000, BASE_ID + 0) == 1 and len(state['edits']) == edits + 5  # under a stop's travel
    state['func'] = 0
    assert call('wt_input_encoder', view, 0x49004000, BASE_ID + 0) == 0 and len(state['edits']) == edits + 5  # native without FUNC
    state['delta'] = 1
    # A moving position redraws at most every other idle call.
    state['values'] = {0: 0x4000, 1: 0x6400, 2: 0, 3: 64 << 8, 4: 0x4000, 5: 0x4000, 6: 0x4000, 7: 0}
    state['cursor'] = (2, 64, 0)
    call('wt_idle', API, machine, view, 0)
    answers = []
    for k in range(1, 7):
        state['cursor'] = (2, 64, k * 2048)                           # a whole wave per call
        answers.append(call('wt_idle', API, machine, view, 0))
    assert answers in ([1, 0, 1, 0, 1, 0], [0, 1, 0, 1, 0, 1]), answers     # every other call, whatever the phase
    print('instructions per page draw (bank + native routines):', state['draw_steps'])
    print('PASS: SY WAVETABLE pages drawn by the bank on native OS 1.42 routines: Tonverk layout, '
          'labels and values, the wave over its width, one card read per slot, the moving position '
          'from each oscillator\'s cursor record, locks, an empty slot, nothing outside x 19..124 / y 0..51; '
          'the SLOT list on either page (names, WT/ST tags), NO reverting a value and a new p-lock')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--stock', type=Path, required=True)
    p.add_argument('--bank', type=Path, required=True)
    p.add_argument('--out', type=Path)
    a = p.parse_args()
    run(a.stock, a.bank, a.out)
