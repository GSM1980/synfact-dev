# SPDX-License-Identifier: GPL-2.0-or-later
"""The MD machine (mods/md-st): its voices against mdkit's, word for word.

    python tests/test_md_st.py [--cc <x86_64 clang>] [--sgdk <SGDK bin>] [--quick]

On the host (--cc, default $LLVM_MINGW_BIN/x86_64-w64-mingw32-clang.exe):
  1. tools/md_port.py --check: the ported voices and tables are what it writes
  2. md-runtime.c: the float32 and 64-bit helpers against the host's arithmetic
  3. md-acc.h: every accumulator helper against mdkit's
  4. each voice with MD_ST_EXACT against mdkit's own code, word for word; and as the
     Syntakt builds it (tools/md_port.py's APPROX forms): its error under -85 dBFS; the
     EFM voices with 'fm' (whose FM at high MOD ends up elsewhere from any change) and
     the PI voices with 'struct' (modes left out), by ear: their level within 1.5 dB and
     third-octave bands within 12 dB of mdkit's (md_port.SPECTRUM);
     a voice in md_port.SPECTRUM_LIMITS (approved by ear) by its own limits
  5. md.c: resampling, TYPE at the note, no voice chosen, knobs, rest, track bounds;
     main.c: the one page, the voice list, the name box, the spare knobs hidden; the spare
     knobs reach no voice
On the Syntakt's CPU, in Unicorn (--sgdk, default $SGDK_BIN; needs `unicorn`):
  6. each voice built with the bank's flags, without the EMAC (Unicorn's EMAC does not
     compute what the manual says): its words equal the host's, and the instructions
     per block (instructions, not cycles: the unit's CPU A reading is what counts)
In analog-emu's ColdFire core (--cfbench, default $MD_CFBENCH: its cfbench executable),
whose EMAC follows the MCF5441x reference manual:
  7. each voice and one track built as the bank builds them, with the EMAC: the hash
     of their words equals the host's, and the instructions per block
A step whose tool is not given is reported SKIP, not passed.
"""
from __future__ import annotations

import argparse
import os
import re
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
MOD = ROOT / 'mods' / 'md-st'
sys.path.insert(0, str(MOD / 'tools'))
sys.path.insert(0, str(ROOT))
import md_port  # noqa: E402

failed = []


def run(cmd, **kw):
    r = subprocess.run([str(c) for c in cmd], capture_output=True, text=True, **kw)
    if r.returncode:
        raise RuntimeError('%s\n%s%s' % (' '.join(map(str, cmd)), r.stdout, r.stderr))
    return r.stdout


def step(name, fn):
    try:
        out = fn()
        print('ok      %s%s' % (name, (': ' + out.strip().splitlines()[-1]) if out and out.strip() else ''))
    except Exception as e:  # noqa: BLE001
        import traceback
        print('FAIL    %s\n%s' % (name, e if isinstance(e, RuntimeError) else traceback.format_exc()))
        failed.append(name)


