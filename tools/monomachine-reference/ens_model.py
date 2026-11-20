#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Infer ENS's PW and saw blend from same-note harmonic ratios.

The shared track transfer cancels harmonic by harmonic. Only chorus-off,
single-voice captures are used. This estimates control laws, not global gain.
"""
import argparse,json
from pathlib import Path
import numpy as np
from scipy.optimize import least_squares
import compare as common


def fit_width(ratios, harmonics):
    ratios=np.asarray(ratios,float);k=np.asarray(harmonics,float)
    if not np.all(ratios>0) or np.any(k%2!=1):raise ValueError('positive odd-harmonic ratios required')
    def error(p):return np.log(np.maximum(abs(np.sin(np.pi*k*p[0])),1e-12))-np.log(ratios)
    grid=np.linspace(.00001,.49999,512)
    seed=min(grid,key=lambda d:np.linalg.norm(error([d])))
    fit=least_squares(error,[seed],bounds=([.000001],[.5]))
    return dict(duty=float(fit.x[0]),log_rms_error=float(np.sqrt(np.mean(fit.fun**2))))


def fit_mix(ratios,harmonics,duty):
    ratios=np.asarray(ratios,float);k=np.asarray(harmonics,float)
    if not np.all(ratios>0):raise ValueError('positive harmonic ratios required')
    def error(p):
        model=p[1]*np.sqrt(np.maximum(1+p[0]**2+2*p[0]*np.cos(2*np.pi*k*duty),1e-20))/(2*abs(np.sin(np.pi*k*duty)))
        return np.log(np.maximum(model,1e-12))-np.log(ratios)
    fit=least_squares(error,[.2,1],bounds=([-1,.01],[1,100]))
    return dict(b_over_a=float(fit.x[0]),scale=float(fit.x[1]),log_rms_error=float(np.sqrt(np.mean(fit.fun**2))))


def harmonics(values,rate,f0):
    y=np.asarray(values[round(.3*rate):round(1.8*rate),0]);t=np.arange(len(y))/rate
    cols=[np.ones(len(y))]
    for k in range(1,13):cols.extend((np.sin(2*np.pi*k*f0*t),np.cos(2*np.pi*k*f0*t)))
    coef=np.linalg.lstsq(np.column_stack(cols),y,rcond=None)[0]
    return np.hypot(coef[1::2],coef[2::2])


def measure(folder):
    manifest=json.loads((folder/'results.json').read_text());rows={r['name']:r for r in manifest['cases']}
    used={};result=[]
    def load(name):
        row=rows[name];path=folder/(name+'-reference.wav')
        assert row['params'][:3]==[63]*3 and row['params'][5]==0
        assert common.sha(path)==row['reference_sha256']
        used[path.name]=common.sha(path)
        return common.load_wav(path)
    for note in (36,48,60,72,84):
        rate,base=load(f'ens_n{note}_base');expected=440*2**((note-69)/12)
        f0=common.fit_tone(base[:,0],rate,expected,start=.3,end=1.8)['frequency_hz']/2
        amps={}
        for wave,pw in ((0,32),(64,32),*((127,pw) for pw in (0,16,32,64,96,112,127))):
            rate,y=load(f'ens_n{note}_w{wave}_pw{pw}');amps[wave,pw]=harmonics(y,rate,f0)
        odd=np.arange(1,13,2)
        widths={pw:fit_width(amps[127,pw][odd-1]/amps[127,64][odd-1],odd) for pw in (0,16,32,96,112,127)}
        d=widths[32]['duty'];k=np.arange(1,13)
        mixtures={wave:fit_mix(amps[wave,32]/amps[127,32],k,d) for wave in (0,64)}
        a=mixtures[64]['scale']/mixtures[0]['scale'];b=a*mixtures[64]['b_over_a']
        item=dict(note=note,pair_frequency_hz=f0,widths=widths,mix=mixtures,
                  inferred=dict(pulse_weight=1/mixtures[0]['scale'],wave64_a=a,wave64_b=b,
                                a_slope=(1-a)/(64/127),b_slope=(1-b)/(64/127)))
        result.append(item)
        print('MIDI',note,': PW0/16/32/127',*[round(widths[p]['duty'],6) for p in (0,16,32,127)],
              '; WAVE slopes',round(item['inferred']['a_slope'],5),round(item['inferred']['b_slope'],5),flush=True)
    return dict(schema=1,input_manifest_sha256=common.sha(folder/'results.json'),
                tool_sha256=common.sha(__file__),reference_wav_sha256=used,notes=result,
                assumptions=['Same-note shared linear track response cancels in harmonic ratios.',
                             'PW64 pulse is the half-period anchor; pulse amplitude stays constant across PW.',
                             'The two saws share a waveform; blend is estimated relative to the WAVE127 pulse.',
                             'Low-level/narrow pulses have less reliable fits; no global output gain is inferred.'])


def validate(grid,holdout):
    """Compare held-out control responses; never equalize the raw oscillator."""
    a=json.loads((grid/'results.json').read_text());b=json.loads((holdout/'results.json').read_text())
    for key in ('os_sha256','reference_executable_sha256','reference_chain'):
        assert a[key]==b[key],key
    rows={r['name']:r for r in a['cases']};used={};cache={};out=[]
    fitted={(r['note'],tuple(r['params'])) for r in a['cases']}
    def get(folder,row,f0):
        path=folder/(row['name']+'-reference.wav');assert common.sha(path)==row['reference_sha256']
        assert row['params'][:3]==[63]*3 and row['params'][5]==0
        used[folder.name+'/'+path.name]=common.sha(path)
        rate,y=common.load_wav(path)
        return harmonics(y,rate,f0)
    for row in b['cases']:
        # A fresh render of a training setting is repeatability, not holdout.
        if (row['note'],tuple(row['params'])) in fitted:continue
        note=row['note'];wave,pw=row['params'][3:5]
        if note not in cache:
            base=rows[f'ens_n{note}_base'];path=grid/(base['name']+'-reference.wav')
            assert common.sha(path)==base['reference_sha256']
            rate,y=common.load_wav(path)
            f0=common.fit_tone(y[:,0],rate,440*2**((note-69)/12),start=.3,end=1.8)['frequency_hz']/2
            cache[note]=(f0,get(grid,rows[f'ens_n{note}_w0_pw32'],f0),get(grid,rows[f'ens_n{note}_w127_pw64'],f0))
        f0,saw,pulse=cache[note]
        if pw==64:continue
        h=get(holdout,row,f0)
        if pw==32:
            k=np.arange(1,13);d=.255
            ratio=h/saw;x=wave/127
            denominator=abs(1+np.exp(2j*np.pi*k*d))
            def blend(aa,bb):return abs((1-aa*x)+(1-bb*x)*np.exp(2j*np.pi*k*d))/denominator
            old,new=blend(.37,1.63),blend(.30,1.70)
            mode='WAVE response, fixed measured duty'
        else:
            k=np.arange(1,13,2);ratio=h[k-1]/pulse[k-1]
            old=abs(np.sin(np.pi*k*pw/128));new=abs(np.sin(np.pi*k*(.01+.98*pw/128)))
            mode='PW response at WAVE127'
        def error(model):return float(np.sqrt(np.mean((20*np.log10(np.maximum(model,1e-10)/ratio))**2)))
        out.append(dict(name=row['name'],mode=mode,old_harmonic_response_rms_db=error(old),new_harmonic_response_rms_db=error(new)))
    return dict(grid_manifest_sha256=common.sha(grid/'results.json'),holdout_manifest_sha256=common.sha(holdout/'results.json'),
                reference_wav_sha256=used,cases=out,
                mean_old_rms_db=float(np.mean([r['old_harmonic_response_rms_db'] for r in out])),
                mean_new_rms_db=float(np.mean([r['new_harmonic_response_rms_db'] for r in out])))


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('captures',type=Path);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--validation-captures',type=Path)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    result=measure(a.captures)
    if a.validation_captures:
        result['validation']=validate(a.captures,a.validation_captures)
        print('Held-out harmonic response RMS dB:',result['validation']['mean_old_rms_db'],'->',result['validation']['mean_new_rms_db'])
    (a.out/'model.json').write_text(json.dumps(result,indent=2)+'\n')
