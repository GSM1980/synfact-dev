"""Offline first-note comparison: compiled FM + original native AMP routines.

The isolated Syntakt AUDIO AMP (40001476) and Digitone MAIN AMP (40096926)
execute unchanged. Explicit fixtures supply controls, gates and a released,
zero-gain envelope. These are bounded routine comparisons, not device captures,
full IRQ/MAIN timing, filters, FX, MIDI latency or polyphonic voice allocation.
WAV gain is fixed at 0.2, identical for all paths; no fades or normalization.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import wave

from test_st_fm_audio import CPU, Port, Digitone, Syx, Elf, sha, require_emac, RETURN, DN_SHA, ST_SHA

DN_MAIN_SHA = 'fce648a97c6c5d93b961732e8f8db6b02e0820c6d344c7e2131fa05a4b3168e4'
RATE = 48000


class NativeAmp(CPU):
    def __init__(self, source, digitone, attack):
        super().__init__([(0x40000000, 0x400000), (0x80000000, 0x10000),
                          (0x431f0000, 0x10000)])
        self.digitone = digitone
        self.uc.mem_write(0x40000400, source)
        if not digitone:
            self.call(0x400004bc)
        # Known quiescent release state, supplied explicitly. It does not model
        # boot initialization, machine selection, mute or a previous note's tail.
        self.write32(RETURN+0x200, 3)
        self.write32(RETURN+0x208, 0)
        # Shared ADSR: ATK variable, DEC 64, SUS 100, REL 32, reset ON.
        # Syntakt additionally has HOLD=0 and its ADSR selector set to 1.
        controls = ([attack*256, 0x4000, 0x6400, 0x2000]+[0]*6+[0x100]
                    if digitone else
                    [0, attack*256, 0, 0x4000, 0x6400, 0x2000,
                     0, 0, 0, 0, 0, 0x100, 0x100])
        self.uc.mem_write(RETURN+0x300, struct.pack('>'+str(len(controls))+'H', *controls))
        self.source = source

    def block(self, pcm, trigger=False, release=False):
        self.uc.mem_write(RETURN+0x1000, struct.pack('>32i', *pcm))
        args = [RETURN+0x1000, RETURN+0x200, RETURN+0x300, int(trigger), int(release), 0]
        if not self.digitone:
            args.append(0)  # explicit mute/fast-kill flag is clear
        self.call(0x40096926 if self.digitone else 0x40001476, *args)
        return struct.unpack('>32i', self.uc.mem_read(RETURN+0x1000, 128))

    def assert_code(self):
        assert bytes(self.uc.mem_read(0x40000400, len(self.source))) == self.source


def amp_differential(stock, dn_main):
    # Independent constants oracle: attack and decay/release tables match.
    for st_at, dn_at in ((0x40014070, 0x401a9438), (0x40013e70, 0x401a9238)):
        assert stock[st_at-0x40000400:st_at-0x40000400+512] == dn_main[dn_at-0x40000400:dn_at-0x40000400+512]
    results = []
    for attack in (0, 1, 16, 32, 64, 96):
        outputs = []
        for original, is_dn in ((stock, False), (dn_main, True)):
            amp = NativeAmp(original, is_dn, attack)
            for _ in range(64):
                assert amp.block([0]*32) == (0,)*32
            pcm = []
            for block in range(96):
                pcm.extend(amp.block([0x40000000]*32, trigger=block == 0, release=block >= 80))
            amp.assert_code()
            outputs.append(pcm)
        starts = [next(i for i, value in enumerate(pcm) if value) for pcm in outputs]
        count = min(80*32-start for start in starts)
        a, b = [pcm[start:start+count] for pcm, start in zip(outputs, starts)]
        assert a == b, attack
        assert starts == [32, 64]
        results.append(dict(attack=attack, syntakt_first_nonzero=starts[0],
                            digitone_first_nonzero=starts[1], matched_attack_samples=count,
                            aligned_attack_sample_exact=True))
    return results


def wav(path, samples):
    pcm = [max(-8388608, min(8388607, round(v/2**31*0.2*8388607))) for v in samples]
    with wave.open(str(path), 'wb') as stream:
        stream.setnchannels(1)
        stream.setsampwidth(3)
        stream.setframerate(RATE)
        stream.writeframes(b''.join((value & 0xffffff).to_bytes(3, 'little') for value in pcm))
    return dict(file=str(path), sha256=sha(path.read_bytes()), fixed_gain=0.2, bits=24)


def metrics(pcm):
    first = next((i for i, value in enumerate(pcm) if value), None)
    at = 0 if first is None else first
    section = pcm[at:at+96]
    peak = max((abs(v) for v in section), default=0)/2**31
    delta = max((abs(b-a) for a, b in zip(section, section[1:])), default=0)/2**31
    return dict(first_nonzero_sample=first, peak_first_2ms=peak,
                max_adjacent_step_first_2ms=delta,
                onset_step_from_silence=abs(pcm[at]-(pcm[at-1] if at else 0))/2**31,
                peak_first_2ms_dbfs=20*math.log10(peak) if peak else None,
                first_128_samples=pcm[:128])


def verify(bank, stock_path, dn_audio, dn_main_path, out):
    require_emac()
    stock, original, dn_main = Syx.load(stock_path).section(7), dn_audio.read_bytes(), dn_main_path.read_bytes()
    assert (sha(stock), sha(original), sha(dn_main)) == (ST_SHA, DN_SHA, DN_MAIN_SHA)
    body = bank.read_bytes()
    bank_report = json.loads(bank.with_suffix(bank.suffix+'.json').read_text())
    assert sha(body) == bank_report['sha256']
    elf = Elf.load(bank.with_suffix('.work')/'audio.elf')
    section = elf.section('.image')
    address = struct.unpack_from('>I', elf.raw, struct.unpack_from('>I', elf.raw, 32)[0]+section.idx*40+12)[0]
    symbols = {s.name: s.value for s in elf.symbols if s.name}
    offset = 48+struct.unpack_from('>I', body, 72)[0]+address-0x4204f750
    assert body[offset:offset+len(section.data)] == section.data
    out.mkdir(parents=True, exist_ok=True)
    envelope = amp_differential(stock, dn_main)
    print('PASS native AMP: six attacks sample-exact after aligning routine onset', flush=True)
    cases = []
    for preset in ('init', 'modulated'):
        for attack in (0, 32, 96):
            ports = [Port(stock, section.data, address, symbols, mode, 0, 0x100) for mode in (0, 1, 1)]
            # A fourth control uses the bank's existing full-slot fallback.
            # This distinguishes weighted-wave bake rounding from the native
            # morph kernels. Only emulator data is changed, never bank bytes.
            ports[2].write32(symbols['fm_state']+0x3230, 16)
            for mode, port in zip((0, 1, 1), ports):
                # Retain the requested mode during pre-roll, also before first trig.
                port.uc.mem_write(symbols['fm_state']+0x31ec+8, bytes([mode]))
                port.raw[4] = 0x3f00 if preset == 'init' else round(70.5*256)
                port.raw[8] = 0 if preset == 'init' else 0x2000
                ext = list(struct.unpack('>20H', port.uc.mem_read(RETURN+0x2000, 40)))
                ext[0] = ext[4] = 0 if preset == 'init' else 0x2000
                ext[3] = ext[7] = 0 if preset == 'init' else 0x4000
                port.uc.mem_write(RETURN+0x2000, struct.pack('>20H', *ext))
            initial = [port.block() for port in ports]
            params = bytes(ports[0].uc.mem_read(symbols['fm_state']+0xf28, 158))
            native = Digitone(original, params)
            initial.append(native.block())
            assert initial[2] == initial[3]
            engines = ports+[native]
            amps = [NativeAmp(stock, False, attack) for _ in ports]+[NativeAmp(dn_main, True, attack)]
            # 80 ms of actual continuous oscillator execution under zero AMP gain.
            for _ in range(120):
                for engine, amp in zip(engines, amps):
                    assert amp.block(engine.block()) == (0,)*32
            samples, raw_samples = [[] for _ in engines], [[] for _ in engines]
            first_bake_difference, max_bake_difference = None, 0
            for block in range(12000):
                raw = [engine.block(trigger=block == 0, release=block >= 300) for engine in engines]
                assert raw[2] == raw[3], (preset, attack, block, 'native morph fallback')
                if block < 9:
                    assert raw[1] == raw[3], (preset, attack, block, 'first 6ms')
                difference = max(abs(a-b) for a, b in zip(raw[1], raw[3]))
                if difference and first_bake_difference is None:
                    first_bake_difference = block
                if block < 750:
                    max_bake_difference = max(max_bake_difference, difference)
                for i, amp in enumerate(amps):
                    raw_samples[i].extend(raw[i])
                    samples[i].extend(amp.block(raw[i], trigger=block == 0, release=block >= 300))
                # Native fixed-point release settles at a small residual (about
                # 13,278 Q31 units here) rather than exact zero. Keep the natural
                # tail until the fixed-gain export is below 16 PCM24 units
                # (-114.4 dBFS). Do not cut an audible tail or add a host fade.
                gains = [amp.read32(RETURN+0x208) for amp in amps]
                gains = [value if value < 0x80000000 else value-0x100000000 for value in gains]
                if block >= 750 and max(abs(value) for value in gains) <= 16384:
                    break
            else:
                raise AssertionError('Native release did not reach the declared export residual')
            case = dict(preset=preset, amp_attack=attack, operator_attack=0 if preset == 'init' else 32,
                        frames=len(samples[0]), native_x2_fallback_raw_sample_exact=True,
                        native_x2_baked_first_6ms_sample_exact=True,
                        x2_bake_first_difference_block=first_bake_difference,
                        x2_bake_max_difference_q31=max_bake_difference, paths={})
            for i, label in enumerate(('syntakt-x1', 'syntakt-x2-diagnostic', 'syntakt-x2-fallback-control', 'digitone')):
                path = out/('%s-atk%d-%s.wav' % (preset, attack, label))
                assert max(abs(round(value/2**31*0.2*8388607)) for value in samples[i][-32:]) <= 16
                # Preserve 80 ms leading silence, measured attack and natural release.
                case['paths'][label] = dict(**wav(path, [0]*(120*32)+samples[i]),
                    **metrics(samples[i]), raw_metrics=metrics(raw_samples[i]))
                amps[i].assert_code()
            cases.append(case)
            print('PASS', preset, 'AMP ATK', attack, flush=True)
    result = dict(bank=str(bank), bank_sha256=sha(body), digitone_main_sha256=DN_MAIN_SHA,
        digitone_audio_sha256=DN_SHA, stock_audio_sha256=ST_SHA, native_amp_checks=envelope,
        comparison_cases=cases, sample_rate=RATE, no_normalization_or_fades=True,
        explicit_released_zero_gain_fixture=True, preroll_blocks=120, note_off_block=300,
        native_release_to_24bit_residual=True, terminal_pcm24_peak_limit=16,
        native_amp_routines_executed=True, filters_fx_bypassed=True, full_boot=False,
        hardware_capture=False, midi_to_audio_latency_measured=False, firmware_written=False)
    (out/'results.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('bank', 'stock', 'digitone-audio', 'digitone-main', 'out'):
        parser.add_argument('--'+name, required=True, type=Path)
    args = parser.parse_args()
    verify(args.bank.resolve(), args.stock, args.digitone_audio, args.digitone_main, args.out.resolve())
