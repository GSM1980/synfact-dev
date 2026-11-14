"""Compare a MAIN optimization's AUDIO against the owner's working bank/core."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


def audio(path):
    raw = path.read_bytes()
    if len(raw) < 80 or raw[48:52] != b'SMZ4':
        raise ValueError('not an SMZ4 bank')
    offset, size = struct.unpack_from('>II', raw, 72)
    offset += 48
    if offset + size > len(raw) or raw[offset:offset + 4] != b'SMA3':
        raise ValueError('invalid AUDIO section')
    return raw[offset:offset + size]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bank', type=Path, required=True)
    p.add_argument('--reference-bank', type=Path, required=True)
    p.add_argument('--core', type=Path)
    p.add_argument('--reference-core', type=Path)
    p.add_argument('--out', type=Path)
    a = p.parse_args()
    if bool(a.core) != bool(a.reference_core):
        p.error('supply both core files or neither')
    current, reference = audio(a.bank), audio(a.reference_bank)
    report = {'audio_identical': current == reference,
              'audio_bytes': len(current),
              'audio_sha256': hashlib.sha256(current).hexdigest(),
              'reference_audio_sha256': hashlib.sha256(reference).hexdigest()}
    if a.core:
        report['core_identical'] = a.core.read_bytes() == a.reference_core.read_bytes()
    if a.out:
        a.out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if not report['audio_identical'] or not report.get('core_identical', True):
        raise SystemExit('FAIL: AUDIO or core differs from the working reference')
    print('PASS: AUDIO identical to working bank: ' + report['audio_sha256'])
    if a.core:
        print('PASS: complete core SYX identical to working reference')


if __name__ == '__main__':
    main()
