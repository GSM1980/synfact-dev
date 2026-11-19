"""Preserve already-correct analog master Sound routing in both POLY banks.

Reuse the native-constructor regression without requiring an intentionally
broken historical bank. Its original wrong-Sound negative control is unchanged.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT / 'tests')]
from elekloader import formats
from test_st_poly_keyboard_emu import scenario


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for n in ('before', 'after', 'stock', 'core', 'out'):
        p.add_argument('--' + n, required=True, type=Path)
    a = p.parse_args()
    fw, _, _ = formats.load(a.stock)
    assert fw.sha256 == '8e2488f462c4a5656396a895f113bcd415e9900fa8709340dccf45d4cb9ed19e'
    rows = []
    for label, native in (('stock', fw.section(3)), ('core', a.core.read_bytes())):
        layout = json.loads((a.core.parent / 'verification.json').read_text())['link'] if label == 'core' else None
        for master, voices in ((8, 2), (8, 3), (9, 2)):
            for origin in (0x4002ed0a, 0x40105408):
                before = scenario(a.before, native, master, voices, origin, layout)
                after = scenario(a.after, native, master, voices, origin, layout)
                assert before == after and all(r['correct'] for r in before + after)
                rows.append(dict(main=label, master=master + 1, voices=voices,
                                 origin=hex(origin), notes=len(after)))
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    a.out.write_text(json.dumps(dict(ok=True, cases=rows, before_sha256=sha(a.before),
                                    after_sha256=sha(a.after), core_sha256=sha(a.core),
                                    fixture_sha256=sha(Path(__file__)), device_access=False,
                                    physical_audio_pending=True), indent=2) + '\n')
    print('PASS keyboard/MIDI native constructor, routing and Sound selection:', len(rows))


if __name__ == '__main__':
    main()
