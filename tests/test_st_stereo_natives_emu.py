"""The stereo engine's replacements for native AUDIO calls, against the natives, in analog-emu.

tests/st_stereo_natives_check.c is built with the bank's compiler and flags,
linked after a core build's AUDIO image (audio.bin, which holds the stock
chain routines) and run in analog-emu's cfbench: its ColdFire core models the
EMAC on its physical registers, checked against the Programmer's Reference
Manual, and runs the Syntakt's own DSP program. (The patched Unicorn the other
emulator tests use gets the EMAC's arithmetic wrong in both modes the audio
engine uses, so it cannot judge EMAC code.)

Each check runs the native routine and the replacement on the same random
chain state and buffers and compares every output word; it fails on any
difference, or when the replacement never ran. It prints the instruction
counts of both for one R pass of eight stereo tracks.

Usage: test_st_stereo_natives_emu.py --core <core build folder> --cfbench <cfbench executable>
  (cfbench: analog-emu's target/release/cfbench; or set CFBENCH)
Needs the m68k compiler on PATH (docs/SYNTAKT-BUILD.md). Without cfbench or
the compiler it prints SKIPPED and exits 2. No device access.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader.devices.syntakt import DEVICE
from elekloader.sdk import elf

LOAD, HARNESS = 0x40000400, 0x40100000
CASES = 400
UNIT = {'drive': 'one R pass of 8 stereo tracks', 'voice': 'one stereo track', 'multi': 'one track'}


def skipped(why):
    print('SKIPPED: ' + why)
    sys.exit(2)


def build(work, audio, source):
    prefix = os.environ.get('ELEKLOADER_CROSS', DEVICE.toolchain['prefix'])
    if not shutil.which(prefix + 'gcc'):
        skipped('no %sgcc on PATH (docs/SYNTAKT-BUILD.md)' % prefix)
    work = work / Path(source).stem
    work.mkdir()
    obj = work / 'check.o'
    subprocess.run([prefix + 'gcc', *DEVICE.toolchain['cflags'], '-fno-jump-tables', '-Wno-array-bounds',
                    '-I', str(ROOT / 'elekloader/sdk/include'), '-c', str(ROOT / source),
                    '-o', str(obj)], check=True)
    script = work / 'check.ld'
    script.write_text('SECTIONS { .image 0x%x : { *(.text*) *(.rodata*) *(.data*) BYTE(0) . = ALIGN(4); '
                      '*(.bss*) *(COMMON) . = ALIGN(4); } /DISCARD/ : { *(.comment) *(.note*) *(.gnu.attributes) } }\n'
                      % HARNESS)
    exe = work / 'check.elf'
    subprocess.run([prefix + 'ld', '-T', str(script), '-o', str(exe), str(obj)], check=True)
    raw = work / 'check.bin'
    subprocess.run([prefix + 'objcopy', '-O', 'binary', '-j', '.image', str(exe), str(raw)], check=True)
    e = elf.Elf.load(exe)
    sec = e.section('.image')
    syms = {s.name: s.value for s in e.symbols if s.name and s.shndx == sec.idx}
    if len(audio) > HARNESS - LOAD:
        raise SystemExit('the AUDIO image reaches the harness address')
    image = work / 'image.bin'
    image.write_bytes(audio + bytes(HARNESS - LOAD - len(audio)) + raw.read_bytes())
    return image, syms


def cfbench(tool, image, *steps):
    out = subprocess.run([str(tool), str(image), '%x' % LOAD, *steps], capture_output=True, text=True, timeout=600)
    if out.returncode:
        raise SystemExit('cfbench failed: ' + out.stderr.strip())
    return re.findall(r'-> d0 ([0-9a-f]{8}): (\d+) instructions', out.stdout)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--core', type=Path, required=True)
    ap.add_argument('--cfbench', type=Path, default=os.environ.get('CFBENCH'))
    a = ap.parse_args()
    if not a.cfbench or not Path(a.cfbench).is_file():
        skipped('no cfbench (analog-emu target/release/cfbench; --cfbench or CFBENCH)')
    audio = (a.core / 'audio.bin').read_bytes()
    failed = False
    with tempfile.TemporaryDirectory() as tmp:
        image, syms = build(Path(tmp), audio, 'tests/st_stereo_natives_check.c')
        for name in ('drive', 'voice', 'multi'):
            result = cfbench(a.cfbench, image, '%x:%x,%x' % (syms['check_' + name], CASES, 0x5eed))
            word = int(result[0][0], 16)
            bad, ran = word >> 16, word & 0xffff
            native, replacement = cfbench(a.cfbench, image, '%x' % syms['bench_%s_native' % name],
                                          '%x' % syms['bench_%s_replacement' % name])
            print('%s: %d cases, replacement ran in %d, %d differing words; %s: native %s, replacement %s '
                  'instructions' % (name, CASES, ran, bad, UNIT[name], native[1], replacement[1]))
            failed |= bad != 0 or ran == 0
        # the claim's release: every multimode type's tail falls under the
        # quiet threshold after a burst (exact 0 is not reached by all)
        tails = [int(cfbench(a.cfbench, image, '%x:%x' % (syms['check_tail'], ty))[0][0], 16) for ty in range(9)]
        print('tails after a burst (blocks to quiet / to 0): ' + ', '.join(
            '%s %d/%s' % ('render' if ty == 8 else 'type %d' % ty, w >> 16, 'never' if w & 0xffff == 0xffff else w & 0xffff)
            for ty, w in enumerate(tails)))
        failed |= any(w >> 16 == 0xffff for w in tails)
        # MAIN: the STEREO module's EMAC mix against a 64-bit model
        image, syms = build(Path(tmp), audio, 'tests/st_stereo_main_check.c')
        result = cfbench(a.cfbench, image, '%x:%x,%x' % (syms['check_mix'], CASES, 0x5eed))
        word = int(result[0][0], 16)
        bad, emac = word >> 16, word & 0xffff
        cost = cfbench(a.cfbench, image, '%x' % syms['bench_mix'])
        print('main mix: %d cases, %d frames off the model, %d EMAC states or ramps not kept; '
              'one bus, 8 tracks with D: %s instructions' % (CASES, bad, emac, cost[0][1]))
        failed |= bad != 0 or emac != 0
        # the samplers' PCM runs, hand-written loops included, against the
        # per-sample model, both built for the target
        image, syms = build(Path(tmp), audio, 'tests/st_sampler_pcm_check.c')
        result = cfbench(a.cfbench, image, '%x:%x,%x' % (syms['check_pcm'], 2000, 12345))
        word = int(result[0][0], 16)
        bad, blocks = word >> 16, word & 0xffff
        costs = [cfbench(a.cfbench, image, '%x:%x' % (syms['bench_pcm_setup'], channels),
                         '%x' % syms['bench_pcm'])[1][1] for channels in (1, 2)]
        print('sampler PCM: %d blocks, %d differing words or cursors; a block at unity pitch: '
              'mono %s, stereo %s instructions' % (blocks, bad, costs[0], costs[1]))
        failed |= bad != 0 or blocks == 0
    if failed:
        print('FAIL')
        sys.exit(1)
    print('PASS: the replacements give the natives\' output word for word')


if __name__ == '__main__':
    main()
