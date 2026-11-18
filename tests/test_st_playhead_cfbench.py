"""Compare real step-helper outputs and instruction cost on analog-emu.

No firmware input or device access. This calls the real bank's register-ABI
mirror/peek helpers, including their fast/slow paths. The unchanged C selector
and stubs are covered separately by the host and stock Unicorn differential.
Instruction counts are not measured cycles or hardware CPU percentages.
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
from elekloader.devices.syntakt import DEVICE
from elekloader.sdk.elf import Elf

BASE, HELPER, NEW, PATTERN = 0x40400000, 0x40500000, 0x40800000, 0x41000000
NAMES = ('FORWARD', 'REVERSE', 'PENDULUM', 'RANDOM', 'WALK', 'ANCHOR', 'STUTTER', 'FOLD', 'WALK+')


def bank(path, base):
    raw = path.read_bytes()
    n, nr = struct.unpack_from('>II', raw, 52)
    data = bytearray(raw[80 + 4 * nr:80 + 4 * nr + n])
    for off in struct.unpack_from('>%dI' % nr, raw, 80):
        struct.pack_into('>I', data, off, struct.unpack_from('>I', data, off)[0] + base)
    syms = {s.name: s.value + base for s in Elf.load(path.with_suffix('.work') / 'main.elf').symbols if s.name}
    return data, syms


def verify(reference, current, cfbench, out):
    out.mkdir(parents=True, exist_ok=False)
    sgdk = Path(os.environ['SGDK_BIN'])
    prefix = os.environ.get('ELEKLOADER_CROSS', DEVICE.toolchain['prefix'])

    def run(args):
        r = subprocess.run([str(x) for x in args], cwd=ROOT, capture_output=True, text=True)
        if r.returncode:
            raise RuntimeError(r.stdout + r.stderr)
        return r.stdout

    def tool(name):
        return sgdk / (prefix + name + ('.exe' if os.name == 'nt' else ''))

    obj, abi, exe, ld, raw = [out / n for n in ('probe.o', 'abi.o', 'probe.elf', 'probe.ld', 'probe.bin')]
    run([tool('as'), '-mcpu=54415', 'tests/st_playhead_cfbench.s', '-o', obj])
    run([tool('as'), '-mcpu=54415', 'tests/st_stereo_cfbench.s', '-o', abi])
    ld.write_text(f'SECTIONS {{ .image 0x{HELPER:x} : {{ *(.text*) *(.rodata*) *(.data*) *(.bss*) }} }}\n')
    run([tool('ld'), '-T', ld, '-o', exe, obj, abi])
    run([tool('objcopy'), '-O', 'binary', '-j', '.image', exe, raw])
    h = {s.name: s.value for s in Elf.load(exe).symbols if s.name}
    old, osym = bank(reference, BASE)
    new, nsym = bank(current, NEW)
    data = bytearray(old)

    def put(at, value):
        off = at - BASE
        if len(data) < off + len(value):
            data.extend(bytes(off + len(value) - len(data)))
        data[off:off + len(value)] = value

    put(HELPER, raw.read_bytes())
    put(NEW, new)
    image = out / 'playhead.bin'
    image.write_bytes(data)
    mac = struct.pack('>8I', 0xb65, 0x12345678, 0xfedcba98, 0x76543210,
                      0x89abcdef, 0xa5c33c5a, 0x55aa33cc, 0xfffff00f)
    all_costs = []
    comparisons = 0
    for length, track, flag in ((2, 0, 0), (16, 0, 0), (64, 12, 1)):
        for mode, name in enumerate(NAMES):
            samples = min(4 * length, 32)
            total = 4 * samples
            steps = [f'poke:43bd8430:{PATTERN:08x}', 'poke:43bd843c:41adcfac',
                     'poke:43bcc3a4:01234567', f'poke:{PATTERN+0x21379:x}:{flag:02x}',
                     f'poke:{PATTERN+0x21374:x}:{length:04x}',
                     f'poke:{PATTERN+0x499*track+969:x}:{length:04x}',
                     f'{h["st_playhead_probe"]:x}:{h["st_playhead_identity"]:x},0,0,{PATTERN:x}',
                     f'poke:40600000:{mac.hex()}', f'{h["st_probe_mac_set"]:x}:40600000']
            labels = []
            for sym in (osym, nsym):
                steps.append(f'poke:{sym["playhead_modes"]+16*5+track:x}:{mode:02x}')
                for i in range(samples):
                    j = i % length
                    for entry, count, label in (
                            ('playhead_mirror', j, 'advance' if i else 'start'),
                            ('playhead_mirror', j, 'cached'),
                            ('playhead_peek', j, 'peek'),
                            ('playhead_peek', (j-1) % length, 'previous')):
                        steps.append(f'{h["st_playhead_probe"]:x}:{sym[entry]:x},{track:x},{count:x},{PATTERN:x}')
                        labels.append(label)
                # Check all C callee saves with the existing sentinel probe.
                steps.append(f'{h["st_probe_call"]:x}:{h["st_playhead_probe"]:x},'
                             f'{sym["playhead_mirror"]:x},{track:x},{j:x},{PATTERN:x}')
            steps += [f'{h["st_probe_mac_get"]:x}:40600100', 'dump:40600100:16']
            output = run([cfbench, image, f'{BASE:x}', *steps])
            rows = [(int(v, 16), int(n)) for v, n in re.findall(r'-> d0 ([0-9a-fA-F]+): (\d+) instructions', output)]
            overhead = rows[0][1] - 1
            oldrows, newrows = rows[2:2+total], rows[3+total:3+2*total]
            assert rows[2+total][0] == rows[3+2*total][0] == 0, 'callee-save ABI'
            assert [v for v, _ in oldrows] == [v for v, _ in newrows], (name, length, output)
            shown = re.search(r'dump 40600100 x16: peak \d+; (.*?) \.\.\.', output).group(1)
            assert [int(x) for x in shown.split()] == list(struct.unpack('>16h', mac)), 'EMAC context'
            costs = {}
            for label in ('start', 'advance', 'cached', 'peek', 'previous'):
                ids = [i for i, v in enumerate(labels[:total]) if v == label]
                costs[label] = {side: round(sum(rows_[i][1]-overhead for i in ids)/len(ids), 2)
                                for side, rows_ in (('reference', oldrows), ('optimized', newrows))}
            # The real UI hook executes twice as much lookup work per tick
            # in the reference. Supply its native register context and an RTS
            # at the continuation boundary; no other native code is replaced.
            ui_steps = steps[:6] + ['poke:4008d23a:4e75',
                f'{h["st_playhead_ui_probe"]:x}:{h["st_playhead_identity"]:x},0,{PATTERN:x}',
                f'poke:40600000:{mac.hex()}', f'{h["st_probe_mac_set"]:x}:40600000']
            for sym in (osym, nsym):
                ui_steps.append(f'poke:{sym["playhead_modes"]+16*5+track:x}:{mode:02x}')
                for i in range(8):
                    j = i % length
                    ui_steps += [f'poke:{0x43bd83fc+track:x}:{j:02x}',
                                 f'poke:{0x43bd8416+track:x}:{((j-1)%length if i else 0):02x}']
                    ui_steps += [f'{h["st_playhead_ui_probe"]:x}:{sym["playhead_ui_copy"]:x},{track:x},{PATTERN:x}'] * 6
                ui_steps.append(f'{h["st_probe_call"]:x}:{h["st_playhead_ui_probe"]:x},'
                                f'{sym["playhead_ui_copy"]:x},{track:x},{PATTERN:x},0,0')
            ui_steps += [f'{h["st_probe_mac_get"]:x}:40600100', 'dump:40600100:16']
            ui_output = run([cfbench, image, f'{BASE:x}', *ui_steps])
            ui_rows = [(int(v, 16), int(n)) for v, n in re.findall(r'-> d0 ([0-9a-fA-F]+): (\d+) instructions', ui_output)]
            ui_overhead = ui_rows[0][1] - 1
            ui_old, ui_new = ui_rows[2:50], ui_rows[51:99]
            assert ui_rows[50][0] == ui_rows[99][0] == 0, 'UI C ABI'
            assert [v for v, _ in ui_old] == [v for v, _ in ui_new], (name, length, ui_output)
            shown = re.search(r'dump 40600100 x16: peak \d+; (.*?) \.\.\.', ui_output).group(1)
            assert [int(x) for x in shown.split()] == list(struct.unpack('>16h', mac)), 'UI EMAC context'
            ui_costs = {}
            for label, ids in (('start', [0]), ('advance', range(6, 48, 6)),
                               ('cached', [i for i in range(48) if i % 6])):
                ui_costs[label] = {side: round(sum(rows_[i][1]-ui_overhead for i in ids)/len(ids), 2)
                                   for side, rows_ in (('reference', ui_old), ('optimized', ui_new))}
            all_costs.append(dict(mode=name, length=length, track=track, per_track_scale=bool(flag),
                                 instructions=costs, ui_hook_instructions=ui_costs))
            comparisons += len(oldrows)
            comparisons += len(ui_old)
    negative = [f'poke:43bd8430:{PATTERN:08x}', 'poke:43bd843c:41adcfac',
                f'poke:{PATTERN+0x21374:x}:0010',
                f'poke:{osym["playhead_modes"]+80:x}:07', 'poke:4008d23a:4e75',
                f'{h["st_playhead_ui_probe"]:x}:{osym["playhead_ui_copy"]:x},0,{PATTERN:x}',
                f'poke:{nsym["playhead_ui_copy"]:x}:4e75', 'poke:444e337c:7f',
                f'{h["st_playhead_ui_probe"]:x}:{nsym["playhead_ui_copy"]:x},0,{PATTERN:x}']
    broken = run([cfbench, image, f'{BASE:x}', *negative])
    values = [int(v, 16) for v in re.findall(r'-> d0 ([0-9a-fA-F]+):', broken)]
    assert values == [0, 127], ('broken UI hook must be detected', broken)
    report = dict(ok=True, backend='analog-emu cfbench interpreter', identical_outputs=comparisons,
                  callee_saves=True, full_emac_preserved=True, hardware_cycles_measured=False,
                  broken_ui_negative_control_detected=True,
                  cfbench_sha256=hashlib.sha256(cfbench.read_bytes()).hexdigest(),
                  reference_bank_sha256=hashlib.sha256(reference.read_bytes()).hexdigest(),
                  bank_sha256=hashlib.sha256(current.read_bytes()).hexdigest(), cases=all_costs)
    (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS: {comparisons} identical step/peek outputs, C ABI and EMAC context; {len(all_costs)} cost cases')
    for row in all_costs[9:18]:
        print(row['mode'], json.dumps(dict(helper=row['instructions'], ui=row['ui_hook_instructions'])))


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--reference-bank', required=True, type=Path)
    p.add_argument('--bank', required=True, type=Path)
    p.add_argument('--cfbench', required=True, type=Path)
    p.add_argument('--out', required=True, type=Path)
    a = p.parse_args()
    verify(a.reference_bank, a.bank, a.cfbench, a.out)
