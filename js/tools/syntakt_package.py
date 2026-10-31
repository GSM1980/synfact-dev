"""Prepare local Syntakt core artifacts after the documented Syntakt build.

The output contains derived files: never commit it or publish it without a
provenance and redistribution review. Supply a new, empty output directory.
"""
import argparse
import hashlib
import json
from pathlib import Path


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--reference', type=Path, required=True, help='Syntakt build output with verification.json and audio/')
    p.add_argument('--core', type=Path, required=True)
    p.add_argument('--machines', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--meter', action='store_true', help='reference was built with --system-info')
    a = p.parse_args()
    report = json.loads((a.reference / 'verification.json').read_text(encoding='utf-8'))
    core = json.loads(a.core.read_text(encoding='utf-8-sig'))
    target = core.get('target', {})
    if core.get('id') != 'core' or target.get('device') != 'syntakt' or str(target.get('os')) != '1.41':
        raise ValueError('core must target Syntakt 1.41')
    if not all(report['protected_sections'][str(i)]['stock'] for i in (1, 2, 4, 5, 6, 8)):
        raise ValueError('reference protected-section verification failed')
    symbols = report['audio']['symbols']
    if ('audio_enter' in symbols) != a.meter:
        raise ValueError('meter flag does not match AUDIO symbols')
    # Refuse an existing output: never replace prior work.
    a.out.mkdir(parents=True, exist_ok=False)
    def copy(source, name):
        data = source.read_bytes()
        (a.out / name).write_bytes(data)
        return {'file': name, 'sha256': hashlib.sha256(data).hexdigest()}
    mods = [copy(a.core, 'core.elemod'), copy(a.machines, 'machines.elemod')]
    audio = copy(a.reference / 'audio' / 'audio-code.bin', 'audio-code.bin')
    upper_path = a.reference / 'audio' / 'audio-upper.bin'
    if upper_path.exists():
        upper = copy(upper_path, 'audio-upper.bin')
    else:
        data = b''
        (a.out / 'audio-upper.bin').write_bytes(data)
        upper = {'file': 'audio-upper.bin', 'sha256': hashlib.sha256(data).hexdigest()}
    manifest = {'schema': 1, 'device': 'syntakt', 'os': '1.41', 'coreVersion': core['version'],
                'meter': a.meter, 'mods': mods, 'audio': audio, 'upper': upper, 'symbols': symbols,
                'mainSha256': report['output']['main']['sha256'],
                'audioSha256': report['output']['sections']['7']['stored_sha256'],
                'outputSha256': report['sha256']}
    raw = (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode('utf-8')
    (a.out / 'syntakt.json').write_bytes(raw)
    print('Manifest SHA-256:', hashlib.sha256(raw).hexdigest())


if __name__ == '__main__':
    main()
