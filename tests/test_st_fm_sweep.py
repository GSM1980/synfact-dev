"""Offline HARM/DTUN/FDBK differential: transport, x1, x2 bake and native morph.

Inputs are built independently from pinned Digitone MAIN descriptors. Four
engines run identical notes/controls; the native-morph x2 diagnostic must be
sample exact. No firmware or bank is modified. Static tests compare raw DSP;
listening copies additionally execute each device's native AMP with matched
ADSR controls. Filters/FX, full scheduling and physical output are excluded.
"""
import argparse
import itertools
import json
import math
from pathlib import Path
import struct
import wave

from test_st_fm_audio import Port, Digitone, Syx, Elf, RETURN, sha, require_emac, ST_SHA, DN_SHA
from test_st_fm_onset import NativeAmp, DN_MAIN_SHA

LABELS = ('syntakt-x1', 'syntakt-x2-bake-diagnostic', 'syntakt-x2-morph-diagnostic', 'digitone')
POINTS = dict(HARM=(37, 38, 50, 62, 63, 64, 70.5, 88, 89),
              DTUN=(0, 1, 16, 32, 64, 96, 127), FDBK=(0, 1, 16, 32, 64, 96, 127))
SYN1 = ('ALGO', 'RTOC', 'RTOA', 'RTOB', 'HARM', 'DTUN', 'FDBK', 'MIX')
EXT = ('ATKA', 'DECA', 'ENDA', 'LEVA', 'ATKB', 'DECB', 'ENDB', 'LEVB',
       'ADEL', 'ATRG', 'ARST', 'PHRT', 'BDEL', 'BTRG', 'BRST', 'COFF',
       None, 'AOFF', 'B1OF', 'B2OF')
DSP = dict(zip(SYN1, range(0x34, 0x44, 2)))
DSP.update(zip(('ATKA', 'DECA', 'ENDA', 'LEVA', 'ATKB', 'DECB', 'ENDB', 'LEVB',
                'ADEL', 'ATRG', 'ARST', 'BDEL', 'BTRG', 'BRST'), range(0x48, 0x64, 2)))
DSP.update(PHRT=0x44, COFF=0x76, AOFF=0x78, B1OF=0x7a, B2OF=0x7c)


