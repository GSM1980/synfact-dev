"""Validate the OS 1.42 port against the owner's official file and real build.

No device access. Checks every static/runtime stock preimage, firmware framing,
protected sections, the exact bank contract, and optional CPU2 parity with 1.41.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from elekloader import formats, syx
from elekloader.devices.syntakt import DEVICE
from elekloader.sdk.build import BuildError, for_release
from tools.generate_boot_preimages import modulation_skip


def runtime_sites(source):
    table = source.split('sites[]={', 1)[1].split('};', 1)[0]
    for address, length, encoded in re.findall(
            r'\{(0x[0-9a-f]+)u,(\d+),\{([^}]+)\}', table):
        data = bytes(int(x, 16) for x in re.findall(r'0x[0-9a-f]+', encoded))
        assert len(data) == int(length)
        yield int(address, 16), data


def audio_section(path):
    data = path.read_bytes()
    offset, length = struct.unpack_from('>II', data, 72)
    return data[48 + offset:48 + offset + length]


def verify(stock_path, folder, previous=None):
    stock, device, release = formats.load(stock_path)
    assert device.key == 'syntakt' and release.version == '1.42'
    main = stock.section(3)
    assert syx.sha(main) == release.main_sha256 and len(main) == 3442576
    assert DEVICE.flash_limit == DEVICE.flash_at + stock.total
    assert syx.write(stock, stock.stored[3], device) == stock.raw
    syx.verify(stock.raw, stock, main, device)
    count = 0

    def check(address, expected):
        nonlocal count
        offset = address - DEVICE.main_load
        assert offset >= 0 and main[offset:offset + len(expected)] == expected, hex(address)
        count += 1

    manifest = json.loads((ROOT / 'mods/core-st/mod.json').read_text())
    assert manifest['os'] == '1.42' and len(manifest['sites']) == 119
    for path in sorted((ROOT / 'mods').glob('*-st/mod.json')):
        definition = json.loads(path.read_text())
        assert definition['os'] == '1.42'
        assert for_release(definition, device, release) == (definition, False)
        try:
            for_release(definition, device, device.releases['1.41'])
        except BuildError:
            pass
        else:
            raise AssertionError(str(path) + ': must not build OS 1.42 sources for 1.41')
    for site in manifest['sites']:
        check(int(site['addr'], 16), bytes.fromhex(site['stock']))
    for module in ('lfo3-st/main.c', 'chorus-st/install.c'):
        rows = list(runtime_sites((ROOT / 'mods' / module).read_text()))
        assert len(rows) == (94 if module.startswith('lfo3') else 5)
        for address, expected in rows:
            check(address, expected)
    for address, expected in modulation_skip():
        check(address, expected)
    # Playhead installs five sites conditionally and checks four surrounding
    # CRCs before the first write. Validate both against the actual new stock.
    source = (ROOT / 'mods/playhead-st/seq-direction.c').read_text()
    sites = source.split('sites[SITES]={', 1)[1].split('};', 1)[0]
    rows = re.findall(r'\{(0x[0-9a-f]+)u,\{((?:0x[0-9a-f]+u,?)+)\}\}', sites)
    assert len(rows) == 5
    for address, words in rows:
        check(int(address, 16), b''.join(int(x, 16).to_bytes(2, 'big')
              for x in re.findall(r'0x[0-9a-f]+', words)))
    import zlib
    contexts = source.split('contexts[4]={', 1)[1].split('};', 1)[0]
    rows = re.findall(r'\{(0x[0-9a-f]+)u,(0x[0-9a-f]+)u,(0x[0-9a-f]+)u\}', contexts)
    assert len(rows) == 4
    for lo, hi, crc in rows:
        a, b = int(lo, 16) - DEVICE.main_load, int(hi, 16) - DEVICE.main_load
        assert 0 <= a < b <= len(main) and zlib.crc32(main[a:b]) == int(crc, 16)
    # LFO3's sound-size operands are a distinct pair of checked runtime writes.
    source = (ROOT / 'mods/lfo3-st/main.c').read_text()
    operands = source.split('operands[]=', 1)[1].split('};', 1)[0]
    rows = re.findall(r'\{(0x[0-9a-f]+)u,(0x[0-9a-f]+),ST_SOUND_S1_BYTES\}', operands)
    assert len(rows) == 2
    for address, value in rows:
        check(int(address, 16) - 2, struct.pack('>HH', 0x4878, int(value, 16)))

    report = json.loads((folder / 'verification.json').read_text())
    built_main = (folder / 'main.bin').read_bytes()
    built_audio = (folder / 'audio.bin').read_bytes()
    output = next(folder.glob('Syntakt_OS1.42_*.syx')).read_bytes()
    facts = syx.verify(output, stock, built_main, device, secondary={7: built_audio})
    assert facts['main']['inplace_min_gap'] >= 4096
    built = syx.Syx(output)
    assert built.header == stock.header
    assert all(built.stored[sid] == stock.stored[sid] for sid in (1, 2, 4, 5, 6, 8))
    assert report['stock_sha256'] == stock.sha256 and report['resident_ram']['free'] >= 0
    bank = folder / 'synfact-machines.smz4'
    raw = bank.read_bytes()
    assert struct.unpack_from('>I', raw, 24)[0] & 16
    assert struct.unpack_from('>I', raw, 64)[0] == 0x53594605
    modules = json.loads(bank.with_suffix('.smz4.json').read_text())
    assert modules['machines'] == [66, 67, 68, 69, 65]
    assert set(modules['modules']) == {
        'sp_sampler_input_module', 'st_stereo_module', 'fm_tone_module',
        'recorder_module', 'lfo3_module', 'arp_module', 'poly_module',
        'system_info_module', 'sample_manager_module', 'chorus_module', 'playhead_module', 'modulation_skip_module'}
    if previous:
        assert built_audio == (previous / 'audio.bin').read_bytes()
        assert audio_section(bank) == audio_section(previous / bank.name)
    print('PASS: OS 1.42 round-trip, %d exact preimages, protected sections, '
          'in-place margin, complete bank/contract%s; no device access'
          % (count, ', byte-identical core and bank AUDIO' if previous else ''))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stock', required=True, type=Path)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--previous-build', type=Path)
    args = parser.parse_args()
    verify(args.stock, args.build, args.previous_build)
