#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Extract pinned MONO2K ENS/SID generators and regenerate 48 kHz tables.

No firmware input. The original source snapshots and hashes live in upstream/.
Keep the 16-sample control interval: two engine blocks per Syntakt render.
"""
import hashlib
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
TABLES = ('T_SIN', 'T_INC_TOP', 'T_ENS_PCH', 'T_ENS_W', 'T_ENS_G')


def generate(rate=48000):
    upstream = HERE / 'upstream'
    provenance = json.loads((upstream / 'provenance.json').read_text())
    for name, digest in provenance['sha256'].items():
        assert hashlib.sha256((upstream / name).read_bytes()).hexdigest() == digest, name
    source = (upstream / 'm2k_dsp.c').read_text()
    machines = source[source.index('    case M_SID: {'):source.index('    case M_FMS: {')]
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
    for name in ('ENS_OFF', 'ENS_CINC', 'ENS_CDEP'):
        selected.append(re.search(r'^#define '+name+r' [^\n]+\n', tables, re.M)[0])
    banner = ('/* SPDX-License-Identifier: GPL-3.0-only\n'
              ' * Generated from pinned MONO2K by Inigo Hernandez; see upstream/provenance.json.\n'
              ' * Run python mods/digimono-st/mono2k/generate.py; do not edit. */\n')
    return banner + machines, banner + ''.join(selected)


def generate_ens():
    source = (HERE/'upstream/m2k_dsp.c').read_text()
    case = source[source.index('    case M_ENS: {'):source.index('    case M_WAV: {')]
    case = case.replace('ens_mem[ti]', 't->line').replace('eside[ti]', 'side')
    # Same-note harmonic ratios cancel the original track response. The ENS
    # grid infers duty=.01+.98*PW/128 and saw weights 1-.3*x / 1-1.7*x,
    # x=WAVE/127; ens_model.py validates these laws on independent cuts.
    case = case.replace('12124', '9830').replace('53412', '55706')
    case = case.replace('uint32_t off = (uint32_t)P8(t, P_SYN + 4) << 17;',
                        'uint32_t width = (uint32_t)P8(t, P_SYN + 4) << 17;\n'
                        '        uint32_t off = 42949673u + width - (width + 25u) / 50u;')
    case = case.replace('their difference in at 0.63 (weights 1 - 0.37 x and 1 - 1.63 x, x = WAVE / 127): PW 64',
                        'their difference in at 0.70 (weights 1 - 0.30 x and 1 - 1.70 x, x = WAVE / 127): PW 64')
    case = case.replace('the second PW / 128 of a period on;', 'the second 0.01 + 0.98 * PW / 128 of a period on;')
    # Each product fits signed 32 bits; their sum may not. Split at Q15 to
    # preserve the exact signed wide sum without a ColdFire helper/overflow.
    case = case.replace('(int32_t)(((int64_t)(s * dry) + ((y[0] + y[1]) >> 1) * wet) >> 15)',
                        'add_sh15(s * dry, ((y[0] + y[1]) >> 1) * wet)')
    return ('/* SPDX-License-Identifier: GPL-3.0-only\n'
            ' * MONO2K SWAVE-ENS with measured PW/WAVE laws; generated, do not edit. */\n')+case


def generate_ens_ui():
    source = (HERE/'upstream/m2k_tables.h').read_text()
    index = re.search(r'static const uint8_t T_ENS_PIDX\[.*?\n};\n', source, re.S)[0]
    names = re.search(r'static const char \*const T_ENS_PNAME\[[^\n]+\n', source)[0]
    return '/* SPDX-License-Identifier: GPL-3.0-only; generated MONO2K interval names. */\n'+index+names


if __name__ == '__main__':
    machines, tables = generate()
    (HERE / 'machines.inc').write_text(machines, newline='\n')
    (HERE / 'tables.h').write_text(tables, newline='\n')
    (HERE / 'ens.inc').write_text(generate_ens(), newline='\n')
    (HERE / 'ens-ui.h').write_text(generate_ens_ui(), newline='\n')
