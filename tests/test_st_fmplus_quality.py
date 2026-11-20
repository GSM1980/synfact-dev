# SPDX-License-Identifier: AGPL-3.0-or-later
"""Balanced FM+ tuning/level/spectrum versus retained HQ; requires numpy/scipy."""
import argparse,ctypes,hashlib,json,math,subprocess,wave
from pathlib import Path
import numpy as np
from scipy.signal import welch
from scipy.spatial.distance import jensenshannon

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',type=Path,required=True)
    p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    src=ROOT/'mods/digimono-st/fmplus.c';digest=hashlib.sha256(src.read_bytes()).hexdigest()
    wrapper=a.out/'size.c';wrapper.write_text('#include "fmplus/voice.h"\nunsigned voice_bytes(void){return sizeof(struct dm_fm_voice);}\n')
    libs={}
    for name,extra in (('hq',['-DDM_FM_HQ']),('balanced',[])):
        dll=(a.out/(name+'.dll')).resolve()
        subprocess.run([str(a.cc),'-O3','-shared','-I','mods/digimono-st',*extra,str(src),str(wrapper),'-o',str(dll)],cwd=ROOT,check=True)
        lib=ctypes.CDLL(str(dll));lib.voice_bytes.restype=ctypes.c_uint
        lib.dm_fm_init.argtypes=[ctypes.c_void_p];lib.dm_fm_trig.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p]
        lib.dm_fm_half.argtypes=[ctypes.c_void_p,ctypes.c_uint,ctypes.c_void_p,ctypes.c_int32,ctypes.c_void_p];libs[name]=lib
    def render(lib,kind,knobs,note):
        state=ctypes.create_string_buffer(lib.voice_bytes());par=(ctypes.c_uint16*7)(*(v<<8 for v in knobs));pcm=(ctypes.c_int32*16)()
        lib.dm_fm_init(state);lib.dm_fm_trig(state,kind,par);result=[]
        for _ in range(2048):lib.dm_fm_half(state,kind,par,note*64,pcm);result.extend(pcm)
        x=np.array(result,dtype=np.float64)/2**29
        assert np.isfinite(x).all() and np.max(np.abs(x))<.51
        return x
    def frequency(x):
        i=np.where((x[:-1]<=0)&(x[1:]>0))[0];cross=i-x[i]/(x[i+1]-x[i])
        return 44100/np.median(np.diff(cross))
    silent={9:[61,64,0,0,64,0,98],10:[61,0,64,0,64,0,98],11:[64,64,0,64,64,0,0]}
    cases=[]
    for kind in (9,10,11):
        for note in (36,48,60,69,84,96):cases.append((kind,'tuning',note,silent[kind]))
        for note in (48,60,72):
            knobs=list(silent[kind])
            for k in ({9:(2,5),10:(1,3,5),11:(2,5)}[kind]):knobs[k]=24
            cases.append((kind,'low-depth',note,knobs))
    defaults={9:[61,64,80,32,82,64,64],10:[61,64,82,64,104,80,64],11:[64,64,64,0,74,80,32]}
    for kind in (9,10,11):
        cases.append((kind,'default',60,defaults[kind]))
        for note in (84,96):cases.append((kind,'high-note',note,defaults[kind]))
        for value in (32,48,64,127):
            knobs=list(defaults[kind]);knobs[{9:3,10:1,11:6}[kind]]=value
            cases.append((kind,'feedback-'+str(value),60,knobs))
    rows=[]
    for kind,label,note,knobs in cases:
        signals={name:render(lib,kind,knobs,note) for name,lib in libs.items()};h,b=(signals[n][8192:] for n in ('hq','balanced'))
        power=[welch(x,fs=44100,nperseg=2048,noverlap=1024)[1] for x in (h,b)]
        level=20*math.log10(max(np.sqrt(np.mean(b*b)),1e-20)/max(np.sqrt(np.mean(h*h)),1e-20))
        distance=float(jensenshannon(power[0],power[1],base=2));row=dict(type=kind,case=label,note=note,knobs=knobs,level_db=level,spectral_js_distance=distance)
        if label=='tuning':
            row['tuning_delta_cents']=1200*math.log2(frequency(b)/frequency(h))
            assert abs(row['tuning_delta_cents'])<.5 and abs(level)<.5,row
        elif label=='low-depth':assert distance<.2 and abs(level)<2.5,row
        elif label=='high-note':assert abs(level)<6,row
        else:assert abs(level)<3,row
        if label=='default':
            # Same gain for both clips: HQ, silence, balanced. No per-clip normalization.
            gain=.9/max(np.max(np.abs(x)) for x in signals.values())
            audio=np.concatenate((signals['hq'],np.zeros(4410),signals['balanced']))
            path=a.out/f'type-{kind}-hq-then-balanced.wav'
            with wave.open(str(path),'wb') as w:w.setparams((1,2,44100,0,'NONE','not compressed'));w.writeframes(np.round(audio*gain*32767).astype('<i2').tobytes())
        rows.append(row)
    assert digest==hashlib.sha256(src.read_bytes()).hexdigest(),'source changed during comparison'
    (a.out/'results.json').write_text(json.dumps(dict(ok=True,cases=len(rows),rows=rows,source_sha256=digest,
        scope='raw native-rate oscillator: HQ versus balanced, not original-device identity',hardware_tested=False),indent=2)+'\n')
    print('PASS:',len(rows),'balanced/HQ tuning, level and low-depth spectral cases; high-feedback spectra reported separately')


if __name__=='__main__':main()
