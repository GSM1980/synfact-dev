#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Build the offline reference against an explicitly supplied pinned checkout."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, required=True, help='Monomodule checkout, not an OS path')
    p.add_argument('--build', type=Path, required=True)
    p.add_argument('--cc', required=True)
    p.add_argument('--cxx', required=True)
    p.add_argument('--cmake', default='cmake')
    p.add_argument('--jobs', type=int, default=4)
    p.add_argument('--dsp-source', type=Path, help='reuse an existing pinned, Monomodule-patched DSP56300 checkout')
    a = p.parse_args()
    source, build = a.source.resolve(), a.build.resolve()
    provenance = json.loads((HERE/'provenance.json').read_text())
    head = subprocess.check_output(['git', '-c', f'safe.directory={source.as_posix()}', '-C', str(source),
                                    'rev-parse', 'HEAD'], text=True).strip()
    if head != provenance['reference']['commit']:
        raise SystemExit('Monomodule revision differs from provenance.json; use the pinned checkout')
    env = os.environ.copy()
    count = int(env.get('GIT_CONFIG_COUNT', '0'))
    for name in (source, build/'_deps/dsp56300-src', build/'_deps/dsp56300-src/source/asmjit'):
        env[f'GIT_CONFIG_KEY_{count}'] = 'safe.directory'
        env[f'GIT_CONFIG_VALUE_{count}'] = name.as_posix()
        count += 1
    env['GIT_CONFIG_COUNT'] = str(count)
    extra = []
    if a.dsp_source:
        dsp = a.dsp_source.resolve()
        revision = subprocess.check_output(['git', '-c', f'safe.directory={dsp.as_posix()}', '-C', str(dsp),
                                           'rev-parse', 'HEAD'], text=True).strip()
        if revision != provenance['reference']['dsp56300_commit']:
            raise SystemExit('DSP56300 revision differs from provenance.json')
        extra.append(f'-DMNM_DSP56300_DIR={dsp.as_posix()}')
    subprocess.run([a.cmake, '-S', str(source), '-B', str(build), '-G', 'Ninja',
                    f'-DCMAKE_C_COMPILER={a.cc}', f'-DCMAKE_CXX_COMPILER={a.cxx}',
                    '-DMNM_BUILD_PLUGIN=OFF', '-DMNM_BUILD_TESTS=OFF',
                    f'-DCMAKE_PROJECT_Monomodule_INCLUDE={(HERE/"reference.cmake").as_posix()}', *extra], env=env, check=True)
    subprocess.run([a.cmake, '--build', str(build), '--target', 'synfact-mnm-reference',
                    '-j', str(a.jobs)], env=env, check=True)
    provenance['driver_sha256'] = {name: hashlib.sha256((HERE/name).read_bytes()).hexdigest()
                                  for name in ('reference.cpp', 'reference.cmake', 'compat.h', 'mingw.patch')}
    (build/'reference-build.json').write_text(json.dumps(provenance, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
