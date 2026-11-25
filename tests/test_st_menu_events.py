"""Core 2.7: the PATTERN menu's build events (ev_pattern_menu, ev_reload_menu).

Always: the manifest, the SMZ event numbering, the bank parser's bound and the
host API agree. With an assembler (--as, or ELEKLOADER_CROSS + 'as'): the two
adapters, assembled alone and run in Unicorn, call each handler with the menu
in order, then run the replaced epilogue with the caller's registers intact.
With --stock: the sites hold the documented stock bytes.

    python tests/test_st_menu_events.py [--as m68k-elf-as] [--ld m68k-elf-ld] [--stock Syntakt_OS1.42.syx]
"""
import argparse, json, os, re, shutil, struct, subprocess, sys, tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / 'mods/core-st'
SITES = {0x4004ae0c: ('core_pattern_menu', '4cef7c7c0010', 0x4004ae12, 16),
         0x40182212: ('core_reload_menu', '4cef7c7c0018', 0x40182218, 24)}


def static_checks():
    m = json.loads((CORE / 'mod.json').read_text())
    assert m['sources'][-1] == 'menu-events.s'                      # last: nothing before it moves
    sites = {int(s['addr'], 16): s for s in m['sites']}
    for at, (target, stock, _, _) in SITES.items():
        s = sites[at]
        assert (s['op'], s['stock'], s['target']) == ('jmp', stock, target), hex(at)
    assert m['collections']['ev_pattern_menu'] == m['collections']['ev_reload_menu'] == 4
    h = (ROOT / 'elekloader/sdk/include/syntakt/modules.h').read_text()
    names = re.search(r'enum st_smz_event \{(.*?)\};', h, re.S).group(1)
    names = [n.strip() for n in re.sub(r'/\*.*?\*/', '', names, flags=re.S).split(',') if n.strip()]
    assert names.index('ST_EV_FX_CV') == 15 and names.index('ST_EV_PATTERN_MENU') == 16
    assert names.index('ST_EV_RELOAD_MENU') == 17 and names[-1] == 'ST_EV_COUNT' and len(names) - 1 == 18
    c = (CORE / 'modules.c').read_text()
    table = re.search(r'events\[\]=\{(.*?)\};', c, re.S).group(1)
    assert table.replace(' ', '').replace('\n', '').endswith('E(fx_cv),E(pattern_menu),E(reload_menu)')
    smz = (CORE / 'smz.c').read_text()
    assert '!(x<18 ||' in smz and '!(x<16 ||' not in smz           # 16/17 accepted, nothing above
    host = (CORE / 'machine-host.c').read_text()
    assert re.search(r'core_machine_host_api=\{\s*18,', host)
    print('static: manifest, event numbers 16/17, parser bound 18, host API 18')


def stock_checks(path):
    sys.path.insert(0, str(ROOT))
    from elekloader import formats
    firmware, device, release = formats.load(path)
    assert release.version == '1.42'
    main = formats.main_image(firmware, device)                  # loaded at 0x40000400
    for at, (_, stock, _, _) in SITES.items():
        assert main[at - 0x40000400:at - 0x40000400 + 6].hex() == stock, hex(at)
    print('stock: both sites hold the documented epilogues')


