# SPDX-License-Identifier: AGPL-3.0-or-later
"""Write the DigiPRO tables header from the owner's Monomachine OS 1.32B.

python mods/digimono-st/dpro/import_tables.py <Elektron_SFX6-60_OS1.32B.syx>

dpro/tables.h holds Elektron's DigiPRO waves and tables, so it is git-ignored and
never committed: every build makes it again from the OS file. tables.json (committed)
records the OS and header hashes, so a build can check it has the expected tables.
The Digibank stand-in is the one De-Pattern's DSP builds from the ROM waves
(dpro.build, run to completion in the pinned DSL); it takes about a minute and is
cached in dpro/out/.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import reference as R  # noqa: E402

PAD_WORDS = 32  # zero words after the 33 waves: a shrinking WP window may read past them


def generate(os_file):
    raw = Path(os_file).read_bytes()
    os_sha = hashlib.sha256(raw).hexdigest()
    m = R.load(os_file)
    rom = m.rom
    if os_sha != rom.SHA256:
        raise SystemExit(f'{os_file} is not the Monomachine OS 1.32B (sha256 {rom.SHA256[:12]}...)')
    cache = HERE / 'out' / f'digibank-{os_sha[:12]}-{hashlib.sha256((HERE / "upstream/dpro.py").read_bytes()).hexdigest()[:12]}.bin'
    tables = {
        'exp': ('uint32_t', rom.words(*rom.EXP)),
        'waves': ('uint32_t', rom.words(*rom.WAVES) + [0] * PAD_WORDS),
        'interp': ('int32_t', list(rom.interp_words())),
        'pch': ('uint32_t', rom.words(*rom.PCH)),
        'wid': ('uint32_t', rom.words(*rom.WID[0]) + rom.words(*rom.WID[1])),
        'br': ('uint32_t', list(rom.br_table())),
        'digibank': ('uint32_t', list(R.digibank(m, cache))),
        # De-Pattern's DDRW TIME law (its host's glide word), not Elektron's data:
        # the float32 rate a block, truncated to Q23.
        'rate': ('uint32_t', [int(R.ddrw_rate(m, t) * (1 << 23)) for t in range(128)]),
    }
    assert len(tables['exp'][1]) == 2048 and len(tables['interp'][1]) == 320
    assert len(tables['digibank'][1]) == 32 * 2048
    assert all(0 <= v < 1 << 24 for k, (_, vs) in tables.items() if k != 'interp' for v in vs)
    assert all(-(1 << 24) <= v < 1 << 24 for v in tables['interp'][1])
    text = ('/* DigiPRO tables from the owner\'s Monomachine OS 1.32B, written by\n'
            ' * dpro/import_tables.py. Elektron\'s data: never commit this file. */\n')
    for name, (kind, values) in tables.items():
        text += f'static const {kind} dpt_{name}[{len(values)}]={{\n'
        fmt = '{}' if kind == 'int32_t' else '0x{:06x}'
        text += ''.join('    ' + ','.join(fmt.format(v) for v in values[i:i + 12]) + ',\n'
                        for i in range(0, len(values), 12)) + '};\n'
    info = {'os_sha256': os_sha, 'tables_sha256': hashlib.sha256(text.encode()).hexdigest(),
            'lengths': {k: len(v) for k, (_, v) in tables.items()}}
    return text, info


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('os', type=Path)
    p.add_argument('--check', action='store_true', help='only compare with tables.json')
    a = p.parse_args()
    text, info = generate(a.os)
    recorded = HERE / 'tables.json'
    if a.check:
        want = json.loads(recorded.read_text())
        assert want == info, (want, info)
        print('PASS: DigiPRO tables reproduce', info['tables_sha256'][:12])
        return
    (HERE / 'tables.h').write_text(text, newline='\n')
    recorded.write_text(json.dumps(info, indent=2) + '\n')
    print('Wrote', HERE / 'tables.h', '(git-ignored), sha256', info['tables_sha256'][:12])


if __name__ == '__main__':
    main()