class Fixture:
    def __init__(self, bank, stock, dn_audio, dn_main):
        require_emac()
        self.body = bank.read_bytes()
        assert sha(self.body) == json.loads(bank.with_suffix('.smz4.json').read_text())['sha256']
        self.stock = Syx.load(stock).section(7)
        self.original, self.main = dn_audio.read_bytes(), dn_main.read_bytes()
        assert (sha(self.stock), sha(self.original), sha(self.main)) == (ST_SHA, DN_SHA, DN_MAIN_SHA)
        elf = Elf.load(bank.with_suffix('.work')/'audio.elf')
        section = elf.section('.image')
        self.section = section.data
        self.address = struct.unpack_from('>I', elf.raw, struct.unpack_from('>I', elf.raw, 32)[0]+section.idx*40+12)[0]
        self.symbols = {s.name: s.value for s in elf.symbols if s.name}
        at = 48+struct.unpack_from('>I', self.body, 72)[0]+self.address-0x4204f750
        assert self.body[at:at+len(self.section)] == self.section
        self.defaults, self.ranges = {}, {}
        for i in range(110, 137):
            fields = struct.unpack_from('>13I', self.main, 0x4018d3a0-0x40000400+60*i)
            name_at = fields[12]-0x40000400
            name = self.main[name_at:name_at+12].split(b'\0')[0].decode('ascii')
            assert fields[0] == 0 and name in DSP
            self.defaults[name] = fields[4]
            self.ranges[name] = fields[2:4]
        assert len(self.defaults) == 27
        assert [self.ranges[n] for n in ('HARM', 'DTUN', 'FDBK')] == [
            (0x2500, 0x5900), (0, 0x7f00), (0, 0x7f00)]
        # Only unrelated template fields are shared. All active SYN controls
        # below come from the original DN descriptor input, never port output.
        start = self.symbols['fm_template']-self.address
        self.template = self.section[start:start+158]

    def settings(self, algorithm, preset, control, value):
        cfg = dict(self.defaults)
        cfg['ALGO'] = (algorithm-1)*256
        if preset == 'modulated':
            cfg.update(LEVA=64*256, LEVB=64*256, HARM=round(70.5*256), FDBK=32*256)
        cfg[control] = round(value*256)
        return cfg

    def expected(self, cfg):
        p = bytearray(self.template)
        for name, at in DSP.items():
            struct.pack_into('>H', p, at, cfg[name])
        return bytes(p)

    def configure(self, port, cfg):
        port.raw = [cfg[n] for n in SYN1[:3]] + [(cfg['RTOB']//2)*88,
                    cfg['HARM'], cfg['DTUN'], 0, cfg['MIX'], cfg['FDBK']]
        port.uc.mem_write(RETURN+0x2000, struct.pack('>20H', *[cfg[n] if n else 0 for n in EXT]))

    def engines(self, cfg, note):
        ports = [Port(self.stock, self.section, self.address, self.symbols, m,
                      cfg['ALGO']//256, cfg['PHRT']) for m in (0, 1, 1)]
        for mode, port in zip((0, 1, 1), ports):
            port.write32(0x80008184, 14400)
            port.uc.mem_write(self.symbols['fm_state']+0x31ec+8, bytes([mode]))
            self.configure(port, cfg)
        # Exhaust the existing bake allocator in emulator data: original
        # morph fallback, not a patch to instructions or a proposed bank mode.
        ports[2].write32(self.symbols['fm_state']+0x3230, 16)
        native = Digitone(self.original, self.expected(cfg))
        native.write32(0x80000378, 14400)
        engines = ports+[native]
        initial = [e.block(note=note) for e in engines]
        assert initial[2] == initial[3]
        self.assert_parameters(engines, cfg)
        return engines

    def assert_parameters(self, engines, cfg):
        expected = self.expected(cfg)
        for port in engines[:3]:
            params = port.read32(self.symbols['fm_params'])
            assert bytes(port.uc.mem_read(params, 158)) == expected, cfg
        assert bytes(engines[3].uc.mem_read(0x800003a0, 158)) == expected

    def assert_code(self, engines):
        size = self.symbols['fm_state']-self.address
        for e in engines[:3]:
            assert bytes(e.uc.mem_read(self.address, size)) == self.section[:size]
        assert bytes(engines[3].uc.mem_read(0x40000400, 0x6390)) == self.original[:0x6390]


def difference(a, b):
    error = sum((x-y)**2 for x, y in zip(a, b))
    signal = sum(y*y for y in b)
    noise = math.sqrt(error/len(a))/2**31
    return dict(sample_exact=a == b, max_error_q31=max(abs(x-y) for x, y in zip(a, b)),
                error_rms_dbfs=20*math.log10(noise) if noise else None,
                error_to_reference_db=10*math.log10(error/signal) if error and signal else None)


def static_case(f, algorithm, note, preset, control, value, blocks):
    cfg = f.settings(algorithm, preset, control, value)
    engines = f.engines(cfg, note)
    samples = [[] for _ in engines]
    first_state_difference = [None]*3
    for block in range(blocks):
        raw = [e.block(trigger=block == 0, note=note) for e in engines]
        assert raw[2] == raw[3], ('morph PCM', algorithm, note, preset, control, value, block)
        for i, e in enumerate(engines[:3]):
            if e.state() != engines[3].state() and first_state_difference[i] is None:
                first_state_difference[i] = block
        assert first_state_difference[2] is None, ('morph state', cfg)
        for out, pcm in zip(samples, raw):
            out.extend(pcm)
    f.assert_parameters(engines, cfg)
    f.assert_code(engines)
    return dict(algorithm=algorithm, note=note, preset=preset, control=control, value=value,
                blocks=blocks, received_words_match=True,
                paths={name: dict(**difference(samples[i], samples[3]),
                                  first_scalar_state_difference_block=first_state_difference[i])
                       for i, name in enumerate(LABELS[:3])})


def write_wav(path, pcm):
    # Same common gain and final-file-only fade as the previous factory A/B.
    converted = [round(v/2**31*.2*8388607*(min(1, (len(pcm)-1-i)/479)
                  if i >= len(pcm)-480 else 1)) for i, v in enumerate(pcm)]
    assert all(abs(v) <= 8388607 for v in converted)
    payload = bytes(3840*3)+b''.join((v&0xffffff).to_bytes(3, 'little') for v in converted)
    with wave.open(str(path), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(3); w.setframerate(48000); w.writeframes(payload)
    return payload


def listening_sweep(f, control, folder):
    folder.mkdir(parents=True, exist_ok=True)
    values = POINTS[control]
    cfg = f.settings(1, 'modulated', control, values[0])
    engines = f.engines(cfg, 60)
    amps = [NativeAmp(f.stock, False, 8) for _ in range(3)]+[NativeAmp(f.main, True, 8)]
    for amp in amps:
        controls = ([8*256, 32*256, 96*256, 24*256]+[0]*6+[256] if amp.digitone else
                    [0, 8*256, 0, 32*256, 96*256, 24*256, 0, 0, 0, 0, 0, 256, 256])
        amp.uc.mem_write(RETURN+0x300, struct.pack('>'+str(len(controls))+'H', *controls))
    for _ in range(120):
        for e, amp in zip(engines, amps):
            assert amp.block(e.block(note=60)) == (0,)*32
    raw_samples, audio = [[] for _ in engines], [[] for _ in engines]
    # Hold the same note while changing the control every 250ms, without retrigs.
    # 375 blocks * 32 samples = 12000 samples per point.
    for point, value in enumerate(values):
        cfg = f.settings(1, 'modulated', control, value)
        for e in engines[:3]:
            f.configure(e, cfg)
        engines[3].uc.mem_write(0x800003a0, f.expected(cfg))
        for block in range(375):
            trigger = point == block == 0
            raw = [e.block(note=60, trigger=trigger) for e in engines]
            assert raw[2] == raw[3], ('live morph PCM', control, point, block)
            for i, amp in enumerate(amps):
                raw_samples[i].extend(raw[i])
                audio[i].extend(amp.block(raw[i], trigger=trigger))
        f.assert_parameters(engines, cfg)
    f.assert_code(engines)
    files, payloads = [], []
    for name, pcm, raw in zip(LABELS, audio, raw_samples):
        path = folder/(control+'-'+name+'.wav')
        payloads.append(write_wav(path, pcm))
        q31 = folder/(control+'-'+name+'.q31')
        q31.write_bytes(struct.pack('>'+str(len(raw))+'i', *raw))
        files.append(dict(path=str(path.resolve()), sha256=sha(path.read_bytes()),
                          raw_before_amp=str(q31.resolve()), raw_sha256=sha(q31.read_bytes())))
    sequence = payloads[0]+bytes(24000*3)+payloads[3]+bytes(24000*3)
    combined = folder/('AB-'+control+'-Syntakt-Digitone.wav')
    with wave.open(str(combined), 'wb') as w:
        w.setnchannels(1); w.setsampwidth(3); w.setframerate(48000); w.writeframes(sequence)
    for amp in amps:
        amp.assert_code()
    return dict(control=control, values=values, point_ms=250, algorithm=1, note=60,
                preset='modulated', matched_amp_adsr=[8, 32, 96, 24],
                files=files, combined=str(combined.resolve()), order=['Syntakt x1', 'Digitone'],
                fixed_gain=.2, normalization=False, onset_fade=False, final_file_fade_ms=10,
                raw_errors={name: difference(raw_samples[i], raw_samples[3]) for i, name in enumerate(LABELS[:3])})


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('bank', 'stock', 'digitone-audio', 'digitone-main', 'out'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--algorithms', type=int, nargs='+', default=list(range(1, 9)))
    p.add_argument('--notes', type=int, nargs='+', default=[48, 60, 84])
    p.add_argument('--blocks', type=int, default=64)
    p.add_argument('--listen', action='store_true')
    a = p.parse_args()
    assert all(1 <= n <= 8 for n in a.algorithms) and all(0 <= n <= 127 for n in a.notes)
    assert a.blocks >= 32
    f = Fixture(a.bank.resolve(), a.stock, a.digitone_audio, a.digitone_main)
    a.out.mkdir(parents=True, exist_ok=True)
    result = dict(bank_sha256=sha(f.body), digitone_main_sha256=DN_MAIN_SHA,
                  digitone_audio_sha256=DN_SHA, syntakt_audio_sha256=ST_SHA,
                  raw_static_before_amp=True, physical_output=False, filters_fx=False,
                  quality_changed_only_in_emulator=True, firmware_or_bank_modified=False,
                  scalar_states_exclude_wave_pointers=True, cases=[])
    for preset, control, algorithm, note in itertools.product(
            ('init', 'modulated'), POINTS, a.algorithms, a.notes):
        for value in POINTS[control]:
            case = static_case(f, algorithm, note, preset, control, value, a.blocks)
            result['cases'].append(case)
        print('PASS', preset, control, 'ALGO', algorithm, 'note', note, 'cases', len(result['cases']), flush=True)
        (a.out/'results.json').write_text(json.dumps(result, indent=2)+'\n')
    if a.listen:
        result['listening_sweeps'] = []
        for control in POINTS:
            result['listening_sweeps'].append(listening_sweep(f, control, a.out))
            print('PASS live sweep + matched native AMP', control, flush=True)
            (a.out/'results.json').write_text(json.dumps(result, indent=2)+'\n')


if __name__ == '__main__':
    main()