def host(cc, work, quick):
    flags = [cc, '-std=c99', '-O2', '-ffp-contract=off', '-w']
    cases = '200000' if quick else '2000000'
    env = dict(os.environ, MD_HOST_CC=cc)
    step('md_port.py is up to date', lambda: run([sys.executable, MOD / 'tools' / 'md_port.py', '--check'], env=env))

    def runtime():
        run(flags + [ROOT / 'tests' / 'md_st_runtime_check.c', '-o', work / 'runtime.exe'])
        return run([work / 'runtime.exe', cases])
    step('md-runtime.c against the host', runtime)

    def acc():
        run(flags + ['-I', MOD / 'mdkit' / 'include', ROOT / 'tests' / 'md_st_acc_check.c', '-o', work / 'acc.exe'])
        return run([work / 'acc.exe', cases])
    step('md-acc.h against mdkit', acc)
    run(flags + ['-I', MOD / 'audio', '-c', MOD / 'audio' / 'md-tables.c', '-o', work / 'tables.o'])
    for voice in md_port.VOICES:
        family, name = voice.split('_', 1)

        def exact(voice=voice, family=family, name=name):
            ref, port = work / ('ref-%s.o' % voice), work / ('port-%s.o' % voice)
            run(flags + ['-Dmdkit_voice_%s=ref_voice' % voice, '-I', MOD / 'mdkit' / 'include', '-c',
                         MOD / 'mdkit' / 'src' / family / name / ('mdkit_%s.c' % voice), '-o', ref])
            run(flags + ['-DMD_ST_TABLES', '-DMD_ST_DOMAIN_CHECK', '-DMD_ST_EXACT', '-c', MOD / md_port.voice_source(voice), '-o', port])
            if voice in md_port.HAND:     # a model in mdkit, not DSP words: attacks and levels (md_st_voice_model.c)
                exe = work / ('model-%s.exe' % voice)
                run(flags + ['-DVOICE=' + voice, '-I', MOD / 'mdkit' / 'include', ROOT / 'tests' / 'md_st_voice_model.c',
                             ref, port, work / 'tables.o', '-lm', '-o', exe])
                return run([exe, '12' if quick else '60', '90'])
            exe = work / ('voice-%s.exe' % voice)
            run(flags + ['-DVOICE=' + voice, ROOT / 'tests' / 'md_st_voice_check.c', ref, port, work / 'tables.o', '-o', exe])
            return run([exe, '12' if quick else '60', '0' if quick else '1'])
        step('%s against mdkit' % voice, exact)

    for voice in md_port.APPROX:
        def approx(voice=voice):
            port = work / ('approx-%s.o' % voice)
            run(flags + ['-DMD_ST_TABLES', '-c', MOD / md_port.voice_source(voice), '-o', port])
            exe = work / ('approx-%s.exe' % voice)
            if md_port.SPECTRUM & set(md_port.APPROX[voice]):
                run(flags + ['-DVOICE=' + voice, ROOT / 'tests' / 'md_st_voice_spectrum.c', work / ('ref-%s.o' % voice), port,
                             work / 'tables.o', '-lm', '-o', exe])
                level, bands = getattr(md_port, 'SPECTRUM_LIMITS', {}).get(voice, (1.5, 12))   # md_port: approved by ear
                return run([exe, '12' if quick else '60', str(level), str(bands)])
            run(flags + ['-DVOICE=' + voice, ROOT / 'tests' / 'md_st_voice_ser.c', work / ('ref-%s.o' % voice), port,
                         work / 'tables.o', '-lm', '-o', exe])
            return run([exe, '12' if quick else '60', '-1000', '-85'])
        what = 'level and spectrum' if md_port.SPECTRUM & set(md_port.APPROX[voice]) else 'error level'
        step('%s as built (%s), %s against mdkit' % (voice, ', '.join(md_port.APPROX[voice]), what), approx)

    for voice in md_port.NATIVE:
        def fast(voice=voice):
            port = work / ('fast-%s.o' % voice)
            run(flags + ['-DMD_ST_TABLES', '-DMD_ST_FAST', '-DMD_ST_EXACT', '-c', MOD / md_port.voice_source(voice), '-o', port])
            exe = work / ('ser-%s.exe' % voice)
            run(flags + ['-DVOICE=' + voice, ROOT / 'tests' / 'md_st_voice_ser.c', work / ('ref-%s.o' % voice), port,
                         work / 'tables.o', '-lm', '-o', exe])
            return run([exe, '12' if quick else '60', '70'])
        step('%s with MD_ST_FAST, signal to error against mdkit' % voice, fast)

    def glue(rest):
        exe = work / ('glue-%d.exe' % rest)
        run(flags + ['-DMD_ST_TABLES', '-DMD_REST=%d' % rest, '-I', ROOT / 'elekloader' / 'sdk' / 'include', '-I', MOD / 'audio',
                     ROOT / 'tests' / 'md_st_glue_check.c', work / 'tables.o'] +
            [MOD / md_port.voice_source(v) for v in md_port.VOICES] + ['-o', exe])
        return run([exe])
    step('md.c on the host (a voice renders always: MD_REST 0, as built)', lambda: glue(0))
    step('md.c on the host (MD_REST 1: a silent voice rests)', lambda: glue(1))

    def picker():
        exe = work / 'picker.exe'
        run(flags + ['-I', ROOT / 'elekloader' / 'sdk' / 'include', '-I', MOD, ROOT / 'tests' / 'md_st_picker_check.c',
                     '-o', exe])
        return run([exe])
    step('main.c (page, voice picker) on the host', picker)

    def spare():
        exe = work / 'spare.exe'
        run(flags + ['-DMD_ST_TABLES', '-I', ROOT / 'elekloader' / 'sdk' / 'include', '-I', MOD / 'audio',
                     ROOT / 'tests' / 'md_st_spare_knobs.c', work / 'tables.o'] +
            [MOD / md_port.voice_source(v) for v in md_port.VOICES] + ['-o', exe])
        return run([exe])
    step('md.c: a voice ignores the knobs past its last control', spare)


