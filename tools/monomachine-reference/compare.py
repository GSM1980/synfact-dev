#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproducible GND-SIN baseline: original DSP/neutral chain vs raw bank OSC."""
import argparse
import ctypes
import hashlib
import html
import json
import os
from pathlib import Path
import subprocess

import numpy as np
from scipy.io import wavfile
from scipy.optimize import minimize_scalar
from scipy.signal import resample_poly

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_wav(path):
    rate, values = wavfile.read(path)
    if np.issubdtype(values.dtype, np.integer):
        values = values.astype(np.float64) / (2 ** (np.iinfo(values.dtype).bits - 1))
    else:
        values = values.astype(np.float64)
    if values.ndim == 1:
        values = values[:, None]
    if not np.isfinite(values).all():
        raise ValueError(f'nonfinite WAV: {path}')
    return rate, values


def db(value):
    return float(20 * np.log10(max(float(value), 1e-15)))


def fit_tone(values, rate, expected, start=.2, end=.9):
    """Estimate frequency independently, then fit harmonics at physical Hz.

    A coarse FFT search is followed by a variable-frequency sine/DC fit.
    Each WAV is analysed at its own native rate; no resampling affects metrics.
    """
    y = np.asarray(values[int(start*rate):int(end*rate)], dtype=np.float64)
    if len(y) < 64 or np.sqrt(np.mean(y*y)) < 1e-8:
        raise ValueError('not enough audible steady audio')
    t = np.arange(len(y), dtype=np.float64) / rate
    spectrum = abs(np.fft.rfft((y-y.mean()) * np.hanning(len(y)), n=4*len(y)))
    frequencies = np.fft.rfftfreq(4*len(y), 1/rate)
    region = (frequencies > expected*.95) & (frequencies < expected*1.05)
    peak = np.flatnonzero(region)[np.argmax(spectrum[region])]
    coarse = frequencies[peak]

    def cost(f):
        design = np.column_stack((np.sin(2*np.pi*f*t), np.cos(2*np.pi*f*t), np.ones(len(t))))
        coef = np.linalg.lstsq(design, y, rcond=None)[0]
        residual = y-design@coef
        return float(residual@residual)

    delta = rate/len(y)
    result = minimize_scalar(cost, bounds=(coarse-delta, coarse+delta), method='bounded',
                             options={'xatol': 1e-9})
    frequency = float(result.x)
    harmonics = min(8, int((rate/2-1)/frequency))
    columns = [np.ones(len(t))]
    for n in range(1, harmonics+1):
        columns.extend((np.sin(2*np.pi*frequency*n*t), np.cos(2*np.pi*frequency*n*t)))
    design = np.column_stack(columns)
    coef = np.linalg.lstsq(design, y, rcond=None)[0]
    amplitudes = [float(np.hypot(coef[2*n-1], coef[2*n])) for n in range(1, harmonics+1)]
    residual = y-design@coef
    return {'frequency_hz': frequency, 'cents_from_expected': float(1200*np.log2(frequency/expected)),
            'rms_dbfs': db(np.sqrt(np.mean(y*y))), 'peak_dbfs': db(max(abs(y))),
            'dc': float(coef[0]), 'fundamental_peak': amplitudes[0],
            'harmonics_dbc': [db(a/amplitudes[0]) for a in amplitudes],
            'thd_db': db(np.linalg.norm(amplitudes[1:])/amplitudes[0]),
            'residual_dbfs': db(np.sqrt(np.mean(residual*residual))),
            'phase_deg_at_window_start': float(np.degrees(np.arctan2(coef[2], coef[1])))}


def phase_jump(values, rate, frequency, time):
    """Phase extrapolated to the same event time from either steady window."""
    def phase(a, b):
        begin, end = round(a*rate), round(b*rate)
        t = np.arange(begin, end)/rate
        design = np.column_stack((np.sin(2*np.pi*frequency*t), np.cos(2*np.pi*frequency*t), np.ones(len(t))))
        coef = np.linalg.lstsq(design, values[begin:end], rcond=None)[0]
        return np.arctan2(coef[1], coef[0])
    difference = phase(time+.025, time+.075)-phase(time-.075, time-.025)
    return float(np.degrees(np.angle(np.exp(1j*difference))))


