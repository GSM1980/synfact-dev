#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Multi-engine timbre/control bench; original neutral track chain vs raw OSC.

Spectral distance is a diagnostic, not a percentage of fidelity. Absolute gain
is deliberately not scored across unmatched track chains.
"""
import argparse
import ctypes
import html
import json
import os
from pathlib import Path
import shutil
import subprocess

import numpy as np
from scipy.integrate import cumulative_trapezoid
from scipy.signal import find_peaks, resample_poly, welch

import compare as common
from complex_cases import FAMILIES, cases, ens_cases, ens_shape_cases

HERE, ROOT = common.HERE, common.ROOT
BAND_EDGES = 20*2**(np.arange(121)/12)
QUIET_DBFS = -90


def power_bands(frequencies, power, edges=BAND_EDGES):
    integral = cumulative_trapezoid(power, frequencies, initial=0)
    return np.maximum(0, np.diff(np.interp(edges, frequencies, integral)))


def spectral_distance(a, b):
    """Square-root Jensen-Shannon distance, 0..1, on energy distributions."""
    a, b = np.asarray(a, float), np.asarray(b, float)
    if a.sum() <= 0 or b.sum() <= 0:
        return None
    a, b = a/a.sum(), b/b.sum()
    mid = .5*(a+b)
    def divergence(p):
        mask = p > 0
        return np.sum(p[mask]*np.log2(p[mask]/mid[mask]))
    return float(np.sqrt(max(0, .5*(divergence(a)+divergence(b)))))


def stereo_metrics(values):
    if values.ndim == 1 or values.shape[1] == 1:
        return {'correlation': 1.0, 'side_to_mid_db': -300.0}
    left, right = values[:, 0], values[:, 1]
    mid, side = (left+right)/2, (left-right)/2
    a, b = left-left.mean(), right-right.mean()
    norm = np.linalg.norm(a)*np.linalg.norm(b)
    return {'correlation': float(a@b/norm) if norm > 1e-20 else None,
            'side_to_mid_db': common.db(np.sqrt(np.mean(side*side))/max(np.sqrt(np.mean(mid*mid)), 1e-15))}


def audio_metrics(values, rate, start, end):
    values = np.asarray(values, float)
    if values.ndim == 1:
        values = values[:, None]
    signal = values[round(start*rate):round(end*rate)]
    if len(signal) < 64 or not np.isfinite(signal).all():
        raise ValueError('insufficient or nonfinite audio')
    centered = signal-signal.mean(axis=0)
    ac = float(np.sqrt(np.mean(centered*centered)))
    frequencies, channel_power = welch(centered, rate, nperseg=min(32768, len(signal)), axis=0,
                                       detrend=False, scaling='density')
    # Average channel power, not the L+R waveform: antiphase stereo must not
    # disappear from the timbre measurement.
    power = channel_power.mean(axis=1)
    bands = power_bands(frequencies, power)
    audible = common.db(ac) > QUIET_DBFS
    within = (frequencies >= 20) & (frequencies <= 20000)
    total = power[within].sum()
    centroid = float((power[within]*frequencies[within]).sum()/total) if audible and total > 1e-25 else None
    peaks, _ = find_peaks(power)
    peaks = [int(i) for i in peaks if frequencies[i] >= 20]
    peaks = sorted(peaks, key=lambda i: power[i], reverse=True)[:8] if audible else []
    peak_power = max((power[i] for i in peaks), default=0)
    strongest = [{'hz': float(frequencies[i]), 'relative_db': float(10*np.log10(max(power[i], 1e-30)/max(peak_power, 1e-30)))}
                 for i in peaks]
    return {'rms_dbfs': common.db(np.sqrt(np.mean(signal*signal))), 'ac_rms_dbfs': common.db(ac),
            'peak_dbfs': common.db(np.max(abs(signal))), 'audible': audible,
            'centroid_hz': centroid, 'peaks': strongest, 'stereo': stereo_metrics(centered),
            'normalized_bands': (bands/bands.sum()).tolist() if audible and bands.sum()>0 else [0.0]*len(bands)}, (frequencies, power)


def temporal_metrics(values, rate, duration):
    result = []
    for start, end in ((.02,.12),(.2,.35),(.7,.9),(1.4,1.8)):
        if end <= duration:
            metrics, _ = audio_metrics(values, rate, start, end)
            result.append({'window_s': [start, end], 'ac_rms_dbfs': metrics['ac_rms_dbfs'],
                           'centroid_hz': metrics['centroid_hz'], 'normalized_bands': metrics['normalized_bands']})
    return result


def raw_render(lib, case):
    frames = round(case['duration_s']*48000)
    state = ctypes.create_string_buffer(lib.dm_compare_bytes())
    lib.dm_compare_init(state)
    p = (ctypes.c_uint16*7)(*(value<<8 for value in case['params']))
    block = (ctypes.c_float*32)()
    stereo=case['bank_type']==4
    right=(ctypes.c_float*32)()
    out = np.zeros((frames,2) if stereo else frames, dtype=np.float32)
    events = {round(time*48000): note for time, note in case['events']}
    assert all(frame%32 == 0 for frame in events)
    note = case['note']
    for frame in range(0, frames, 32):
        trigger = frame in events
        if trigger:
            note = events[frame]
        pitch = note*128+(case['tune']-64)*4
        if stereo:lib.dm_compare_stereo_block(state,case['bank_type'],pitch,p,60*128,trigger,block,right)
        else:lib.dm_compare_block(state, case['bank_type'], pitch, p, 60*128, trigger, block)
        count = min(32, frames-frame)
        if stereo:
            out[frame:frame+count,0]=np.ctypeslib.as_array(block)[:count]
            out[frame:frame+count,1]=np.ctypeslib.as_array(right)[:count]
        else:out[frame:frame+count] = np.ctypeslib.as_array(block)[:count]
    return out


def listen_ab(reference, raw, path, window):
    clips = [resample_poly(reference, 160, 147, axis=0), raw if raw.ndim==2 else np.repeat(raw[:, None], 2, axis=1)]
    a, b = (round(time*48000) for time in window)
    def ac_rms(values):
        return float(np.sqrt(np.mean((values-values.mean(axis=0))**2)))
    gains = [ac_rms(values[a:b]) for values in clips]
    # A consonant can end before the steady window. In that case use whole-
    # clip RMS for both sounds, so the original transient is never discarded.
    whole_clip = any(common.db(value)<=QUIET_DBFS for value in gains)
    if whole_clip:
        gains = [ac_rms(values) for values in clips]
    for i, (values, rms) in enumerate(zip(clips, gains)):
        # Preserve genuinely silent cases, rather than amplifying numerical
        # residue by 100 dB and presenting it as an audible oscillator.
        clips[i] = values*(.13/rms) if common.db(rms)>QUIET_DBFS else np.zeros_like(values)
    peak = max(float(np.max(abs(values))) for values in clips)
    if peak > .95:
        clips = [values*.95/peak for values in clips]
    gap = np.zeros((12000, 2))
    common.wavfile.write(path, 48000, np.concatenate((clips[0], gap, clips[1])).astype(np.float32))
    return 'whole_clip_ac_rms' if whole_clip else 'steady_window_ac_rms'


def relative_gain_change(row, anchor):
    if not all(x['audible'] for x in (row['reference'], row['raw'], anchor['reference'], anchor['raw'])):
        return None
    return ((row['raw']['ac_rms_dbfs']-anchor['raw']['ac_rms_dbfs'])-
            (row['reference']['ac_rms_dbfs']-anchor['reference']['ac_rms_dbfs']))


def score_case(case, reference, raw):
    end = case['duration_s']-.1
    window = [.25, end]
    a, psd_a = audio_metrics(reference, 44100, *window)
    b, psd_b = audio_metrics(raw, 48000, *window)
    if a['audible'] and b['audible']:
        status = 'ambos audibles en ventana estable'
        distance = spectral_distance(a['normalized_bands'], b['normalized_bands'])
    elif a['audible'] != b['audible']:
        status = 'solo referencia audible en ventana estable' if a['audible'] else 'solo nuestro OSC audible en ventana estable'
        distance = None
    else:
        status, distance = 'ambos bajo umbral en ventana estable', None
    row = dict(case, analysis_window_s=window, reference=a, raw=b, status=status,
               spectral_js_distance=distance,
               temporal_reference=temporal_metrics(reference, 44100, case['duration_s']),
               temporal_raw=temporal_metrics(raw, 48000, case['duration_s']))
    first_a, first_b = row['temporal_reference'][0], row['temporal_raw'][0]
    row['early_spectral_js_distance'] = spectral_distance(first_a['normalized_bands'], first_b['normalized_bands'])
    return row, (psd_a, psd_b)


def plot_report(result, spectra, out):
    os.environ.setdefault('MPLCONFIGDIR', str(out/'matplotlib-cache'))
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    rows = result['cases']
    families=list(dict.fromkeys(row['family'] for row in rows))
    fig, axis = plt.subplots(figsize=(12, 6), layout='constrained')
    for y, family in enumerate(families):
        values = [row['spectral_js_distance'] for row in rows if row['family']==family and row['spectral_js_distance'] is not None]
        jitter = np.linspace(-.13, .13, len(values))
        axis.scatter(values, y+jitter, alpha=.75, s=28)
    axis.set(yticks=range(len(families)), yticklabels=families, xlim=(-.02,1.02),
             xlabel='Distancia espectral normalizada (0: igual distribución; 1: disjunta)',
             title='Motores complejos · casos individuales · diagnóstico, no porcentaje de fidelidad')
    axis.grid(axis='x', alpha=.25)
    fig.savefig(out/'overview.png', dpi=140);plt.close(fig)
    selected = []
    for family in families:
        candidates = [row for row in rows if row['family']==family and row['spectral_js_distance'] is not None]
        row = max(candidates, key=lambda r:r['spectral_js_distance'])
        selected.append(row['name'])
        fig, axes = plt.subplots(2,1,figsize=(12,7),layout='constrained')
        for label, (frequencies, power) in zip(('MNM: cadena neutra', 'Nuestro OSC bruto'), spectra[row['name']]):
            bands = power_bands(frequencies, power)
            centres = np.sqrt(BAND_EDGES[1:]*BAND_EDGES[:-1])
            axes[0].semilogx(centres, 10*np.log10(np.maximum(bands/max(bands.max(),1e-30),1e-9)), label=label)
        axes[0].set(title=f"{family} · {row['feature']}", xlim=(20,20000), ylim=(-80,3),
                    xlabel='Hz', ylabel='Energía por banda / máximo (dB)')
        for label, key in [('MNM: cadena neutra','temporal_reference'),('Nuestro OSC bruto','temporal_raw')]:
            profile = row[key]
            x = [np.mean(item['window_s']) for item in profile]
            axes[1].plot(x, [item['centroid_hz'] for item in profile], 'o-', label=label)
        axes[1].set(title='Evolución del centro de energía espectral', xlabel='Tiempo desde la nota (s)', ylabel='Centroide (Hz)')
        for axis in axes:
            axis.grid(alpha=.25);axis.legend()
        fig.savefig(out/(row['family'].lower().replace('+','_').replace('-','_')+'-detail.png'), dpi=140)
        plt.close(fig)
    result['plotted_cases'] = selected


def write_report(result, out):
    rows = result['cases']
    families=list(dict.fromkeys(row['family'] for row in rows))
    notes=sorted(set(row['note'] for row in rows))
    lines = ['# Máquinas complejas: comparación con Monomachine', '',
             f"{len(rows)} casos en {len(families)} motores, notas MIDI {notes}, mismos valores crudos de controles y misma afinación física.", '',
             'Referencia: DSP original OS 1.32B en Monomodule/JIT con cadena neutralizada. Nuestro lado: generador del banco, antes de declick/gain/FLTR/AMP/FX de Syntakt. No hay validación con Monomachine física.', '',
             'La distancia espectral compara energía normalizada en bandas de 1/12 de octava entre 20 Hz y 20,48 kHz. No es un porcentaje de fidelidad, ni distingue por sí sola diferencias del generador y de la cadena residual. Las fases y el volumen global no forman parte de esa distancia. El ruido y la modulación requieren interpretación estadística.', '',
             'La respuesta de nivel es el cambio respecto al caso base de cada motor, restando el cambio original. Cancela una ganancia constante entre cadenas, pero conserva posibles diferencias de filtrado, saturación y modulación. No es una calibración de ganancia absoluta.', '',
             '| Motor | Casos | Mayor distancia espectral | Caso |', '|---|---:|---:|---|']
    sections = []
    for family in families:
        group = [row for row in rows if row['family']==family]
        audible = [row for row in group if row['spectral_js_distance'] is not None]
        worst = max(audible,key=lambda r:r['spectral_js_distance'])
        lines.append(f"| {family} | {len(group)} | {worst['spectral_js_distance']:.3f} | {worst['name']} |")
        slug = family.lower().replace('+','_').replace('-','_')
        table = []
        for row in group:
            distance = '—' if row['spectral_js_distance'] is None else f"{row['spectral_js_distance']:.3f}"
            gain = '—' if row['relative_gain_change_db'] is None else f"{row['relative_gain_change_db']:+.2f}"
            early = '—' if row['early_spectral_js_distance'] is None else f"{row['early_spectral_js_distance']:.3f}"
            table.append('<tr><td>'+html.escape(row['feature'])+'</td><td>'+distance+' / '+early+'</td><td>'+gain+'</td><td>'+html.escape(row['status'])+'</td><td><audio controls preload="none" src="'+row['name']+'-ab.wav"></audio><details><summary>Parámetros y medidas</summary><pre>'+html.escape(json.dumps({'params':row['params'],'note':row['note'],'reference':row['reference'],'raw':row['raw'],'early_reference':row['temporal_reference'][0],'early_raw':row['temporal_raw'][0]},indent=2))+'</pre></details></td></tr>')
        sections.append('<section id="'+slug+'"><h2>'+html.escape(family)+'</h2><img src="'+slug+'-detail.png" alt="Espectro y evolución temporal"><table><tr><th>Prueba</th><th>Distancia estable / inicio</th><th>Respuesta ΔdB</th><th>Audibilidad</th><th>A/B: original, nuestro OSC</th></tr>'+''.join(table)+'</table></section>')
    lines += ['', '## Cambios de audibilidad en la ventana estable', '', 'Una consonante breve puede sonar al principio y estar bajo umbral después. Estas etiquetas no significan que el WAV completo sea silencioso.', '']
    mismatches = [row for row in rows if row['reference']['audible'] != row['raw']['audible']]
    lines += [f"- {row['name']}: {row['status']} ({row['feature']})." for row in mismatches] or ['Ningún caso con audibilidad unilateral al umbral de -90 dBFS AC.']
    lines += ['', '## Estéreo ENS', '']
    for row in rows:
        if row['family']=='ENS' and 'chorus' in row['name']:
            lines.append(f"- {row['feature']}: correlación L/R original {row['reference']['stereo']['correlation']:.4f}; nuestro generador {row['raw']['stereo']['correlation']:.4f}.")
    lines += ['', '## Alcance', '',
              'SAW/PULSE/VO usan Digi Mono; ENS, SID y FM+ usan nuestro port MONO2K. Los hashes registran la versión medida de cada generador. Las notas y los parámetros de cada caso se muestran en el JSON; esto no valida todo el teclado ni todas las combinaciones.', '',
              'Los casos de ocho segundos de ENS permiten observar movimiento lento de chorus. VO incluye ventanas tempranas para consonantes/transiciones. No hay note-off, portamento, PRCH entre pistas ni locks/LFO externos; la modulación interna sí funciona.', '',
              'A/B: solo los archivos de escucha están igualados en RMS AC y resampleados a 48 kHz. Si una ventana estable es silenciosa, se usa RMS del clip completo para ambos lados y se conservan los transitorios. Los WAV reference/raw mantienen sus tasas y ganancias nativas. El caso totalmente silencioso no se amplifica. La comparación espectral promedia potencia L/R sin cancelar el contenido fuera de fase.', '',
              '[Informe con audios](report.html) · [Resultados y procedencia](results.json) · [Vista general](overview.png)', '']
    (out/'report.md').write_text('\n'.join(lines),encoding='utf-8')
    document = '''<!doctype html><html lang="es"><meta charset="utf-8"><title>Máquinas complejas · Monomachine</title>
<style>body{font:16px system-ui;max-width:1260px;margin:30px auto;padding:0 20px;color:#18202b;background:#fafbfc}table{border-collapse:collapse;width:100%}td,th{padding:9px;border-bottom:1px solid #ddd;text-align:left}img{width:100%}.note{background:#e9eef5;padding:18px;line-height:1.5}audio{width:270px}pre{font-size:12px;max-width:500px;white-space:pre-wrap}section{margin:45px 0}nav a{margin-right:15px}</style>
<h1>Máquinas complejas · OS 1.32B</h1><p class="note">DSP original con cadena neutralizada frente a generadores brutos del banco. La distancia espectral es un diagnóstico, no un porcentaje de fidelidad. Niveles absolutos y estéreo corresponden a etapas diferentes; no hay prueba con hardware físico. A/B: primero original y después nuestro motor, igualados en RMS.</p>
<nav>'''+''.join('<a href="#'+f.lower().replace('+','_').replace('-','_')+'">'+html.escape(f)+'</a>' for f in families)+'''</nav><img src="overview.png" alt="Distancias de los casos por motor"><p><a href="report.md">Método, alcance y resumen</a> · <a href="results.json">Resultados completos</a></p>'''+''.join(sections)+'</html>'
    (out/'report.html').write_text(document,encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--os',type=Path,required=True)
    parser.add_argument('--reference',type=Path,required=True)
    parser.add_argument('--cc',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--captures',type=Path,help='reanalyze verified captures with identical controls/OS/engine sources')
    parser.add_argument('--suite',choices=('complex','ens','ens-shape'),default='complex')
    parser.add_argument('--reference-dist',type=int,choices=range(128),default=64,
                        help='original AMP DIST; lower values test extra internal headroom')
    args = parser.parse_args()
    build = json.loads((args.reference.resolve().parent/'reference-build.json').read_text())
    for name,digest in build['driver_sha256'].items():
        if common.sha(HERE/name)!=digest:
            raise RuntimeError(f'rebuild the reference: {name}')
    out = args.out.resolve();out.mkdir(parents=True,exist_ok=False)
    cached = None
    if args.captures:
        cached=json.loads((args.captures/'results.json').read_text())
        assert cached['os_sha256']==common.sha(args.os)
        assert cached['reference_executable_sha256']==common.sha(args.reference)
        # Analysis may change, but all native engine and driver inputs must
        # remain byte-identical before old audio can be reused.
        for name,digest in cached['source_sha256'].items():
            if Path(name).suffix in ('.c','.h','.inc','.cpp','.cmake','.patch') or name.startswith('mods/'):
                assert common.sha(ROOT/name)==digest,name
        cached_rows={row['name']:row for row in cached['cases']}
        lib=None
    else:
        lib,_ = common.host_engine(args.cc,out)
    fixtures={'complex':cases,'ens':ens_cases,'ens-shape':ens_shape_cases}[args.suite]()
    result = {'schema':1,'suite':args.suite,'scope':'original neutral track chain vs raw bank OSC',
              'reference_build':build,'os_sha256':common.sha(args.os),
              'reference_executable_sha256':common.sha(args.reference),
              'quiet_threshold_dbfs_ac':QUIET_DBFS,'band_edges_hz':BAND_EDGES.tolist(),
              'analysis_versions':{'numpy':np.__version__,'scipy':__import__('scipy').__version__},
              'source_sha256':{p.relative_to(ROOT).as_posix():common.sha(p) for p in
                              [*sorted(p for p in HERE.glob('*') if p.suffix in ('.py','.c','.cpp','.h','.cmake','.json','.patch')),
                               *sorted(p for p in (ROOT/'mods/digimono-st').rglob('*') if p.is_file() and p.suffix in ('.c','.h','.inc','.json'))]},
              'reference_chain':{'amp':[0,127,127,0,args.reference_dist,64,64,0],'filt':[0,127,0,0,0,0,64,64],
                                 'efx':[64,64,0,64,64,0,0,127],'level':100,'warmup_blocks':8},
              'cases':[]}
    if cached is not None:
        assert cached['reference_chain']==result['reference_chain'],'reference chain differs'
        assert set(cached_rows)=={case['name'] for case in fixtures},'capture fixture set differs'
        result['reused_captures_manifest_sha256']=common.sha(args.captures/'results.json')
    spectra, anchors = {}, {}
    for case in fixtures:
        name=case['name'];refpath=out/(name+'-reference.wav');rawpath=out/(name+'-raw.wav')
        events=[(round(time*44100),note) for time,note in case['events']]
        assert all(frame%16==0 for frame,_ in events)
        command=[str(args.reference.resolve()),'--fw',str(args.os.resolve()),'--out',str(refpath),
                 '--frames',str(round(case['duration_s']*44100)),'--machine',str(case['original_machine']),
                 '--dist',str(args.reference_dist),
                 '--syn',','.join(map(str,case['params']+[case['tune']])),
                 '--events',','.join(f'{frame}:{note}' for frame,note in events)]
        if cached is None:
            with (out/(name+'-reference.log')).open('w',encoding='utf-8') as log:
                subprocess.run(command,check=True,stdout=log,stderr=log,text=True,timeout=90)
        else:
            old=cached_rows[name]
            for key,value in json.loads(json.dumps(case)).items():
                assert old[key]==value,(name,key)
            for suffix,key in [('-reference.wav','reference_sha256'),('-raw.wav','raw_sha256')]:
                src=args.captures/(name+suffix)
                assert common.sha(src)==old[key],str(src)
                shutil.copy2(src,out/src.name)
        rate,reference=common.load_wav(refpath)
        assert rate==44100 and len(reference)==round(case['duration_s']*rate)
        if cached is None:
            raw=raw_render(lib,case);common.wavfile.write(rawpath,48000,raw)
        else:
            raw_rate,raw=common.load_wav(rawpath);assert raw_rate==48000
            if raw.shape[1]==1:raw=raw[:,0]
        row,spectra[name]=score_case(case,reference,raw)
        row['reference_sha256'],row['raw_sha256']=common.sha(refpath),common.sha(rawpath)
        anchor=case.get('anchor_group',case['family'])
        if anchor not in anchors:
            anchors[anchor]=row
        row['anchor']=anchors[anchor]['name']
        row['relative_gain_change_db']=relative_gain_change(row,anchors[anchor])
        result['cases'].append(row)
        row['ab_normalization']=listen_ab(reference,raw,out/(name+'-ab.wav'),row['analysis_window_s'])
        with (out/'measurements.jsonl').open('a',encoding='utf-8') as log:
            log.write(json.dumps(row,allow_nan=False)+'\n')
        distance='—' if row['spectral_js_distance'] is None else f"{row['spectral_js_distance']:.3f}"
        print(f"{len(result['cases']):02}: {name}: spectral {distance}; {row['status']}",flush=True)
    plot_report(result,spectra,out)
    (out/'results.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n',encoding='utf-8')
    write_report(result,out)
    print(f"Created {len(result['cases'])} captures in {len(anchors)} anchor groups, WAV A/B, plots and report; no device access.")


if __name__=='__main__':
    main()