def emulator_checks(asm, ld):
    from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
    from unicorn.m68k_const import (UC_CPU_M68K_CFV4E, UC_M68K_REG_A7, UC_M68K_REG_SR, UC_M68K_REG_PC,
                                    UC_M68K_REG_D0, UC_M68K_REG_A0)
    regs = [getattr(__import__('unicorn.m68k_const', fromlist=['x']), 'UC_M68K_REG_%s%d' % (k, i))
            for k in 'DA' for i in range(8)]
    CODE, LISTS, HANDLERS = 0x46000000, 0x46010000, 0x46020000
    with tempfile.TemporaryDirectory() as w:
        w = Path(w)
        subprocess.run([asm, '-mcpu=54415', '-o', str(w / 'm.o'), str(CORE / 'menu-events.s')], check=True)
        subprocess.run([ld, '-o', str(w / 'm.elf'), '--section-start=.run=%#x' % CODE,
                        '--defsym=ev_pattern_menu=%#x' % LISTS, '--defsym=ev_reload_menu=%#x' % (LISTS + 0x100),
                        '-e', 'core_pattern_menu', str(w / 'm.o')], check=True)
        nm = subprocess.run([ld.replace('ld', 'nm'), str(w / 'm.elf')], check=True, capture_output=True, text=True)
        sym = {l.split()[2]: int(l.split()[0], 16) for l in nm.stdout.splitlines() if len(l.split()) == 3}
        oc = ld.replace('ld', 'objcopy')
        subprocess.run([oc, '-O', 'binary', '-j', '.run', str(w / 'm.elf'), str(w / 'm.bin')], check=True)
        code = (w / 'm.bin').read_bytes()

    for at, (target, _, resume, frame) in SITES.items():
        for n in (0, 1, 3):
            u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
            u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
            u.mem_map(0x46000000, 0x40000)
            u.mem_map(0x50000000, 0x10000)
            u.mem_write(CODE, code)
            lst = LISTS + (0x100 if target == 'core_reload_menu' else 0)
            u.mem_write(lst, struct.pack('>%dI' % (n + 1), *[HANDLERS + 0x10 * i for i in range(n)], 0))
            for i in range(n):          # each handler: clobber d0-d1/a0-a1, return
                u.mem_write(HANDLERS + 0x10 * i, bytes.fromhex('70ff72ff91c893c94e75'))
            menu, sp = 0x50001234, 0x50008000
            # the caller's frame: saved d2-d6/a2-a6 at frame(sp), checked after the replaced movem
            saved = [0x1000 + i for i in range(10)]
            u.mem_write(sp + frame, struct.pack('>10I', *saved))
            u.reg_write(UC_M68K_REG_SR, 0x2700)
            u.reg_write(UC_M68K_REG_A7, sp)
            for r, v in zip(regs, [0xd0, 0xd1, 2, 3, 4, 5, 6, 7, 0xa0, 0xa1, menu, 0xa3, 4, 5, 6]):
                u.reg_write(r, v)
            calls = []
            u.hook_add(UC_HOOK_CODE, lambda uc, a, s, _: calls.append(
                (a, struct.unpack('>I', uc.mem_read(uc.reg_read(UC_M68K_REG_A7) + 4, 4))[0])),
                begin=HANDLERS, end=HANDLERS + 0x100)
            u.emu_start(sym[target], resume, count=10_000)
            assert u.reg_read(UC_M68K_REG_PC) == resume
            assert [c[0] for c in calls if c[0] % 0x10 == 0] == [HANDLERS + 0x10 * i for i in range(n)]
            assert all(c[1] == menu for c in calls if c[0] % 0x10 == 0)        # f(menu)
            assert u.reg_read(UC_M68K_REG_A7) == sp                             # same stack as the stock epilogue
            got = [u.reg_read(r) & 0xffffffff for r in regs[:2] + regs[8:10]]
            assert got == [0xd0, 0xd1, 0xa0, 0xa1], [hex(x) for x in got]      # scratch registers restored
            got = [u.reg_read(r) & 0xffffffff for r in regs[2:7] + regs[10:15]]
            assert got == saved                                                 # the replaced movem ran
    print('emulator: 0, 1 and 3 handlers at both sites, f(menu), registers and stack as stock')


def main():
    p = argparse.ArgumentParser()
    cross = os.environ.get('ELEKLOADER_CROSS', 'm68k-elf-')
    p.add_argument('--as', dest='asm', default=shutil.which(cross + 'as'))
    p.add_argument('--ld', default=shutil.which(cross + 'ld'))
    p.add_argument('--stock')
    a = p.parse_args()
    static_checks()
    if a.stock:
        stock_checks(a.stock)
    else:
        print('stock: skipped (no --stock)')
    if a.asm and a.ld:
        emulator_checks(a.asm, a.ld)
    else:
        print('emulator: skipped (no assembler)')


if __name__ == '__main__':
    main()
