"""Run production playhead selection with native RAM/patch boundaries doubled.

The Windows x86 host check needs no firmware; the stock ColdFire differential
and header/persistence checks remain in test_st_playhead_emu.py.
"""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def verify(cc):
    out = ROOT / 'out/playhead-tests'
    out.mkdir(parents=True, exist_ok=True)
    exe = out / 'playhead.exe'
    subprocess.run([str(cc), '-O2', '-Wall', '-Wextra', '-Werror',
                    '-Wl,--large-address-aware', str(ROOT / 'tests/test_st_playhead.c'),
                    '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', type=Path, required=True)
    verify(parser.parse_args().cc)