def host_engine(cc, out):
    dll = (out/'bank-generators.dll').resolve()
    sources = [HERE/'host.c', ROOT/'mods/digimono-st/engine.c', ROOT/'mods/digimono-st/sine.c',
               ROOT/'mods/digimono-st/mono2k.c', ROOT/'mods/digimono-st/fmplus.c']
    command = [str(cc), '-O3', '-shared', '-Wall', '-Werror', '-I', str(ROOT/'mods/digimono-st'),
               *map(str, sources), '-o', str(dll)]
    subprocess.run(command, check=True, capture_output=True, text=True)
    lib = ctypes.CDLL(str(dll))
    lib.dm_compare_bytes.restype = ctypes.c_uint
    lib.dm_compare_init.argtypes = [ctypes.c_void_p]
    lib.dm_compare_block.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_int, ctypes.c_void_p,
                                    ctypes.c_int, ctypes.c_uint, ctypes.c_void_p]
    lib.dm_compare_stereo_block.argtypes = lib.dm_compare_block.argtypes+[ctypes.c_void_p]
    return lib, sources


def raw_render(lib, case, frames):
    state = ctypes.create_string_buffer(lib.dm_compare_bytes())
    lib.dm_compare_init(state)
    p = (ctypes.c_uint16*7)(*([0]*7))
    block = (ctypes.c_float*32)()
    out = np.zeros(frames, dtype=np.float32)
    events = {round(t*48000): note for t, note in case['events']}
    assert all(frame%32 == 0 for frame in events)
    note = case['note']
    for frame in range(0, frames, 32):
        trigger = frame in events
        if trigger:
            note = events[frame]
        # Original SYN TUNE is +/-2 semitones; our common TUNE has one
        # semitone/step. Match physical pitch: 4 Q7 units per original step.
        pitch = note*128+(case['tune']-64)*4
        lib.dm_compare_block(state, 0, pitch, p, 60*128, trigger, block)
        count = min(32, frames-frame)
        out[frame:frame+count] = np.ctypeslib.as_array(block)[:count]
    return out


def cases():
    result = [{'name': f'sin_n{n:03}', 'note': n, 'tune': 64, 'events': [(0, n)]}
              for n in (24, 36, 48, 60, 69, 72, 84, 96)]
    result += [{'name': f'sin_tune{t:03}', 'note': 60, 'tune': t, 'events': [(0, 60)]}
               for t in (0, 32, 96, 127)]
    result += [{'name': 'sin_retrigger', 'note': 60, 'tune': 64,
                'events': [(0, 60), (.32, 60), (.64, 60)]}]
    return result


def listen_ab(reference, raw, rate, path):
    # Listening only: both clips at the same RMS, ample headroom. The source
    # WAVs and all level measurements keep their original gain.
    reference = resample_poly(reference, 160, 147)
    clips = []
    for values in (reference, raw):
        rms = np.sqrt(np.mean(values[int(.2*rate):int(.9*rate)]**2))
        clips.append(values * (.15/max(rms, 1e-12)))
    peak = max(max(abs(c)) for c in clips)
    if peak > .95:
        clips = [c*.95/peak for c in clips]
    gap = np.zeros(round(.25*rate))
    wavfile.write(path, rate, np.concatenate((clips[0], gap, clips[1])).astype(np.float32))


