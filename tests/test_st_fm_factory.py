"""Compare factory FM/AMP defaults and isolate AMP ATK 0 versus 8 offline.

Defaults come from pinned native descriptor tables. The emulated Syntakt AMP
is its digital factory AD profile (HOLD 64/DEC 127); Digitone uses its ADSR
profile. A separate Syntakt path matches Digitone's ADSR controls. Oscillators
pre-roll under an explicitly released zero-gain AMP. Onset metrics cover the
first 20 ms; an optional listening copy runs for 750 ms. No filters/FX, full boot, mute, prior-note
lifecycle or physical capture. No bank/firmware edits.
"""
import argparse
import json
import wave
from pathlib import Path
import struct
from test_st_fm_audio import Port, Digitone, Syx, Elf, RETURN, sha, require_emac, DN_SHA, ST_SHA
from test_st_fm_onset import NativeAmp, DN_MAIN_SHA, metrics

ST_MAIN_SHA = '8cf2b4b4ea80d58953da46ca453328454fb3fca5cb5f844a2f7bad4b72241f53'
BASE = 0x40000400


def defaults(st_main, dn_main):
    def descriptor(image, address, size):
        fields = struct.unpack_from('>'+str(size//4)+'I', image, address-BASE)
        return dict(address=hex(address), owner=fields[0], slot=fields[1],
                    minimum=fields[2], maximum=fields[3], initial=fields[4])
    st = {name: descriptor(st_main, 0x4022d59c+52*engine, 52) for name, engine in
          (('delay', 72), ('attack', 73), ('hold', 74), ('decay', 75), ('sustain', 76),
           ('release', 77), ('mode', 84), ('reset', 85))}
    dn = {name: descriptor(dn_main, address, 60) for name, address in
          (('attack', 0x4018dd3c), ('decay', 0x4018dd78), ('sustain', 0x4018ddb4),
           ('release', 0x4018ddf0), ('reset', 0x4018df94))}
    assert [st[name]['initial'] for name in st] == [0, 0, 0x4000, 0x7f00, 0x6400, 0x2000, 0, 0x100]
    assert [dn[name]['initial'] for name in dn] == [0x800, 0x2000, 0x6000, 0x1800, 0x100]
    # Native full-Sound initialization selects descriptor 83 (ADSR) only for
    # types 4,5,20,37, otherwise keeps descriptor 84 (AD). Assert the original
    # selection window rather than assuming the duplicate MODE defaults agree.
    assert st_main[0x400b3ca6-BASE:0x400b3cc8-BASE].hex() == (
        '7254b280661c123c0014b28267126d0a123c0001b283650a6006'
        '7225b28266027053')
    return st, dn


def matched_amp_check(st_audio, dn_main, dn):
    """Compare unchanged native AMPs with a constant input, before note-off.

    Align each routine's onset explicitly; this is not a device latency test.
    Release timing and the complete device scheduler are outside this check.
    """
    controls = [0, dn['attack']['initial'], 0, dn['decay']['initial'],
                dn['sustain']['initial'], dn['release']['initial'],
                0, 0, 0, 0, 0, 0x100, dn['reset']['initial']]
    dn_controls = [dn[name]['initial'] for name in ('attack', 'decay', 'sustain', 'release')]
    dn_controls += [0]*6+[dn['reset']['initial']]
    amps = [NativeAmp(st_audio, False, 8), NativeAmp(dn_main, True, 8)]
    outputs = []
    for amp, values in zip(amps, (controls, dn_controls)):
        amp.uc.mem_write(RETURN+0x300, struct.pack('>'+str(len(values))+'H', *values))
        for _ in range(120):
            assert amp.block([0]*32) == (0,)*32
        pcm = []
        for block in range(1125):
            pcm.extend(amp.block([0x40000000]*32, trigger=block == 0))
        outputs.append(pcm)
        amp.assert_code()
    starts = [next(i for i, value in enumerate(pcm) if value) for pcm in outputs]
    assert starts == [32, 64]
    count = min(len(pcm)-start for pcm, start in zip(outputs, starts))
    assert outputs[0][starts[0]:starts[0]+count] == outputs[1][starts[1]:starts[1]+count]
    return dict(syntakt_controls=controls, digitone_controls=dn_controls,
                aligned_sample_exact=True, compared_samples=count,
                constant_input_q31=0x40000000, onset_samples=starts,
                note_off_tested=False, device_latency_measured=False)


def export_factory_ab(engines, amps, pcm, note, folder, syntakt_attack=0, match_digitone_amp=False):
    """Listening copy only: common gain, untouched onset, final 10ms file fade.

    The Syntakt default AD/DEC127 can sustain indefinitely. A disclosed fade at
    the END avoids a file-truncation click; unprocessed Q31 is exported too.
    No firmware parameter or first-note sample is changed by that file fade.
    """
    assert syntakt_attack in (0, 8)
    st_index = 3 if match_digitone_amp else (1 if syntakt_attack == 8 else 0)
    st_label = ('Syntakt-matched-ADSR' if match_digitone_amp else
                'Syntakt-ATK8' if syntakt_attack == 8 else 'Syntakt-default')
    folder.mkdir(parents=True, exist_ok=True)
    for block in range(30, 1125):  # 750ms from the measured note-on
        for i in (st_index, 2):
            pcm[i].extend(amps[i].block(engines[i].block(note=note)))
    frames, files = [], []
    for i, name in ((st_index, 'A-'+st_label), (2, 'B-Digitone-default')):
        raw = struct.pack('>'+str(len(pcm[i]))+'i', *pcm[i])
        raw_path = folder/(name+'.q31')
        raw_path.write_bytes(raw)
        converted = []
        for j, value in enumerate(pcm[i]):
            fade = min(1.0, (len(pcm[i])-1-j)/479) if j >= len(pcm[i])-480 else 1.0
            converted.append(round(value/2**31*0.2*8388607*fade))
        assert all(abs(value) <= 8388607 for value in converted)
        assert converted[-1] == 0
        payload = bytes(3840*3)+b''.join((value & 0xffffff).to_bytes(3, 'little') for value in converted)
        path = folder/(name+'.wav')
        with wave.open(str(path), 'wb') as stream:
            stream.setnchannels(1); stream.setsampwidth(3); stream.setframerate(48000)
            stream.writeframes(payload)
        # The entire first 20ms of the native onset must survive unchanged,
        # apart from the declared common listening gain and PCM24 rounding.
        expected = [round(value/2**31*0.2*8388607) for value in pcm[i][:960]]
        assert converted[:960] == expected
        frames.append(payload)
        files.append(dict(label=name, wav=str(path.resolve()), wav_sha256=sha(path.read_bytes()),
                          raw_q31=str(raw_path.resolve()), raw_q31_sha256=sha(raw),
                          onset=metrics(pcm[i])))
    sequence = frames[0]+bytes(24000*3)+frames[1]+bytes(24000*3)
    sequence += sequence
    combined = folder/('AB-Syntakt-matched-ADSR-Digitone-default.wav' if match_digitone_amp else
                       'AB-Syntakt-ATK8-Digitone-default.wav' if syntakt_attack == 8
                       else 'AB-defaults-Syntakt-Digitone.wav')
    with wave.open(str(combined), 'wb') as stream:
        stream.setnchannels(1); stream.setsampwidth(3); stream.setframerate(48000)
        stream.writeframes(sequence)
    return dict(files=files, combined=str(combined.resolve()),
                combined_sha256=sha(combined.read_bytes()),
                order=[st_label.replace('-', ' '), 'Digitone default']*2,
                syntakt_attack=8 if match_digitone_amp else syntakt_attack,
                syntakt_amp_matches_digitone=match_digitone_amp,
                midi_note=note, rate=48000, channels=1, bits=24, fixed_gain=0.2,
                normalized=False, onset_fade=False, final_file_fade_ms=10,
                native_note_duration_ms=750, leading_silence_ms=80,
                raw_q31_has_no_fade=True, filters_fx_bypassed=True,
                physical_final_output_capture=False)


def compare(bank, stock_path, dn_audio_path, dn_main_path, out, wav_dir=None, syntakt_attack=0,
            match_digitone_amp=False):
    require_emac()
    stock_os = Syx.load(stock_path)
    st_main, st_audio = stock_os.section(3), stock_os.section(7)
    dn_audio, dn_main = dn_audio_path.read_bytes(), dn_main_path.read_bytes()
    assert (sha(st_main), sha(st_audio), sha(dn_main), sha(dn_audio)) == (
        ST_MAIN_SHA, ST_SHA, DN_MAIN_SHA, DN_SHA)
    st, dn = defaults(st_main, dn_main)
    amp_match = matched_amp_check(st_audio, dn_main, dn)
    print('PASS matched native ADSR: 8/32/96/24, sample-exact after onset alignment', flush=True)
    body = bank.read_bytes()
    assert sha(body) == json.loads(bank.with_suffix(bank.suffix+'.json').read_text())['sha256']
    elf = Elf.load(bank.with_suffix('.work')/'audio.elf')
    section = elf.section('.image')
    address = struct.unpack_from('>I', elf.raw, struct.unpack_from('>I', elf.raw, 32)[0]+section.idx*40+12)[0]
    symbols = {symbol.name: symbol.value for symbol in elf.symbols if symbol.name}
    offset = 48+struct.unpack_from('>I', body, 72)[0]+address-0x4204f750
    assert body[offset:offset+len(section.data)] == section.data
    results, exported = [], None
    for note in (48, 60, 69, 72):
        ports = [Port(st_audio, section.data, address, symbols, 0, 0, 0x100) for _ in range(3)]
        # Every SYN control is INIT, including operator attacks/levels = 0.
        for port in ports:
            port.write32(0x80008184, 14400)  # native default 120 BPM clock
            port.raw[4], port.raw[8] = 0x3f00, 0
            ext = list(struct.unpack('>20H', port.uc.mem_read(RETURN+0x2000, 40)))
            ext[0] = ext[3] = ext[4] = ext[7] = 0
            port.uc.mem_write(RETURN+0x2000, struct.pack('>20H', *ext))
            port.block(note=note)
        params = bytes(ports[0].uc.mem_read(symbols['fm_state']+0xf28, 158))
        native = Digitone(dn_audio, params)
        native.write32(0x80000378, 14400)
        native.block(note=note)
        amps = [NativeAmp(st_audio, False, 0), NativeAmp(st_audio, False, 8), NativeAmp(dn_main, True, 8)]
        for amp, attack in zip(amps[:2], (0, 8)):
            controls = [st['delay']['initial'], attack*256, st['hold']['initial'],
                        st['decay']['initial'], st['sustain']['initial'], st['release']['initial'],
                        0, 0, 0, 0, 0, st['mode']['initial'], st['reset']['initial']]
            amp.uc.mem_write(RETURN+0x300, struct.pack('>13H', *controls))
        controls = [dn[name]['initial'] for name in ('attack', 'decay', 'sustain', 'release')]
        controls += [0]*6+[dn['reset']['initial']]
        amps[2].uc.mem_write(RETURN+0x300, struct.pack('>11H', *controls))
        matched = NativeAmp(st_audio, False, 8)
        matched_controls = [0, dn['attack']['initial'], 0, dn['decay']['initial'],
                            dn['sustain']['initial'], dn['release']['initial'],
                            0, 0, 0, 0, 0, 0x100, dn['reset']['initial']]
        matched.uc.mem_write(RETURN+0x300, struct.pack('>13H', *matched_controls))
        amps.append(matched)
        engines = ports[:2]+[native, ports[2]]
        for _ in range(120):
            for engine, amp in zip(engines, amps):
                assert amp.block(engine.block(note=note)) == (0,)*32
        pcm = [[] for _ in engines]
        for block in range(30):
            for i, (engine, amp) in enumerate(zip(engines, amps)):
                pcm[i].extend(amp.block(engine.block(note=note, trigger=block == 0), trigger=block == 0))
        measurements = [metrics(samples) for samples in pcm]
        assert measurements[1]['onset_step_from_silence'] < measurements[0]['onset_step_from_silence']/10
        assert measurements[2]['onset_step_from_silence'] < measurements[0]['onset_step_from_silence']/10
        results.append(dict(note=note, paths=dict(zip(('syntakt_factory', 'syntakt_only_atk8',
                            'digitone_factory', 'syntakt_matched_adsr'), measurements))))
        if note == 60 and wav_dir:
            exported = export_factory_ab(engines, amps, pcm, note, wav_dir, syntakt_attack,
                                         match_digitone_amp)
        for amp in amps:
            amp.assert_code()
        print('PASS factory AMP defaults, first-note step reduced >20dB with ATK8, note', note, flush=True)
    step = struct.unpack_from('>i', st_audio, 0x40014070-BASE+4*8)[0]
    result = dict(bank_sha256=sha(body), syntakt_amp_descriptors=st, digitone_amp_descriptors=dn,
                  attack8_linear_ramp_ms=(2**31/abs(step))/48, cases=results,
                  syntakt_matched_adsr_controls=matched_controls,
                  matched_native_amp_check=amp_match,
                  note_on_scheduling_explicit_fixture=True, filters_fx_bypassed=True,
                  physical_click_reproduced=False, dsp_or_bank_modified=False,
                  full_sound_load_and_machine_selection_executed=False)
    if exported:
        result['factory_ab'] = exported
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('bank', 'stock', 'digitone-audio', 'digitone-main', 'out'):
        parser.add_argument('--'+name, required=True, type=Path)
    parser.add_argument('--wav-dir', type=Path, help='Export native-default A/B at MIDI note 60')
    parser.add_argument('--syntakt-attack', type=int, choices=(0, 8), default=0,
                        help='Syntakt AMP ATK for the listening copy; Digitone stays at its defaults')
    parser.add_argument('--match-digitone-amp', action='store_true',
                        help='Use Syntakt ADSR with Digitone AMP defaults; overrides --syntakt-attack')
    args = parser.parse_args()
    compare(args.bank.resolve(), args.stock, args.digitone_audio, args.digitone_main,
            args.out, args.wav_dir, args.syntakt_attack, args.match_digitone_amp)