def target(cc, sgdk, work, quick):
    from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_HOOK_CODE
    from unicorn.m68k_const import UC_CPU_M68K_CFV4E, UC_M68K_REG_A7
    from elekloader.devices.syntakt import DEVICE
    tool = lambda n: str(pathlib.Path(sgdk) / (n + ('.exe' if os.name == 'nt' else '')))
    env = dict(os.environ, GCC_EXEC_PREFIX=str(sgdk).rstrip('/\\') + '/', PATH=str(sgdk) + os.pathsep + os.environ['PATH'])
    cflags = DEVICE.toolchain['cflags'] + ['-fno-jump-tables', '-DMD_ST_NO_EMAC']
    blocks = 60 if quick else 200
    for voice in md_port.VOICES:
        def bench(voice=voice):
            objs = []
            for src in (MOD / md_port.voice_source(voice), ROOT / 'tests' / 'md_st_bench.c',
                        MOD / 'audio' / 'md-runtime.c', MOD / 'audio' / 'md-tables.c'):
                o = work / ('t-%s-%s.o' % (voice, src.stem))
                run([tool('gcc')] + cflags + ['-DVOICE=' + voice, '-I', MOD / 'audio', '-I', MOD, '-c', src, '-o', o], env=env)
                objs.append(o)
            base = 0x40100000
            (work / 'bench.ld').write_text('SECTIONS { .image 0x%x : { *(.text*) *(.rodata*) *(.data*) BYTE(0) . = ALIGN(4); '
                                           '*(.bss*) *(COMMON) . = ALIGN(16); } /DISCARD/ : { *(.comment) } }\n' % base)
            elf_, bin_ = work / ('t-%s.elf' % voice), work / ('t-%s.bin' % voice)
            run([tool('ld'), '--no-warn-rwx-segments', '-T', work / 'bench.ld', '-o', elf_] + objs, env=env)
            run([tool('objcopy'), '-O', 'binary', '-j', '.image', elf_, bin_], env=env)
            undefined = run([tool('nm'), '-u', elf_], env=env).split()
            if undefined: raise RuntimeError('undefined on the Syntakt: %s' % undefined)
            if voice in md_port.INT_CLOCK + ('trx_bd', 'trx_sd', 'trx_b2'):         # the clock in integers
                floats = [u for u in run([tool('nm'), '-u', objs[0]], env=env).split() if re.match(r'__\w*sf', u)]
                if floats: raise RuntimeError('%s calls float routines: %s' % (voice, floats))
            syms = {p[2]: int(p[0], 16) for p in (l.split() for l in run([tool('nm'), elf_], env=env).splitlines()) if len(p) == 3}
            img = bin_.read_bytes()
            u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN); u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
            u.mem_map(base, (len(img) + 0xffff) & ~0xffff); u.mem_write(base, img)
            stack, ret = 0x48000000, 0x4f000000
            u.mem_map(stack - 0x10000, 0x20000); u.mem_map(ret, 0x1000)
            count = [0]
            u.hook_add(UC_HOOK_CODE, lambda *a: count.__setitem__(0, count[0] + 1))

            def call(name):
                u.mem_write(stack - 16, struct.pack('>I', ret)); u.reg_write(UC_M68K_REG_A7, stack - 16)
                count[0] = 0; u.emu_start(syms[name], ret); return count[0]
            knobs = [100, 40, 64, 20, 90, 10, 64, 30]
            u.mem_write(syms['bench_knobs'], struct.pack('>8I', *knobs))
            call('bench_init'); call('bench_trigger')
            costs, words = [], []
            for _ in range(blocks):
                costs.append(call('bench_block'))
                words.append(' '.join(str(w) for w in struct.unpack('>32i', u.mem_read(syms['bench_out'], 128))))
            exe = work / ('host-bench-%s.exe' % voice)
            run([cc, '-std=c99', '-O2', '-ffp-contract=off', '-w', '-DVOICE=' + voice, '-DMD_ST_TABLES', '-I', MOD / 'audio',
                 ROOT / 'tests' / 'md_st_bench.c', MOD / md_port.voice_source(voice), MOD / 'audio' / 'md-tables.c',
                 '-o', exe])
            want = run([exe, str(blocks)] + [str(k) for k in knobs]).split('\n')
            if [w.strip() for w in want[:blocks]] != words:
                raise RuntimeError('%s: the Syntakt build\'s words differ from the host\'s' % voice)
            costs.sort()
            return ('%d blocks equal to the host; instructions per Syntakt block: median %d, most %d'
                    % (blocks, costs[len(costs) // 2] * 147 // 160, costs[-1] * 147 // 160))
        step('%s on the Syntakt CPU (Unicorn)' % voice, bench)

    def track():
        """md_update + md_render of one sounding track, each TYPE: the voice and the resampler"""
        objs = []
        sources = [ROOT / 'tests' / 'md_st_track_bench.c', MOD / 'audio' / 'md-runtime.c', MOD / 'audio' / 'md-tables.c'] + \
                  [MOD / md_port.voice_source(v) for v in md_port.VOICES]
        for src in sources:
            o = work / ('k-%s.o' % src.stem)
            run([tool('gcc')] + cflags + ['-I', ROOT / 'elekloader' / 'sdk' / 'include', '-I', MOD / 'audio', '-I', MOD,
                                          '-c', src, '-o', o], env=env)
            objs.append(o)
        base = 0x40100000
        elf_, bin_ = work / 'track.elf', work / 'track.bin'
        run([tool('ld'), '--no-warn-rwx-segments', '-T', work / 'bench.ld', '-o', elf_] + objs, env=env)
        run([tool('objcopy'), '-O', 'binary', '-j', '.image', elf_, bin_], env=env)
        syms = {p[2]: int(p[0], 16) for p in (l.split() for l in run([tool('nm'), elf_], env=env).splitlines()) if len(p) == 3}
        img = bin_.read_bytes()
        out = []
        for type_ in range(len(md_port.VOICES)):
            u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN); u.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
            u.mem_map(base, (len(img) + 0xffff) & ~0xffff); u.mem_write(base, img)
            stack, ret = 0x48000000, 0x4f000000
            u.mem_map(stack - 0x10000, 0x20000); u.mem_map(ret, 0x1000)
            count = [0]
            u.hook_add(UC_HOOK_CODE, lambda *a: count.__setitem__(0, count[0] + 1))
            params = [0] * 71
            for i, slot in enumerate((0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x1a, 0x19)):
                params[slot] = (100, 40, 64, 20, 90, 10, 64, 30)[i] << 8
            u.mem_write(syms['bench_params'], struct.pack('>71H', *params))
            u.mem_write(syms['bench_type'], struct.pack('>I', type_))
            u.mem_write(syms['bench_trigger'], struct.pack('>I', 1))
            costs = []
            for _ in range(blocks):
                u.mem_write(stack - 16, struct.pack('>I', ret)); u.reg_write(UC_M68K_REG_A7, stack - 16)
                count[0] = 0; u.emu_start(syms['bench_track_block'], ret); costs.append(count[0])
            costs = sorted(costs[2:])
            out.append('%s %d' % (list(md_port.VOICES)[type_], costs[len(costs) // 2]))
        return 'instructions per Syntakt block, median, the voice and its resampler: ' + ', '.join(out)
    step('one track on the Syntakt CPU (Unicorn)', track)


def analog(cc, sgdk, cfbench, work, quick):
    """step 7: the EMAC build in analog-emu's interpreter, against the host's hash"""
    from elekloader.devices.syntakt import DEVICE
    tool = lambda n: str(pathlib.Path(sgdk) / (n + ('.exe' if os.name == 'nt' else '')))
    env = dict(os.environ, GCC_EXEC_PREFIX=str(sgdk).rstrip('/\\') + '/', PATH=str(sgdk) + os.pathsep + os.environ['PATH'])
    cflags = DEVICE.toolchain['cflags'] + ['-fno-jump-tables']
    blocks, base = (60 if quick else 200), 0x40100000
    ld = work / 'cf.ld'
    ld.write_text('SECTIONS { .image 0x%x : { *(.text*) *(.rodata*) *(.data*) BYTE(0) . = ALIGN(4); '
                  '*(.bss*) *(COMMON) . = ALIGN(16); } /DISCARD/ : { *(.comment) } }\n' % base)

    def image(name, sources, defines):
        objs = []
        for src in sources:
            o = work / ('cf-%s-%s.o' % (name, src.stem))
            run([tool('gcc')] + cflags + defines + ['-I', ROOT / 'elekloader' / 'sdk' / 'include', '-I', MOD / 'audio',
                                                    '-I', MOD, '-c', src, '-o', o], env=env)
            objs.append(o)
        elf_, bin_ = work / ('cf-%s.elf' % name), work / ('cf-%s.bin' % name)
        run([tool('ld'), '--no-warn-rwx-segments', '-T', ld, '-o', elf_] + objs, env=env)
        run([tool('objcopy'), '-O', 'binary', '-j', '.image', elf_, bin_], env=env)
        undefined = run([tool('nm'), '-u', elf_], env=env).split()
        if undefined: raise RuntimeError('undefined on the Syntakt: %s' % undefined)
        if name in md_port.NATIVE and 'macl' not in run([tool('objdump'), '-d', elf_], env=env):
            raise RuntimeError('%s: no EMAC instruction in the build' % name)
        syms = {p[2]: int(p[0], 16) for p in (l.split() for l in run([tool('nm'), elf_], env=env).splitlines()) if len(p) == 3}
        return bin_, syms

    def calls(bin_, steps):
        out = run([cfbench, bin_, '%x' % base] + steps)
        return [(int(l.split('-> d0 ')[1].split(':')[0], 16), int(l.split(': ')[1].split()[0]))
                for l in out.splitlines() if l.startswith('call ')]
    knobs = [100, 40, 64, 20, 90, 10, 64, 30]
    for voice in md_port.VOICES:
        def bench(voice=voice):
            bin_, syms = image(voice, [MOD / md_port.voice_source(voice), ROOT / 'tests' / 'md_st_bench.c',
                                       MOD / 'audio' / 'md-runtime.c', MOD / 'audio' / 'md-tables.c'], ['-DVOICE=' + voice])
            r = calls(bin_, ['poke:%x:%s' % (syms['bench_knobs'], ''.join('%08x' % k for k in knobs)),
                             '%x' % syms['bench_init'], '%x' % syms['bench_trigger'], '%x:%x' % (syms['bench_run'], blocks)])
            exe = work / ('host-bench-%s.exe' % voice)
            run([cc, '-std=c99', '-O2', '-ffp-contract=off', '-w', '-DVOICE=' + voice, '-DMD_ST_TABLES', '-I', MOD / 'audio',
                 ROOT / 'tests' / 'md_st_bench.c', MOD / md_port.voice_source(voice), MOD / 'audio' / 'md-tables.c',
                 '-o', exe])
            want = int(run([exe, 'hash', str(blocks)] + [str(k) for k in knobs]).strip(), 16)
            if r[-1][0] != want:
                raise RuntimeError("%s: the EMAC build's words differ from the host's (%08x, want %08x)" % (voice, r[-1][0], want))
            return '%d blocks equal to the host; instructions per Syntakt block: %d' % (blocks, r[-1][1] * 147 // (160 * blocks))
        step('%s with the EMAC (analog-emu)' % voice, bench)

    def track():
        bin_, syms = image('track', [ROOT / 'tests' / 'md_st_track_bench.c', MOD / 'audio' / 'md-runtime.c',
                                     MOD / 'audio' / 'md-tables.c'] +
                           [MOD / md_port.voice_source(v) for v in md_port.VOICES], [])
        params = [0] * 71
        for i, slot in enumerate((0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x1a, 0x19)): params[slot] = knobs[i] << 8
        host_track = work / 'host-track.exe'
        run([cc, '-std=c99', '-O2', '-ffp-contract=off', '-w', '-DMD_ST_TABLES', '-I', ROOT / 'elekloader' / 'sdk' / 'include',
             '-I', MOD / 'audio', ROOT / 'tests' / 'md_st_track_bench.c', MOD / 'audio' / 'md-tables.c'] +
            [MOD / md_port.voice_source(v) for v in md_port.VOICES] + ['-o', host_track])
        out = []
        for type_ in range(len(md_port.VOICES)):
            r = calls(bin_, ['poke:%x:%s' % (syms['bench_params'], ''.join('%04x' % v for v in params)),
                             'poke:%x:%08x' % (syms['bench_type'], type_), 'poke:%x:%08x' % (syms['bench_trigger'], 1),
                             '%x:2' % syms['bench_track_run'],     # the self-test runs in the first
                             '%x:%x' % (syms['bench_track_run'], blocks), '%x' % syms['bench_emac_status']])
            if r[-1][0] != 1: raise RuntimeError("md.c's EMAC self-test did not pass in analog-emu (status %d)" % r[-1][0])
            want = int(run([host_track, str(type_), '2', str(blocks)] + [str(k) for k in knobs]).strip(), 16)
            if r[-2][0] != want:
                raise RuntimeError("%s: the track's words on the Syntakt's CPU differ from the host's (%08x, want %08x)"
                                   % (list(md_port.VOICES)[type_], r[-2][0], want))
            out.append('%s %d' % (list(md_port.VOICES)[type_], r[-2][1] // blocks))
        return ('EMAC self-test passed, words equal to the host; instructions per Syntakt block, the voice and its resampler: '
                + ', '.join(out))
    step('one track with the EMAC (analog-emu)', track)


def main() -> int:
    ap = argparse.ArgumentParser()
    llvm = os.environ.get('LLVM_MINGW_BIN')
    ap.add_argument('--cc', default=os.environ.get('MD_HOST_CC') or (str(pathlib.Path(llvm) / 'x86_64-w64-mingw32-clang.exe') if llvm else None))
    ap.add_argument('--sgdk', default=os.environ.get('SGDK_BIN'))
    ap.add_argument('--cfbench', default=os.environ.get('MD_CFBENCH'), help="analog-emu's cfbench executable")
    ap.add_argument('--quick', action='store_true', help='fewer cases')
    a = ap.parse_args()
    work = pathlib.Path(tempfile.mkdtemp(prefix='md-st-'))
    try:
        if a.cc and pathlib.Path(a.cc).exists(): host(a.cc, work, a.quick)
        else: print('SKIP    host steps 1-5: no --cc (set LLVM_MINGW_BIN)')
        try:
            import unicorn  # noqa: F401
            have_unicorn = True
        except ImportError:
            have_unicorn = False
        if a.cc and a.sgdk and pathlib.Path(a.sgdk).exists() and have_unicorn: target(a.cc, a.sgdk, work, a.quick)
        else: print('SKIP    step 6: needs --cc, --sgdk (SGDK_BIN) and the unicorn module')
        if a.cc and a.sgdk and a.cfbench and pathlib.Path(a.cfbench).exists(): analog(a.cc, a.sgdk, a.cfbench, work, a.quick)
        else: print("SKIP    step 7: needs --cc, --sgdk and --cfbench (MD_CFBENCH: analog-emu's cfbench)")
    finally:
        shutil.rmtree(work, ignore_errors=True)
    print('%s' % ('FAILED: ' + ', '.join(failed) if failed else 'md-st: all steps that ran passed'))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