def plot_report(rows, audio, out):
    os.environ.setdefault('MPLCONFIGDIR', str(out/'matplotlib-cache'))
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(2, 2, figsize=(13, 8), layout='constrained')
    pitch = [r for r in rows if r['name'].startswith('sin_n')]
    notes = [r['note'] for r in pitch]
    axes[0, 0].plot(notes, [r['reference']['cents_from_expected'] for r in pitch], 'o-', label='MNM: cadena neutra')
    axes[0, 0].plot(notes, [r['raw']['cents_from_expected'] for r in pitch], 's-', label='Digi Mono: OSC bruto')
    axes[0, 0].set(title='Afinación respecto a temperamento igual / A4=440', xlabel='Nota MIDI', ylabel='Cents')
    axes[0, 1].plot(notes, [r['reference']['rms_dbfs'] for r in pitch], 'o-', label='MNM: cadena neutra')
    axes[0, 1].plot(notes, [r['raw']['rms_dbfs'] for r in pitch], 's-', label='Digi Mono: OSC bruto')
    axes[0, 1].set(title='Nivel medido: etapas diferentes', xlabel='Nota MIDI', ylabel='RMS (dBFS)')
    for label, values, sr in [('MNM: cadena neutra', audio['reference'], 44100), ('Digi Mono: OSC bruto', audio['raw'], 48000)]:
        values = values[:round(.012*sr)]
        axes[1, 0].plot(np.arange(len(values))*1000/sr, values, label=label)
    axes[1, 0].set(title='Primeros 12 ms de A4 (sin alinear ni normalizar)', xlabel='Tiempo (ms)', ylabel='Amplitud')
    a4 = next(r for r in rows if r['name']=='sin_n069')
    h = np.arange(2, 9)
    axes[1, 1].bar(h-.18, a4['reference']['harmonics_dbc'][1:], .36, label='MNM: cadena neutra')
    axes[1, 1].bar(h+.18, a4['raw']['harmonics_dbc'][1:], .36, label='Digi Mono: OSC bruto')
    axes[1, 1].set(title='Armónicos de A4 respecto a su fundamental', xlabel='Armónico', ylabel='dBc', ylim=(-150, 0))
    for axis in axes.flat:
        axis.grid(alpha=.2);axis.legend(fontsize=8)
    fig.suptitle('GND-SIN · línea base de comparación · OS 1.32B', fontsize=16)
    fig.savefig(out/'sin-baseline.png', dpi=150)
    plt.close(fig)
    fig, axes = plt.subplots(2, 1, figsize=(12, 5), sharex=True, layout='constrained')
    for axis, label, key, sr, color in [(axes[0], 'MNM: cadena neutra', 'retrigger_reference', 44100, 'C0'),
                                        (axes[1], 'Digi Mono: OSC bruto', 'retrigger_raw', 48000, 'C1')]:
        begin, end = round(.312*sr), round(.328*sr)
        values = audio[key][begin:end]
        axis.plot((np.arange(begin, end)/sr-.32)*1000, values/max(abs(values)), color=color)
        axis.axvline(0, color='black', linestyle='--', alpha=.6, label='Nota repetida programada')
        axis.set(ylabel='Amplitud normalizada', title=label, ylim=(-1.15, 1.15))
        axis.grid(alpha=.2);axis.legend(fontsize=8, loc='upper right')
    axes[1].set_xlabel('Tiempo respecto al retrigger (ms)')
    fig.suptitle('SIN · continuidad de fase alrededor de la nota repetida', fontsize=14)
    fig.savefig(out/'sin-retrigger.png', dpi=150)
    plt.close(fig)


