"""Execute linked LFO3 callbacks on analog-emu; no device I/O or cycle claims.

Inputs are a core build and two bank/ELF pairs. Preparation is outside the
measured calls. The callback, selector and kernel each include the same small
fixture argument wrapper. Output includes raw counts and pinned input hashes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader.sdk.elf import Elf, STT_FILE
from elekloader.devices.syntakt import DEVICE

BASE, HELPER, CONFIG = 0x40000400, 0x40500000, 0x405f0000
BANKS = (0x40800000, 0x40c00000)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pack(values):
    return struct.pack('>%dI' % len(values), *values)


def bank(path, base):
    raw = path.read_bytes()
    size, nr = struct.unpack_from('>II', raw, 52)
    data = bytearray(raw[80 + 4 * nr:80 + 4 * nr + size])
    e = Elf.load(path.with_suffix('.work') / 'main.elf')
    linked=e.section('.image').data
    count=struct.unpack_from('>I',raw,44)[0]
    features=struct.unpack_from('>I',raw,24)[0]
    modules=struct.unpack_from('>I',raw,36)[0]
    directory=40*count+(32*count if features&2 else 0)+16*modules
    # Builder fills descriptor copies in its reserved directory and appends
    # mod-info records. Everything after that directory must match the ELF.
    assert bytes(data[directory:len(linked)])==linked[directory:]
    for offset in struct.unpack_from('>%dI' % nr, raw, 80):
        value = struct.unpack_from('>I', data, offset)[0]
        struct.pack_into('>I', data, offset, value + base)
    symbols = {s.name: base + s.value for s in e.symbols if s.name}
    groups, group = [], None
    for s in e.symbols:
        if s.type == STT_FILE:
            group = {}
            groups.append(group)
        elif group is not None and s.name:
            group[s.name] = base + s.value
    module = next(g for g in groups if 'ddr_bank' in g and 'render' in g)
    frame = next(g['frame'] for g in groups if 'prepare_track' in g)
    assert next(s.size for s in e.symbols if s.name=='frame' and base+s.value==frame)==282
    fields = [symbols['lfo3_render'], module['lfo3_native'], symbols['lfo3_events_bank'],
              symbols['lfo3_bank'], module['render'], symbols['lfo3_events_context'],
              symbols['lfo3_active_locks'], symbols['lfo3_params_dirty'], frame]
    return data, fields, symbols['lfo3_render_end'] - symbols['lfo3_render']


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core', type=Path, required=True)
    p.add_argument('--before', type=Path, required=True)
    p.add_argument('--after', type=Path)
    p.add_argument('--cfbench', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--quick', action='store_true')
    p.add_argument('--require-no-count-regressions',action='store_true')
    p.add_argument('--idle-policy',action='store_true',
                   help='Separate suspend-without-output oracle; keeps historical differential tests intact')
    a = p.parse_args()
    after = a.after or a.before
    a.out.mkdir(parents=True, exist_ok=True)
    if (a.out/'execution.bin').exists():
        p.error('measurement output exists; choose a new --out folder')
    prefix = os.environ.get('ELEKLOADER_CROSS', DEVICE.toolchain['prefix'])
    sgdk = Path(os.environ['SGDK_BIN'])

    def tool(n):
        return str(sgdk / (prefix + n + ('.exe' if os.name == 'nt' else '')))

    def run(args):
        result = subprocess.run(list(map(str, args)), cwd=ROOT, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        return result.stdout

    obj, asm = a.out / 'fixture.o', a.out / 'abi.o'
    fixture_source='tests/st_lfo3_idle_cfbench.c' if a.idle_policy else 'tests/st_lfo3_cfbench.c'
    run([tool('gcc'), *DEVICE.toolchain['cflags'], '-fno-jump-tables', '-c',
         ROOT / fixture_source, '-o', obj])
    run([tool('as'), *DEVICE.toolchain['asflags'], ROOT / 'tests/st_lfo3_cfbench.s', '-o', asm])
    script = a.out / 'fixture.ld'
    script.write_text('SECTIONS { .image 0x40500000 : { *(.text*) *(.rodata*) *(.data*) '
                      'BYTE(0) . = ALIGN(4); *(.bss*) *(COMMON) . = ALIGN(4); } '
                      '/DISCARD/ : { *(.comment) *(.note*) *(.gnu.attributes) } }\n')
    exe, binary = a.out / 'fixture.elf', a.out / 'fixture.bin'
    run([tool('ld'), '-T', script, '-o', exe, obj, asm])
    run([tool('objcopy'), '-O', 'binary', '-j', '.image', exe, binary])
    syms = {s.name: s.value for s in Elf.load(exe).symbols if s.name}
    source = (a.core / 'main.bin').read_bytes()
    # The exact-default specialization calls the pinned OS 1.41 TRI entry.
    assert struct.unpack_from('>I', source, 0x40245838 - BASE)[0] == 0x40118d8e
    verification = json.loads((a.core / 'verification.json').read_text())
    layout = verification['link']
    end = max(layout['ddr'][1], layout['bss'][1])
    image = bytearray(end - BASE)
    image[:len(source)] = source
    lo, hi = layout['ddr']
    offset = layout['run_load'] - BASE
    image[lo - BASE:hi - BASE] = source[offset:offset + hi - lo]
    helper = binary.read_bytes()
    assert HELPER + len(helper) < CONFIG
    image[HELPER - BASE:HELPER - BASE + len(helper)] = helper
    inputs, lengths = [], []
    for path, base in zip((a.before, after), BANKS):
        data, fields, length = bank(path, base)
        assert base + len(data) < base + 0x100000
        image[base - BASE:base - BASE + len(data)] = data
        inputs.extend(fields)
        lengths.append(length)
    if a.idle_policy:
        # Test-only old kernel: replace its uniquely pinned 13-engine counter
        # by 1. Nothing in either delivered bank is changed. Validate this
        # reference against the untouched full old kernel before using it.
        reference=bytearray(image[inputs[0]-BASE:inputs[0]-BASE+lengths[0]])
        needle=bytes.fromhex('700d2f400050')
        assert reference.count(needle)==1, 'reference must be the old 13-engine kernel'
        reference[reference.index(needle)+1]=1
        assert HELPER+len(helper)<0x405e0000
        image[0x405e0000-BASE:0x405e0000-BASE+len(reference)]=reference
    image[CONFIG - BASE:CONFIG - BASE + 80] = pack(inputs + lengths)
    fixture = a.out / 'execution.bin'
    fixture.write_bytes(image)
    del image

    def call(steps):
        result = run([a.cfbench, fixture, '%x' % BASE, *steps])
        matches = re.findall(r'-> d0 ([0-9a-f]{8}): (\d+) instructions', result)
        if not matches:
            raise RuntimeError(result)
        return [(int(v, 16), int(n)) for v, n in matches]

    def step(name, *args):
        return '%x' % syms[name] + (':' + ','.join('%x' % n for n in args) if args else '')

    checks = []
    check_cases=[
        ('native_LFO2_oracle', 'lfo_probe_native_oracle', [CONFIG, 7, 8], False),
        ('continuous_banks', 'lfo_probe_differential', [CONFIG, 256, 2 | 4 | 32, 0], False),
        ('relocated_SRAM_and_stereo_neighbors','lfo_probe_differential',[CONFIG,128,1|2|4|32,0],False),
        ('phase_fade_boundaries_native_MACSR', 'lfo_probe_edges', [CONFIG, 280], False),
        ('exact_default_control_bits_and_tempo', 'lfo_probe_default_guards', [CONFIG, 256], False),
        ('all_256_lock_masks_and_ARP_bits', 'lfo_probe_lock_masks', [CONFIG,512],False),
        ('negative_state_corruption', 'lfo_probe_differential', [CONFIG, 32, 2 | 4, 1], True),
    ]
    if a.idle_policy:
        check_cases=[
            ('single_engine_reference_validation','lfo_idle_reference_check',[CONFIG,280],False),
            ('candidate_native_LFO2_oracle','lfo_idle_native_oracle',[CONFIG,7,8],False),
            ('suspend_resume_all_modes_waves','lfo_idle_oracle',[CONFIG,560,0,0],False),
            ('suspend_in_SRAM_and_neighbours','lfo_idle_oracle',[CONFIG,280,1,0],False),
            ('phase_fade_tempo_boundaries','lfo_idle_oracle',[CONFIG,280,2,0],False),
            ('all_control_bits','lfo_idle_oracle',[CONFIG,256,4,0],False),
            ('all_destinations_and_depth_boundaries','lfo_idle_oracle',[CONFIG,1792,8,0],False),
            ('all_lock_masks_and_ARP_bits','lfo_idle_oracle',[CONFIG,512,16,0],False),
            ('negative_state_corruption','lfo_idle_oracle',[CONFIG,32,0,1],True),
        ]
    for name, fn, args, negative in check_cases:
        result = call([step(fn, *args)])[-1][0]
        checks.append({'name': name, 'result': result, 'ok': bool(result) == negative})
        print(name, hex(result), 'PASS' if checks[-1]['ok'] else 'FAIL', flush=True)
        if not checks[-1]['ok']:
            (a.out / 'checks.json').write_text(json.dumps(checks, indent=2) + '\n')
            raise SystemExit(1)

    scenarios = [('defaults', 0, 0, 0, 0, 0), ('silent_nondefaults', 0, 6, 0, 0, 8),
                 ('fine_depth_4001', 0, 0, 0, 0, 16),('defaults_DEST_zero',0,0,0,0,1024)]
    waves = range(7) if not a.quick else (0, 6)
    for active in (1, 4, 13):
        for wave in waves:
            scenarios.append((f'{active}_active_wave{wave}', active, wave, 1, 0, 0))
    for mode in (0,2,3,4):
        for wave in (0,6):
            scenarios.append((f'4_active_wave{wave}_mode{mode}',4,wave,mode,0,0))
    scenarios += [('one_FX', 1, 0, 1, 0, 128),
                  ('one_lock', 4, 0, 1, 1, 0), ('four_locks', 4, 0, 1, 4, 0),
                  ('all_locks', 13, 6, 1, 13, 0),
                  ('all_eight_fields_locked',13,6,1,13,2048),
                  ('changing_locks_tempo', 13, 6, 4, 13, 2 | 4 | 32 | 64),
                  ('busy', 13, 6, 1, 13, 256)]
    rows = []
    for label, count, wave, mode, locks, flags in scenarios:
        for sram in (False, True):
            sides = []
            for side in (0, 1):
                selected = flags | (1 | side << 9 if sram else 0)
                prepare = step('lfo_probe_prepare', CONFIG, count, wave, mode, locks, selected)
                stages = {}
                for kind in ('callback', 'selector', 'kernel'):
                    name = 'lfo_probe_' + kind
                    steps = [prepare]
                    for _ in range(4):
                        steps += [step('lfo_probe_next'), step(name, side)]
                    values = call(steps)
                    assert values[0][0] == 0
                    stages[kind] = [v[1] for v in values[2::2]]
                sides.append(stages)
            row = {'scenario': label, 'sram': sram, 'before': sides[0], 'after': sides[1]}
            rows.append(row)
            print(label, 'SRAM' if sram else 'DDR',
                  'callback', sides[0]['callback'], '->', sides[1]['callback'], flush=True)
    output = {'ok': True, 'checks': checks, 'measurements': rows,
              'core_sha256': sha(a.core / 'main.bin'), 'before_sha256': sha(a.before),
              'after_sha256': sha(after), 'cfbench_sha256': sha(a.cfbench),
              'fixture_sha256': sha(fixture), 'kernel_bytes': lengths,
              'idle_policy':a.idle_policy,
              'harness_sources': {name:sha(ROOT/name) for name in
                                  ('tests/st_lfo3_cfbench.c','tests/st_lfo3_cfbench.s',
                                   fixture_source,'tools/bench_st_lfo3_main.py')},
              'limits': ['Instructions, not CPU percentages or physical cycles.',
                         'No interrupts, complete scheduler, cache latency or device I/O.',
                         'Synthetic parameter/lock events; publication lifetime tests are separate.',
                         'Native oracle uses non-LFO destinations and default native LFO1.',
                         'Each measured call includes a fixed C argument wrapper.']}
    (a.out / 'results.json').write_text(json.dumps(output, indent=2) + '\n')
    if a.require_no_count_regressions:
        for row in rows:
            for kind in ('callback','selector','kernel'):
                assert all(new<=old for old,new in zip(row['before'][kind],row['after'][kind])),row
    print('PASS: baseline measurement completed; no device access')


if __name__ == '__main__':
    main()
