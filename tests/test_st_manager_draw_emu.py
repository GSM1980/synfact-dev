"""Execute the compiled manager and OS 1.42 list/bitmap routines offline.

Only allocation, synchronization, core services and manager attachment are fixture boundaries.
Native fonts, strings, view construction, row callbacks and selection paint
execute. Optional PNGs are enlarged nearest-neighbour LCD captures.
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader import formats
from elekloader.sdk import elf
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
from unicorn.m68k_const import *


def verify(stock, bank, out):
    firmware, device, release = formats.load(stock)
    assert release.version == '1.42'
    u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
    u.reg_write(UC_M68K_REG_SR, 0x2700)
    for at, n in [(0x40000000, 0x400000), (0x44000000, 0x1000000),
                  (0x47000000, 0x100000), (0x48000000, 0x20000),
                  (0x49000000, 0x1000000), (0x4f000000, 0x10000)]:
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
    def args(n):
        return struct.unpack('>%dI' % n, u.mem_read(u.reg_read(UC_M68K_REG_A7) + 4, 4 * n))
    def ret(value=0):
        sp = u.reg_read(UC_M68K_REG_A7)
        u.reg_write(UC_M68K_REG_D0, value)
        u.reg_write(UC_M68K_REG_A7, sp + 4)
        u.reg_write(UC_M68K_REG_PC, read(sp))
    heap = 0x49010000
    records = list(range(1, 65)) + [0x8001, 0x8002] + [0] * 62
    pcm = [40000] * 64 + [41943, 1488978] + [0] * 62
    names = {0x8001: 'A VERY LONG SAMPLE NAME TO TRIM', 0x8002: 'ST STEREO PAD'}
    text_calls = []
    def hook(uc, at, size, _):
        nonlocal heap
        if at in (0x4010394c, 0x40103952, 0x401036d0, 0x4f001000):
            n, = args(1); result = heap; heap += (n + 31) & ~31
            assert heap < 0x49f00000
            ret(result)
        elif at in (0x40103958, 0x4010395e, 0x401038d6, 0x40001980, 0x40001aa2, 0x40117ce2, 0x40117d60, 0x4f001004, 0x4f00100c, 0x4f001024):
            ret()
        elif at == 0x4f001008:  # no mounted library initially
            ret(0)
        elif at == 0x4f001010:
            project, dst = args(2); u.mem_write(dst, struct.pack('>128H', *records)); ret()
        elif at in (0x4f001014, 0x4f001018):
            ret(1)
        elif at == 0x4f00101c:
            code, dst = args(2)
            if code in names:
                u.mem_write(dst, names[code].encode() + b'\0'); ret(1)
            else:
                u.mem_write(dst, b'STOCK\0'); ret(1)
        elif at == 0x4f001020:
            dst, = args(1); w(dst, 1492992, 66711552, *pcm); ret()
        elif at == 0x40100d86:
            manager, pair, flags = args(3)
            owner = read(pair + 4); w(owner + 4, read(owner + 4) + 1); ret(1)
        elif at == 0x400ffe22:
            ret(1)
        elif at == 0x400f8b68:
            ctx, font, x, y, length, text = args(6)
            data = bytearray()
            while u.mem_read(text + len(data), 1) != b'\0':
                data.extend(u.mem_read(text + len(data), 1))
            text_calls.append((x, y, data.decode('ascii')))
    u.hook_add(UC_HOOK_CODE, hook)
    def call(fn, *values):
        sp = 0x4801f000 - 4 * (len(values) + 1)
        w(sp, 0x4f000000, *values); u.reg_write(UC_M68K_REG_A7, sp)
        try:
            u.emu_start(symbols.get(fn, fn), 0x4f000000, count=2000000)
        except Exception as error:
            raise RuntimeError((fn, hex(u.reg_read(UC_M68K_REG_PC)), hex(u.reg_read(UC_M68K_REG_A0)), hex(u.reg_read(UC_M68K_REG_A1)))) from error
        assert u.reg_read(UC_M68K_REG_PC) == 0x4f000000, hex(u.reg_read(UC_M68K_REG_PC))
    host, storage, pool = 0x49000000, 0x49000100, 0x49000200
    w(host, 13, storage, 0x4f001008, 0x4f00100c, 0, 0x4f001010, 0, 0x4f001014, 0, 0x4f001024)
    w(host + 88, pool)
    w(storage, 0, 0, 0, 0x4f001000, 0x4f001004)
    w(pool, 2, 0x4f001018, 0, 0x4f00101c, 0, 0, 0x4f001024, 0, 0x4f001020)
    w(symbols['host'], host)
    w(0x443db21c, 0x402aa114)  # native small font's boot constructor
    menu, manager = 0x49000300, 0x49000400
    w(menu + 0x64, manager)
    call('st_pool_ui_open', menu)
    view = read(symbols['ui_view'])
    w(symbols['ui_mode'], 0)
    call('rebuild_list')
    call(0x400f9d4c, view, 69, 0)
    call(0x400f9d4c, view, 64, 0)
    u.mem_write(symbols['ui_pool_selected'] + 65, b'\1')
    bitmap, pixels = 0x49001000, 0x49002000
    call(0x400f72be, bitmap, 128, 64, pixels, 0)
    call('st_pool_ui_draw', view + 0x38, bitmap)
    assert (2, 58, 'SAMPLE MANAGER') in text_calls
    assert any(y == 50 and t == '(1.42/63.62) MB' for x, y, t in text_calls)
    rows = [(x, y, t) for x, y, t in text_calls if y < 49]
    assert any('...' in t for x, y, t in rows), rows
    assert any(t == '0.04MB' for x, y, t in rows) and any(t == '1.42MB' for x, y, t in rows), rows
    assert sorted({y for x, y, t in rows}) == [2, 10, 18, 26, 34, 42]
    widths = bytes(u.mem_read(0x402aa128, 256))
    def width(text):
        return sum(widths[ord(c)] + 1 for c in text) - 1
    for x, y, text in rows:
        if text.endswith('MB'):
            assert x + width(text) == 119, (x, text)
            label_x, _, label = next(r for r in rows if r[1] == y and not r[2].endswith('MB'))
            assert label_x + width(label) + 3 <= x
    def capture(name):
        if not out:
            return
        out.mkdir(parents=True, exist_ok=True)
        words = struct.unpack('>256I', u.mem_read(pixels, 1024))
        rows = bytearray()
        for y in reversed(range(64)):
            row = bytearray(b'\0')
            for x in range(128):
                on = words[2 * x + y // 32] & (1 << (31 - y % 32))
                row.extend((bytes((218, 240, 235)) if on else bytes((20, 26, 28))) * 6)
            rows.extend(row * 6)
        def chunk(kind, data):
            return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
        png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 768, 384, 8, 2, 0, 0, 0))
        png += chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b'')
        (out / name).write_bytes(png)
    capture('manager-pool.png')
    # Real library labels/callbacks: a directory stays intact while both mono
    # and stereo names reserve exactly the same right-hand size column.
    index, codes, selected = 0x49004000, 0x49004400, 0x49004500
    w(index + 4, 2); w(index + 12, 2); w(index + 40, 1)
    for i, (code, name, size) in enumerate(((1, names[0x8001], 41944), (2, names[0x8002][3:], 1488978))):
        entry = index + 0x140 + 64 * i
        w(entry, code); w(entry + 16, size // 2); w(entry + 28, i)
        u.mem_write(entry + 32, name.encode() + b'\0')
    w(index + 0x1c8, 1); u.mem_write(index + 0x1d0, b'Drums\0')
    u.mem_write(codes, struct.pack('>3H', 1, 0x8001, 0x8002))
    w(symbols['ui_index'], index); w(symbols['ui_index_len'], 512)
    w(symbols['ui_codes'], codes); w(symbols['ui_selected'], selected)
    w(symbols['ui_mode'], 1); w(symbols['ui_count'], 3)
    call('rebuild_list'); text_calls.clear()
    u.mem_write(pixels, bytes(1024)); call('st_pool_ui_draw', view + 0x38, bitmap)
    assert (8, 42, '[Drums]') in text_calls
    assert any(t.endswith('...') and y == 34 for x, y, t in text_calls)
    assert any(t == '1.42MB' and y == 26 for x, y, t in text_calls)
    assert not any(t.endswith('MB') and y == 42 for x, y, t in text_calls)
    capture('manager-library.png')
    u.mem_write(symbols['ui_status'], b'AUDIO FULL\0'); text_calls.clear()
    call('st_pool_ui_draw', view + 0x38, bitmap)
    assert (2, 50, 'AUDIO FULL') in text_calls
    capture('manager-error.png')
    print('PASS: compiled manager + native OS 1.42 list/font/bitmap; header, right sizes, ellipsis and scrolling; no device IO')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stock', type=Path, required=True)
    parser.add_argument('--bank', type=Path, required=True)
    parser.add_argument('--out', type=Path)
    a = parser.parse_args()
    verify(a.stock, a.bank, a.out)