def write_report(result, out):
    rows = result['cases']
    maximum = max(abs(row['difference_cents']) for row in rows)
    text = ['# GND-SIN: primera línea base', '',
            '**Referencia:** DSP original de Monomachine OS 1.32B en Monomodule/JIT, con AMP sostenido, filtros abiertos y efectos neutralizados.', '',
            '**Comparación:** generador Digi Mono del banco actual a 48 kHz, antes de declick, ganancias, FLTR, AMP y FX de Syntakt.', '',
            'Los niveles absolutos corresponden a etapas distintas. Su diferencia no demuestra por sí sola un error del oscilador ni predice el volumen final de Syntakt. Afinación, armónicos y retrigger se analizan por separado. No se ha probado una Monomachine física.', '',
            f'**Afinación:** diferencia máxima observada {maximum:.3f} cents en estos 13 casos. **Retrigger con AMP sostenido y sin note-off:** la referencia conserva la fase; Digi Mono muestra un salto al reiniciarla. La diferencia de afinación puede incluir la cuantización/MASTER TUNE del modelo de referencia, por lo que no se propone una corrección global de pitch.', '',
            '| Caso | MNM Hz | Digi Mono Hz | Diferencia (cents) | MNM RMS dBFS | OSC RMS dBFS |',
            '|---|---:|---:|---:|---:|---:|']
    for row in rows:
        a, b = row['reference'], row['raw']
        text.append(f"| {row['name']} | {a['frequency_hz']:.5f} | {b['frequency_hz']:.5f} | {row['difference_cents']:+.3f} | {a['rms_dbfs']:.2f} | {b['rms_dbfs']:.2f} |")
    text += ['', '## Retrigger', '', 'Saltos de fase estimados entre ventanas estables antes/después de cada nota repetida. Las ventanas omiten el transitorio; esto no mide el clic ni sustituye a la escucha.', '']
    for event in result['retrigger']:
        text.append(f"- {event['time_s']:.2f} s: MNM {event['reference_jump_deg']:+.2f}°, Digi Mono {event['raw_jump_deg']:+.2f}°.")
    text += ['', '## Audios', '', 'En cada A/B se escucha primero Monomachine y después Digi Mono. Solo el A/B está igualado en RMS; los WAV originales conservan su nivel.', '',
             '- [A/B de A4](sin_n069-ab.wav)', '- [A/B de retrigger](sin_retrigger-ab.wav)',
             '- [Gráficas](sin-baseline.png)', '- [Gráfica de retrigger](sin-retrigger.png)', '- [Resultados y procedencia](results.json)', '',
             'No se modifica el motor de audio en esta ronda. Los resultados son una línea base para decidir las correcciones siguientes.', '']
    (out/'report.md').write_text('\n'.join(text), encoding='utf-8')
    table = ''.join('<tr><td>'+html.escape(r['name'])+'</td><td>'+f"{r['difference_cents']:+.3f}"+'</td><td>'+f"{r['reference']['rms_dbfs']:.2f}"+'</td><td>'+f"{r['raw']['rms_dbfs']:.2f}"+'</td><td><audio controls preload="none" src="'+r['name']+'-ab.wav"></audio></td></tr>' for r in rows)
    document = '''<!doctype html><html lang="es"><meta charset="utf-8"><title>GND-SIN: comparación</title>
<style>body{font:16px system-ui;max-width:1180px;margin:35px auto;padding:0 20px;color:#18202b;background:#fafbfc}table{border-collapse:collapse;width:100%}td,th{padding:9px;border-bottom:1px solid #ddd;text-align:left}img{width:100%}.note{background:#e9eef5;padding:18px;line-height:1.5}audio{width:280px}</style>
<h1>GND-SIN · primera comparación</h1><p class="note">DSP original OS 1.32B con cadena neutralizada frente a OSC bruto de Digi Mono. Los niveles corresponden a etapas diferentes; no son una medida del volumen final de Syntakt. No hay validación con hardware físico.</p>
<img src="sin-baseline.png" alt="Afinación, niveles, inicio de nota y armónicos"><p>A/B: primero Monomachine, después Digi Mono; solo la escucha está igualada en RMS.</p>
<img src="sin-retrigger.png" alt="Continuidad y reinicio de fase alrededor del retrigger"><table><tr><th>Caso</th><th>OSC−MNM cents</th><th>MNM dBFS</th><th>OSC dBFS</th><th>A/B</th></tr>'''+table+'</table><p><a href="report.md">Informe completo y retrigger</a> · <a href="results.json">Resultados y procedencia</a></p></html>'
    (out/'report.html').write_text(document, encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--os', type=Path, default=os.environ.get('MONOMACHINE_OS'), required=not os.environ.get('MONOMACHINE_OS'))
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--cc', type=Path, required=True, help='64-bit host compiler')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    build_record = args.reference.resolve().parent/'reference-build.json'
    if not build_record.is_file():
        raise RuntimeError('build the pinned reference with tools/monomachine-reference/build.py first')
    reference_build = json.loads(build_record.read_text())
    for name, digest in reference_build['driver_sha256'].items():
        if sha(HERE/name) != digest:
            raise RuntimeError(f'reference source changed; rebuild before comparing: {name}')
    lib, sources = host_engine(args.cc, out)
    result = {'schema': 1, 'scope': 'original neutral track-chain vs raw bank generator; not end-to-end Syntakt',
              'provenance': json.loads((HERE/'provenance.json').read_text()),
              'reference_build': reference_build,
              'os_sha256': sha(args.os), 'reference_executable_sha256': sha(args.reference),
              'host_sources': {p.relative_to(ROOT).as_posix(): sha(p) for p in
                               [HERE/'host.c', HERE/'reference.cpp', HERE/'reference.cmake', HERE/'compat.h', HERE/'mingw.patch',
                                HERE/'compare.py', *sorted(p for p in (ROOT/'mods/digimono-st').rglob('*')
                                 if p.is_file() and p.suffix in ('.c', '.h', '.inc', '.json'))]},
              'analysis_versions': {'numpy': np.__version__, 'scipy': __import__('scipy').__version__},
              'analysis_window_s': [.2, .9], 'reference_rate': 44100, 'raw_rate': 48000,
              'reference_chain': {'amp': [0,127,127,0,64,64,64,0], 'filt': [0,127,0,0,0,0,64,64],
                                  'efx': [64,64,0,64,64,0,0,127], 'level': 100, 'warmup_blocks': 8},
              'cases': [], 'retrigger': []}
    logs = []
    for case in cases():
        name = case['name']
        reference_path, raw_path = out/(name+'-reference.wav'), out/(name+'-raw.wav')
        events = [(round(t*44100), n) for t, n in case['events']]
        assert all(frame%16 == 0 for frame, _ in events)
        command = [str(args.reference.resolve()), '--fw', str(args.os.resolve()), '--out', str(reference_path),
                   '--frames', '44100', '--machine', '1', '--syn', ','.join(map(str, [0]*7+[case['tune']])),
                   '--events', ','.join(f'{frame}:{note}' for frame, note in events)]
        with (out/(name+'-reference.log')).open('w', encoding='utf-8') as log:
            run = subprocess.run(command, check=True, stdout=subprocess.PIPE, stderr=log, text=True, timeout=90)
        logs.append(name+'\n'+run.stdout+(out/(name+'-reference.log')).read_text(encoding='utf-8'))
        rate, ref = load_wav(reference_path)
        assert rate == 44100 and len(ref) == 44100
        raw = raw_render(lib, case, 48000)
        wavfile.write(raw_path, 48000, raw)
        expected = 440*2**((case['note']-69+(case['tune']-64)/32)/12)
        # Retrigger discontinuities would contaminate a long steady fit.
        end = .29 if len(events)>1 else .9
        a, b = fit_tone(ref[:, 0], rate, expected, .2, end), fit_tone(raw, 48000, expected, .2, end)
        row = dict(case, expected_hz=expected, reference=a, raw=b,
                   difference_cents=float(1200*np.log2(b['frequency_hz']/a['frequency_hz'])),
                   reference_sha256=sha(reference_path), raw_sha256=sha(raw_path))
        result['cases'].append(row)
        listen_ab(ref[:, 0], raw, 48000, out/(name+'-ab.wav'))
        if name == 'sin_n069':
            a4_audio = {'reference': ref[:, 0], 'raw': raw}
            # A fresh reference instance should be repeatable before phase
            # measurements are interpreted. Use the exact same invocation.
            repeat = out/'repeat-reference.wav'
            again = command.copy();again[again.index('--out')+1] = str(repeat)
            subprocess.run(again, check=True, capture_output=True, text=True, timeout=90)
            result['reference_fresh_instance_repeatable'] = sha(repeat)==sha(reference_path)
            if not result['reference_fresh_instance_repeatable']:
                raise RuntimeError('reference is not repeatable across fresh instances')
        if len(events)>1:
            a4_audio['retrigger_reference'], a4_audio['retrigger_raw'] = ref[:, 0], raw
            for time, _ in case['events'][1:]:
                result['retrigger'].append({'time_s': time,
                    'reference_jump_deg': phase_jump(ref[:, 0], rate, a['frequency_hz'], time),
                    'raw_jump_deg': phase_jump(raw, 48000, b['frequency_hz'], time)})
        print(f"{name}: OSC-MNM {row['difference_cents']:+.3f} cents", flush=True)
    (out/'reference.log').write_text('\n'.join(logs), encoding='utf-8')
    (out/'results.json').write_text(json.dumps(result, indent=2, allow_nan=False)+'\n', encoding='utf-8')
    plot_report(result['cases'], a4_audio, out)
    write_report(result, out)
    print('Created report.md, report.html, sin-baseline.png and per-case WAV/JSON files; no hardware access.')


if __name__ == '__main__':
    main()
