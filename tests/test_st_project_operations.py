"""Project worker scopes in the delivered image; optional linked CPU execution.

Native IO workers and STOP are explicit doubles in the lifecycle tests. The
real reduced mixer/output/EMAC is checked separately on cfbench.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader.sdk.syntakt.audio import audit

BASE, SP, RET, CAPTURE = 0x40000400, 0x48010000, 0x4f000000, 0x47000000
SEQ, STOP = 0x43bd83bc, 0x4008c300
# Independently pinned OS 1.42 literal sites and native worker destinations.
WORKERS = {
    'new': (0x4005d83a, 0x40059f9e),
    'rename': (0x4005a486, 0x4005a6f8),
    'copy': (0x4005a558, 0x4005a788),
    'paste': (0x4005a606, 0x4005a7aa),
    'clear': (0x4005a6a6, 0x4005a7d0),
    'clear_many': (0x4005d20e, 0x4005a828),
    'delete_many': (0x4005d3b6, 0x4005a8a2),
    'save': (0x4005b662, 0x4005ba50),
    'load': (0x4005d9dc, 0x4005bafa),
}


def verify(folder, emulate=False):
    r = json.loads((folder / 'verification.json').read_text())
    sym, layout = r['symbols'], r['link']
    raw = (folder / 'main.bin').read_bytes()
    def addr(name): return sym['core:' + name]
    def read(at, n):
        if layout['ddr'][0] <= at < layout['ddr'][1]:
            at = layout['run_load'] + at - layout['ddr'][0]
        return raw[at - BASE:at - BASE + n]
    for name, (site, native) in WORKERS.items():
        entry = addr('core_project_' + name)
        assert read(site, 6) == struct.pack('>HI', 0x203c, entry)
        assert read(entry, 8) == struct.pack('>HhI', 0x6100,
            addr('core_project_call') - entry - 2, native)
        assert audit(read(entry, 4)) == 1
        assert layout['ddr'][0] <= entry < layout['ddr'][1]
    for name in ('core_project_call', 'core_project_idle_mix'):
        code = read(addr(name), addr(name + '_end') - addr(name))
        assert audit(code) > 0
    assert r['resident_ram']['free'] >= 0
    print('PASS: nine real project callback literals, native worker destinations, resident scopes and instruction legality')
    if not emulate:
        return

    from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
    from unicorn import m68k_const as reg
    u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    u.ctl_set_cpu_model(reg.UC_CPU_M68K_CFV4E)
    for at, n in ((0x40000000, 0x6000000), (0x46000000, 0x10000),
                  (CAPTURE, 0x10000), (SP - 0x10000, 0x20000), (RET, 0x1000)):
        u.mem_map(at, n)
    u.mem_write(BASE, raw)
    lo, hi = layout['ddr']
    u.mem_write(lo, raw[layout['run_load'] - BASE:layout['run_load'] - BASE + hi - lo])
    def w(at, value): u.mem_write(at, struct.pack('>I', value & 0xffffffff))
    def word(at): return struct.unpack('>I', u.mem_read(at, 4))[0]
    def ret(value):
        sp = u.reg_read(reg.UC_M68K_REG_A7)
        u.reg_write(reg.UC_M68K_REG_D0, value)
        u.reg_write(reg.UC_M68K_REG_A7, sp + 4)
        u.reg_write(reg.UC_M68K_REG_PC, word(sp))
    calls = []
    native_entries = {native for _, native in WORKERS.values()}
    # A real callable descriptor for an otherwise eligible recorder session.
    OWNER, READY, MIX = CAPTURE + 0x100, CAPTURE + 0x200, CAPTURE + 0x300
    w(OWNER, READY); w(OWNER + 4, MIX)
    u.mem_write(READY, bytes.fromhex('70014e75'))
    u.mem_write(MIX, bytes.fromhex('4e75'))
    # Observe CCR with a real branch, as render-in does. Unicorn's SR reader
    # does not materialize its lazy arithmetic flags at a stopped boundary.
    PROBE = READY + 32
    u.mem_write(PROBE, struct.pack('>HI', 0x4eb9, addr('core_audio_idle_begin')) +
                bytes.fromhex('660472004e7572014e75'))
    def hook(cpu, pc, size, data):
        if pc == STOP:
            assert word(SEQ) == 1 and word(addr('core_project_depth')) == expected_depth - 1
            calls.append('stop'); w(SEQ, 2)
            # STOP is allowed to clobber every caller-saved register.
            for rr in (reg.UC_M68K_REG_D1, reg.UC_M68K_REG_A0, reg.UC_M68K_REG_A1):
                cpu.reg_write(rr, 0xdead0000)
            ret(0xbeef)
        elif pc in native_entries:
            assert word(addr('core_project_depth')) == expected_depth
            assert word(cpu.reg_read(reg.UC_M68K_REG_A7) + 4) == CAPTURE
            calls.append(pc); cpu.emu_stop()
    u.hook_add(UC_HOOK_CODE, hook)
    def run(pc, until=RET):
        u.emu_start(pc, until, count=10000)
        return u.reg_read(reg.UC_M68K_REG_PC)
    def call_begin():
        context = u.context_save()
        u.reg_write(reg.UC_M68K_REG_A7, SP - 0x8000)
        w(SP - 0x8000, RET)
        assert run(PROBE) == RET
        result = u.reg_read(reg.UC_M68K_REG_D0)
        assert u.reg_read(reg.UC_M68K_REG_D1) == bool(result)
        u.context_restore(context)
        return result
    saved = {getattr(reg, 'UC_M68K_REG_' + n): 0x12340000 + i
             for i, n in enumerate(('D2','D3','D4','D5','D6','D7','A2','A3','A4','A5','A6'))}
    cases = 0
    for name, (site, native) in WORKERS.items():
        for seq in (0, 1, 2):
            for value in (0, 1, 0xdeadbeef):
                for owner in (0, OWNER):
                    for nested in (0, 1):
                        expected_depth = nested + 1
                        calls.clear(); w(SEQ, seq); w(addr('core_project_depth'), nested)
                        w(addr('core_audio_idle_owner'), owner)
                        u.reg_write(reg.UC_M68K_REG_SR, 0x2000)
                        for rr, v in saved.items(): u.reg_write(rr, v)
                        u.reg_write(reg.UC_M68K_REG_A7, SP)
                        w(SP, RET); w(SP + 4, CAPTURE); w(SP + 8, 0xaabbccdd)
                        assert run(word(site + 2)) == native
                        assert calls == (['stop'] if seq == 1 else []) + [native]
                        assert word(SEQ) == (2 if seq == 1 else seq)
                        assert call_begin() == (0 if seq == 0 else 2)
                        # Worker success/failure both return through the same scope.
                        ret(value)
                        assert run(u.reg_read(reg.UC_M68K_REG_PC)) == RET
                        assert u.reg_read(reg.UC_M68K_REG_D0) == value
                        assert u.reg_read(reg.UC_M68K_REG_A7) == SP + 4
                        assert word(SP + 4) == CAPTURE and word(SP + 8) == 0xaabbccdd
                        assert all(u.reg_read(rr) == v for rr, v in saved.items())
                        assert word(addr('core_project_depth')) == nested
                        expected = 0 if seq == 0 else 2 if nested else 1 if owner else 0
                        assert call_begin() == expected
                        cases += 1
    # The mixer uses the block snapshot even if foreground ownership changes.
    # Stop at its selected entry: no EMAC arithmetic is executed in Unicorn.
    for active, depth, target in ((0, 1, 0x4008f242), (1, 1, MIX),
                                  (2, 0, addr('core_project_idle_mix'))):
        w(addr('core_audio_idle_active'), active); w(addr('core_project_depth'), depth)
        w(addr('core_audio_idle_owner'), OWNER if active == 1 else 0)
        u.reg_write(reg.UC_M68K_REG_A7, SP); w(SP, RET)
        assert run(addr('core_audio_idle_mix'), target) == target
    print(f'PASS: {cases} linked worker cases: STOP, no recorder, error returns, nesting, recorder ownership, CCR, stack/register ABI and block-snapshot routing')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('folder', type=Path)
    p.add_argument('--emulate', action='store_true')
    a = p.parse_args()
    verify(a.folder, a.emulate)
