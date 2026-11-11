#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Extract the four pinned MONO2K generators and regenerate their 48 kHz tables.

No firmware input. The original source snapshots and hashes live in upstream/.
Keep the 16-sample control interval: two engine blocks per Syntakt render.
"""
import hashlib
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
TABLES = ('T_SIN', 'T_INC_TOP', 'T_FMR24', 'T_FMB', 'T_FMEC', 'T_FMLP',
          'T_FMFB', 'T_FMDV', 'T_FMDEC', 'T_FMFEN')


def generate(rate=48000):
    upstream = HERE / 'upstream'
    provenance = json.loads((upstream / 'provenance.json').read_text())
    for name, digest in provenance['sha256'].items():
        assert hashlib.sha256((upstream / name).read_bytes()).hexdigest() == digest, name
    source = (upstream / 'm2k_dsp.c').read_text()
    machines = source[source.index('    case M_SID: {'):source.index('    case M_SAW: {')]
    # An eight-track neighbour supplied by the caller replaces the six-track
    # global lookup. Specialized products below preserve the exact low words.
    machines = machines.replace('inc_of(trk[ti ? ti - 1 : M2K_TRACKS6 - 1].pitch)',
                                'inc_of(previous_pitch)')
    machines = machines.replace("/* (the previous track round the first six, as the Monomachine's: T1's is T6 in any build; T7's T6) */",
                                '/* Previous digital track: T1 wraps to T8 on Syntakt. */')
    for state, depth in [('t->ms[3]', 'k1'), ('t->ms[5]', 'k2'), ('a[1]', 'kk[j]')]:
        machines = machines.replace(f'mul_sh({state}, (int32_t){depth}, 8)',
                                    f'phase_depth({state}, {depth})')
    machines = machines.replace('mul_sh(sinp(t->ph[1 + j]) * 256 - a[0], lp, 15)',
                                'gain_q15(sinp(t->ph[1 + j]) * 256 - a[0], lp)')
    machines = machines.replace('mul_sh(a[0] - a[1], lp, 15)',
                                'gain_q15(a[0] - a[1], lp)')
    code = (upstream / 'gen_tables.py').read_text().split('# the FX machines (')[0]
    code = code.replace('SR = 44100', f'SR = {rate}')
    scope = {'__file__': str(upstream / 'gen_tables.py')}
    exec(compile(code, 'MONO2K table generator', 'exec'), scope)
    tables = ''.join(scope['out'])
    selected = []
    for name in TABLES:
        match = re.search(r'static const [^\n]+\b' + name + r'\[.*?\n};\n', tables, re.S)
        assert match, name
        # Anchor on a single declaration, not any earlier table.
        selected.append(match.group(0))
    banner = ('/* SPDX-License-Identifier: GPL-3.0-only\n'
              ' * Generated from pinned MONO2K by Inigo Hernandez; see upstream/provenance.json.\n'
              ' * Run python mods/digimono-st/mono2k/generate.py; do not edit. */\n')
    return banner + machines, banner + ''.join(selected)


if __name__ == '__main__':
    machines, tables = generate()
    (HERE / 'machines.inc').write_text(machines, newline='\n')
    (HERE / 'tables.h').write_text(tables, newline='\n')
