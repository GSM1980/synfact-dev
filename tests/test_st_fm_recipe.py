"""FM TONE's Digitone recipe, against an owner's Digitone OS file. No device access.

python tests/test_st_fm_recipe.py --digitone <Digitone OS 1.44 .syx>

Checks that the recipe rebuilds every output from that file, and that none of
our own bytes ("data" operations) holds a run of the recipe's minimum length
that the file's regions hold. Without --digitone it is skipped, not passed.
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader.sdk.syntakt import donor


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--digitone', type=Path)
    a = p.parse_args()
    if not a.digitone:
        print('SKIPPED: no --digitone file')
        return
    recipe = json.loads((ROOT / 'mods/fm-tone-st/digitone.json').read_text())
    raw = a.digitone.read_bytes()
    outputs = donor.rebuild(recipe, raw)
    sources = donor.regions(recipe, raw)
    ours = 0
    for o in recipe['outputs']:
        runs = donor.literal_runs(o['ops'], sources, recipe['minimum'])
        assert not runs, (o['name'], runs[:5])
        ours += sum(len(op[1]) // 2 for op in o['ops'] if op[0] == 'data')
    print('PASS: %d outputs rebuilt (%d bytes); %d bytes of ours, none a run the Digitone holds'
          % (len(outputs), sum(map(len, outputs.values())), ours))


if __name__ == '__main__':
    main()
