# SPDX-License-Identifier: AGPL-3.0-or-later
"""FM+ balanced/HQ linked callback cost; intentional DSP changes are not called exact."""
import argparse,hashlib,json,random
from pathlib import Path
from elekloader.syx import Syx
from test_st_fmplus_optim_emu import load
from test_st_digimono_emu import PATCHES


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('before','after','stock','out'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True);rnd=random.Random(0x700)
    stock=Syx.load(a.stock).section(7);rows=[]
    for kind in (9,10,11):
        for scene in ('default','zero','max','modulated'):
            ports=[load(bank,stock)[0] for bank in (a.before,a.after)];peaks=[0,0];totals=[0,0]
            for frame in range(64):
                knobs=[v<<8 for v in PATCHES[kind]]
                if scene=='zero':knobs[1:]=[0]*7
                elif scene=='max':knobs[1:]=[0x7f00]*7
                elif scene=='modulated':knobs[1:]=[rnd.randrange(0x7f01) for _ in range(7)]
                for i,port in enumerate(ports):
                    pcm,cost=port.block(0,kind,knobs,(48,60,96,24)[frame//16],frame%17==0)
                    peaks[i]=max(peaks[i],cost[1]);totals[i]+=cost[1]
            assert totals[1]<.85*totals[0],(kind,scene,totals)
            rows.append(dict(type=kind,scene=scene,blocks=64,render_max=peaks,render_total=totals))
            print('PASS:',kind,scene,'render maxima',peaks,flush=True)
    ratio=sum(r['render_total'][1] for r in rows)/sum(r['render_total'][0] for r in rows)
    assert ratio<.8,ratio
    result=dict(ok=True,blocks=768,rows=rows,aggregate_ratio=ratio,pcm_identity_expected=False,
                before_sha256=hashlib.sha256(a.before.read_bytes()).hexdigest(),
                after_sha256=hashlib.sha256(a.after.read_bytes()).hexdigest(),hardware_cycles=False)
    (a.out/'results.json').write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':main()
