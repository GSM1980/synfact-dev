"""Offline Digi Mono adapter/DSP tests; no firmware or hardware required."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc', type=Path, required=True)
    p.add_argument('--out', type=Path, default=Path('out/digimono-tests'))
    a = p.parse_args()
    upstream = ROOT / 'mods/digimono-st/upstream'
    provenance = json.loads((upstream / 'provenance.json').read_text())
    for name, digest in provenance['sha256'].items():
        assert hashlib.sha256((upstream / name).read_bytes()).hexdigest() == digest, name
    print('PASS: unchanged upstream engine source hashes', flush=True)
    a.out.mkdir(parents=True, exist_ok=True)
    exe = (a.out / 'digimono.exe').resolve()
    subprocess.run([str(a.cc), '-O2', '-Wall', '-Werror', '-I', 'elekloader/sdk/include',
                    '-I', 'mods/digimono-st', 'tests/test_st_digimono.c',
                    'mods/digimono-st/engine.c', 'mods/digimono-st/sine.c',
                    '-o', str(exe)], cwd=ROOT, check=True)
    subprocess.run([str(exe)], check=True)
    pages=(a.out/'digimono-pages.exe').resolve()
    subprocess.run([str(a.cc),'-O2','-Wall','-Werror','-Wl,--large-address-aware',
                    '-I','elekloader/sdk/include','tests/test_st_digimono_pages.c',
                    '-o',str(pages)],cwd=ROOT,check=True)
    subprocess.run([str(pages)],check=True)


if __name__ == '__main__':
    main()
