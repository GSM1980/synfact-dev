"""COMP 0.3's detector loops (SCF high-pass and plain peak) in analog-emu.

tests/st_comp_scf_check.c is built with the bank's compiler and flags,
linked with mods/comp-st/dsp.s, and run in analog-emu's cfbench, whose
ColdFire core models the EMAC on its physical registers (the patched Unicorn
the other COMP tests use is not trusted for EMAC arithmetic). It checks:

- comp_hp (lp, peak) and comp_peak against a 64-bit model, every word, on
  noise, squares, rails and silence, across the 127 cutoffs;
- scf-tables.h against its generator (a = 1 - exp(-2 pi fc / 48000));
- the steady response of seven positions (sine peaks through the loop);
- the SRAM block size, and that one block of the SCF loop costs the same
  number of instructions at every cutoff.

Usage: test_st_comp_scf_emu.py [--cfbench <cfbench>]   (or set CFBENCH)
Needs the m68k compiler on PATH (docs/SYNTAKT-BUILD.md). Without cfbench or
the compiler it prints SKIPPED and exits 2. No device access.
"""
import argparse
import math
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

LOAD = 0x40000400
sys.path.insert(0, str(ROOT / 'mods/comp-st/tools'))
import generate_config_tables as tables
POSITIONS = [1, 19, 42, 65, 86, 111, 127]           # 60, 90, 150, 250, 400, 700, 1000 Hz


def skipped(why):
    print('SKIPPED: ' + why)
    sys.exit(2)


def coefficients():
    return tables.scf_coefficients()


def source_matches():
    return (ROOT / 'mods/comp-st/scf-tables.h').read_text() == tables.scf_generated()


def build(work):
    prefix = os.environ.get('ELEKLOADER_CROSS', DEVICE.toolchain['prefix'])
    if not shutil.which(prefix + 'gcc'):
        skipped('no %sgcc on PATH (docs/SYNTAKT-BUILD.md)' % prefix)
    table = work / 'table.s'
    table.write_text('        .section .rodata\n        .globl comp_scf_check_coefficients\n'
                     'comp_scf_check_coefficients:\n' +
                     ''.join('        .long 0x%08x\n' % c for c in coefficients()))
    objects = []
    for src, kind in ((ROOT / 'tests/st_comp_scf_check.c', 'c'), (ROOT / 'mods/comp-st/dsp.s', 's'), (table, 's')):
        obj = work / (src.name + '.o')
        if kind == 'c':
            cmd = [prefix + 'gcc', *DEVICE.toolchain['cflags'], '-fno-jump-tables', '-c', str(src), '-o', str(obj)]
        else:
            cmd = [prefix + 'as', *DEVICE.toolchain['asflags'], '-I', str(ROOT / 'mods/comp-st'), str(src), '-o', str(obj)]
        subprocess.run(cmd, check=True)
        objects.append(str(obj))
    script = work / 'check.ld'
    script.write_text('SECTIONS { .image 0x%x : { *(.text*) *(.rodata*) *(.data*) BYTE(0) . = ALIGN(4); '
                      '*(.bss*) *(COMMON) . = ALIGN(4); } /DISCARD/ : { *(.comment) *(.note*) *(.gnu.attributes) } }\n'
                      % LOAD)
    exe = work / 'check.elf'
    subprocess.run([prefix + 'ld', '-T', str(script), '-o', str(exe), *objects], check=True)
    image = work / 'image.bin'
    subprocess.run([prefix + 'objcopy', '-O', 'binary', '-j', '.image', str(exe), str(image)], check=True)
    e = elf.Elf.load(exe)
    sec = e.section('.image')
    return image, {s.name: s.value for s in e.symbols if s.name and s.shndx == sec.idx}


def cfbench(tool, image, *steps):
    out = subprocess.run([str(tool), str(image), '%x' % LOAD, *steps], capture_output=True, text=True, timeout=600)
    if out.returncode:
        raise SystemExit('cfbench failed: ' + out.stderr.strip())
    return [(int(w, 16), int(n)) for w, n in re.findall(r'-> d0 ([0-9a-f]{8}): (\d+) instructions', out.stdout)]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cfbench', type=Path, default=os.environ.get('CFBENCH'))
    a = ap.parse_args()
    if not a.cfbench or not Path(a.cfbench).is_file():
        skipped('no cfbench (analog-emu target/release/cfbench; --cfbench or CFBENCH)')
    failed = False
    same = source_matches()
    print('coefficients: scf-tables.h %s its generator' % ('matches' if same else 'DIFFERS FROM'))
    failed |= not same
    with tempfile.TemporaryDirectory() as tmp:
        image, syms = build(Path(tmp))
        size = syms['comp_detector_end'] - syms['comp_detector_start']
        print('SRAM detector block: %d bytes (window 244)' % size)
        failed |= size != 244
        blocks = 64 * 6 * 7
        word, _ = cfbench(a.cfbench, image, '%x:%x,%x' % (syms['check_scf'], blocks, 0x5eed))[0]
        print('loops vs 64-bit model: %d blocks, %d differing' % (word & 0xffff, word >> 16))
        failed |= word >> 16 != 0 or word & 0xffff != blocks
        (_, peak), = cfbench(a.cfbench, image, '%x' % syms['bench_peak'])
        counts = {k: cfbench(a.cfbench, image, '%x:%x' % (syms['bench_hp'], k))[0][1] for k in (1, 42, 65, 127)}
        print('one block (64 samples) incl. call shim: plain peak %d; SCF peak at positions %s: %s instructions'
              % (peak, list(counts), [counts[k] for k in counts]))
        # The loop has no cutoff-dependent path; only the peak's "new
        # maximum" store depends on the filtered data, a few per block.
        failed |= max(counts.values()) - min(counts.values()) > 8
        # The loop is y = x - lp[n-1], lp[n] = lp[n-1] + a*y:
        # H(z) = (1 - z^-1) / (1 - (1-a) z^-1), a little above 0 dB near
        # Nyquist for the high positions (+0.55 dB at 1 kHz's a).
        print('steady sine peak through each position, dB (exact one-pole response in brackets):')
        rows = []
        for k in POSITIONS:
            fc = tables.scf_cutoffs()[k - 1]
            cells = []
            for f in (30, 50, 100, 200, 1000, 5000):
                w = round(2 * math.sin(math.pi * f / 48000) * 2 ** 30)
                ref = cfbench(a.cfbench, image, '%x:0,%x' % (syms['response'], w))[0][0]
                out = cfbench(a.cfbench, image, '%x:%x,%x' % (syms['response'], k, w))[0][0]
                got = 20 * math.log10(out / ref)
                c = coefficients()[k] / 2 ** 31
                z = complex(math.cos(2 * math.pi * f / 48000), -math.sin(2 * math.pi * f / 48000))
                ideal = 20 * math.log10(abs((1 - z) / (1 - (1 - c) * z)))
                cells.append('%dHz %.1f (%.1f)' % (f, got, ideal))
                failed |= abs(got - ideal) > 0.3
            rows.append('  %4.0f Hz: ' % fc + ', '.join(cells))
        print('\n'.join(rows))
    print('FAIL' if failed else 'PASS comp-st SCF detector; no device access')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
