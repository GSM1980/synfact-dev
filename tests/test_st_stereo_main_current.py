"""Stereo MAIN differential and independent vectors on analog-emu's CPU core.

Uses the owner's cfbench executable, SGDK 2.11, and two existing SMZ banks.
All generated images/fixtures stay under --out; no device IO or stock OS input.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader.sdk.elf import Elf
from elekloader.devices.syntakt import DEVICE

BASE, HELPER, CONFIG, VECTORS, OLD = 0x40400000, 0x40500000, 0x405f0000, 0x40600000, 0x40800000


def pack(values):
    return struct.pack('>%dI' % len(values), *[v & 0xffffffff for v in values])


def signed(n):
    return (n+0x80000000) % 0x100000000-0x80000000


def bank_image(path, base):
    raw = path.read_bytes();n, nr = struct.unpack_from('>II', raw, 52)
    data = bytearray(raw[80+4*nr:80+4*nr+n])
    for off in struct.unpack_from('>%dI' % nr, raw, 80):
        struct.pack_into('>I', data, off, struct.unpack_from('>I', data, off)[0]+base)
    e = Elf.load(path.with_suffix('.work')/'main.elf')
    symbols = {s.name: base+s.value for s in e.symbols if s.name}
    descriptor = symbols['st_stereo_module']-base
    _, init, subs, count = struct.unpack_from('>4I', data, descriptor)
    rows = [struct.unpack_from('>2I', data, subs-base+8*i) for i in range(count)]
    assert [r[0] for r in rows[:4]] == [5, 13, 14, 10]
    return data, symbols, init, [r[1] for r in rows[:4]], rows[4][1] if count == 5 else 0


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bank', type=Path, required=True)
    p.add_argument('--reference-bank', type=Path, required=True)
    p.add_argument('--cfbench', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--require-no-count-regressions', action='store_true', help='reject any measured full-block instruction increase')
    p.add_argument('--sram-disabled', action='store_true', help='assert that SRAM remains off in the diagnostic bank')
    a = p.parse_args();a.out.mkdir(parents=True, exist_ok=True)
    prefix = os.environ.get('ELEKLOADER_CROSS', DEVICE.toolchain['prefix'])
    sgdk = Path(os.environ['SGDK_BIN'])
    def tool(name):
        return str(sgdk/(prefix+name+('.exe' if os.name == 'nt' else '')))
    def run(cmd):
        r = subprocess.run([str(x) for x in cmd], cwd=ROOT, capture_output=True, text=True)
        if r.stderr:
            print(r.stderr, file=sys.stderr)
        r.check_returncode()
    co, so, executable, binary, linker = [a.out/x for x in ('helper-c.o', 'helper-s.o', 'helper.elf', 'helper.bin', 'helper.ld')]
    run([tool('gcc'), *DEVICE.toolchain['cflags'], '-fno-jump-tables', '-c', 'tests/st_stereo_cfbench.c', '-o', co])
    run([tool('as'), '-mcpu=54415', 'tests/st_stereo_cfbench.s', '-o', so])
    linker.write_text('SECTIONS { . = 0x40500000; .image : { *(.text*) *(.rodata*) *(.data*) *(.bss*) *(COMMON) } }\n')
    run([tool('ld'), '--emit-relocs', '-T', linker, '-o', executable, co, so])
    run([tool('objcopy'), '-O', 'binary', '-j', '.image', executable, binary])
    hs = {s.name: s.value for s in Elf.load(executable).symbols if s.name}
    new, ns, ni, nc, nt = bank_image(a.bank, BASE)
    old, osy, oi, oc, _ = bank_image(a.reference_bank, OLD)
    image = bytearray(OLD-BASE+len(old));image[:len(new)] = new;image[OLD-BASE:] = old
    helper = binary.read_bytes();image[HELPER-BASE:HELPER-BASE+len(helper)] = helper
    path = a.out/'fixture.bin';results = {}
    def call(label, fn, args, negative=False, setup=None):
        path.write_bytes(image)
        command = [str(a.cfbench), str(path), f'{BASE:x}', *(setup or []), f'{fn:x}:'+','.join(f'{v:x}' for v in args)]
        r = subprocess.run(command, capture_output=True, text=True, check=True)
        matches = re.findall(r'-> d0 ([0-9a-f]+): (\d+) instructions', r.stdout)
        assert matches and all(int(m[0], 16) == 0 for m in matches[:-1]), (label, r.stdout, r.stderr)
        value, count = int(matches[-1][0], 16), int(matches[-1][1])
        assert bool(value) == negative, (label, hex(value), r.stdout)
        results[label] = dict(result=value, instructions_including_fixture=count)
        print('PASS', label, value, flush=True)
    scenarios = [('DDR', 0), ('SRAM_then_fallback', 1), ('Chorus_owns_SRAM', 3), ('occupied_state', 5), ('occupied_code', 9)]
    if a.sram_disabled:
        scenarios[1] = ('SRAM_disabled', 17)
    for label, mode in scenarios:
        cfg = pack([*oc, *nc, osy['st_stereo_track_d'], ns['st_stereo_track_d'], oi, ni, nt, ns['st_stereo_sram'], mode, 384])
        image[CONFIG-BASE:CONFIG-BASE+len(cfg)] = cfg
        call('384_callback_blocks_'+label, hs['st_probe_callbacks'], [CONFIG])
    if 'remember_kernel' in ns:
        # Keep all callbacks alive but suppress only the empty-block history
        # commit. The first later stereo block must expose the missing ramp.
        cfg=pack([*oc,*nc,osy['st_stereo_track_d'],ns['st_stereo_track_d'],oi,ni,nt,ns['st_stereo_sram'],0,384])
        image[CONFIG-BASE:CONFIG-BASE+len(cfg)]=cfg
        at=ns['remember_kernel']-BASE;original=image[at:at+4]
        image[at:at+4]=pack([hs['st_probe_noop']])
        call('missing_empty_history_negative_control',hs['st_probe_callbacks'],[CONFIG],negative=True)
        image[at:at+4]=original
    # Instruction counts only; independent of the physical CPU meter. Prime
    # both banks first, then count the same four callbacks with stable gains.
    bench={}
    cfg=pack([*oc,*nc,osy['st_stereo_track_d'],ns['st_stereo_track_d'],oi,ni,nt,ns['st_stereo_sram'],0,0])
    image[CONFIG-BASE:CONFIG-BASE+len(cfg)]=cfg;path.write_bytes(image)
    for mode in (0,1):
        for sends in (0,1,2,3,4,8):
            steps=[f"{hs['st_probe_bench_prepare']:x}:{CONFIG:x},{(1<<sends)-1:x},{mode:x}"]
            steps += [f"{hs['st_probe_bench_next']:x}",*[f'{f:x}' for f in oc],*[f'{f:x}' for f in nc]]
            steps += [f"{hs['st_probe_bench_next']:x}",*[f'{f:x}' for f in oc],*[f'{f:x}' for f in nc]]
            r=subprocess.run([str(a.cfbench),str(path),f'{BASE:x}',*steps],capture_output=True,text=True,check=True)
            matches=re.findall(r'-> d0 ([0-9a-f]+): (\d+) instructions',r.stdout)
            assert matches and int(matches[0][0],16)==0, r.stdout
            counts=[int(x[1]) for x in matches[-8:]]
            row=dict(reference=counts[:4],optimized=counts[4:],reference_total=sum(counts[:4]),optimized_total=sum(counts[4:]))
            bench[f'{sends}_sends_'+('SRAM' if mode else 'DDR')]=row
            print('COUNTS',sends,'sends',mode,row,flush=True)
    dynamic={}
    for sends in (0,2,8):
        for name,action in (('all_active_gains_change',1),('last_gain_changes',2),('mask_changes',3)):
            for phase in ('first_change','continuous'):
                steps=[f"{hs['st_probe_bench_prepare']:x}:{CONFIG:x},{(1<<sends)-1:x},0"]
                for _ in range(2):
                    steps += [f"{hs['st_probe_bench_next']:x}",*[f'{f:x}' for f in oc],*[f'{f:x}' for f in nc]]
                if phase=='continuous':
                    steps += [f"{hs['st_probe_bench_dynamic']:x}:{action:x}",*[f'{f:x}' for f in oc],*[f'{f:x}' for f in nc]]
                steps += [f"{hs['st_probe_bench_dynamic']:x}:{action:x}",*[f'{f:x}' for f in oc],*[f'{f:x}' for f in nc]]
                r=subprocess.run([str(a.cfbench),str(path),f'{BASE:x}',*steps],capture_output=True,text=True,check=True)
                matches=re.findall(r'-> d0 ([0-9a-f]+): (\d+) instructions',r.stdout)
                assert matches and int(matches[0][0],16)==0,r.stdout
                counts=[int(x[1]) for x in matches[-8:]]
                row=dict(reference_total=sum(counts[:4]),optimized_total=sum(counts[4:]),reference=counts[:4],optimized=counts[4:])
                dynamic[f'{sends}_sends_{name}_{phase}']=row
                print('DYNAMIC',sends,name,phase,row,flush=True)
    output = dict(backend='analog-emu cfbench', cfbench_sha256=hashlib.sha256(a.cfbench.read_bytes()).hexdigest(),
                  bank_sha256=hashlib.sha256(a.bank.read_bytes()).hexdigest(), results=results,
                  reference_bank_sha256=hashlib.sha256(a.reference_bank.read_bytes()).hexdigest(),
                  stable_block_instruction_counts=bench,
                  dynamic_block_instruction_counts=dynamic,
                  device_access=False, hardware_cycles_measured=False,
                  sram_disabled=a.sram_disabled,
                  skipped=['SRAM kernel execution: intentionally disabled'] if a.sram_disabled else [])
    (a.out/'results.json').write_text(json.dumps(output, indent=2)+'\n')
    if a.require_no_count_regressions:
        for name,row in [*bench.items(),*dynamic.items()]:
            assert row['optimized_total']<=row['reference_total'],('instruction regression',name,row)
    print('ALL PASS: bit-exact callbacks against current-main bank, DDR/SRAM/fallback/Chorus/occupied windows')


if __name__ == '__main__':
    main()
