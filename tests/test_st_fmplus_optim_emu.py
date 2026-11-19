# SPDX-License-Identifier: AGPL-3.0-or-later
"""Old/new linked FM+ renders: exact PCM/state and complete-call instruction counts."""
import argparse,hashlib,json,random,struct
from pathlib import Path
from elekloader.sdk.elf import Elf
from elekloader.syx import Syx
from test_st_digimono_emu import Port,PATCHES


def load(bank,stock):
    raw=bank.read_bytes();report=json.loads(Path(str(bank)+'.json').read_text())
    assert hashlib.sha256(raw).hexdigest()==report['sha256']
    e=Elf.load(bank.with_suffix('.work')/'audio.elf');sec=e.section('.image')
    at=struct.unpack_from('>I',e.raw,struct.unpack_from('>I',e.raw,32)[0]+sec.idx*40+12)[0]
    off=48+struct.unpack_from('>I',raw,72)[0]+at-0x4204f750
    assert raw[off:off+len(sec.data)]==sec.data
    symbols={s.name:s.value for s in e.symbols if s.name}
    state=next(s for s in e.symbols if s.name=='dm_tracks')
    return Port(stock,sec.data,at,symbols),state.value,state.size


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('before','after','stock','out'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    stock=Syx.load(a.stock).section(7);rows=[];rnd=random.Random(0x56300)
    for kind in (9,10,11):
        for scene in ('default','zero','max','modulated'):
            b,ba,bn=load(a.before,stock);c,ca,cn=load(a.after,stock);assert bn==cn
            peak=[0,0];totals=[0,0]
            for frame in range(64):
                controls=[v<<8 for v in PATCHES[kind]]
                if scene=='zero':controls[1:]=[0]*7
                elif scene=='max':controls[1:]=[0x7f00]*7
                elif scene=='modulated':controls[1:]=[rnd.randrange(0x7f01) for _ in range(7)]
                note=(48,60,96,24)[(frame//16)%4];trigger=frame%17==0
                old,bc=b.block(0,kind,controls,note,trigger)
                new,cc=c.block(0,kind,controls,note,trigger)
                assert new==old,(kind,scene,frame,'PCM')
                assert bytes(b.uc.mem_read(ba,bn))==bytes(c.uc.mem_read(ca,cn)),(kind,scene,frame,'state')
                # Every scenario must improve in aggregate; individual frame
                # boundaries have a different 2/3-half-block cadence.
                for i,n in enumerate((bc[1],cc[1])):peak[i]=max(peak[i],n);totals[i]+=n
            assert totals[1]<totals[0],(kind,scene,totals)
            rows.append(dict(type=kind,scene=scene,blocks=64,render_max=peak,render_total=totals))
            print('PASS:',kind,scene,'exact PCM/state; max',peak,'total',totals,flush=True)
    result=dict(ok=True,blocks=768,rows=rows,before_sha256=hashlib.sha256(a.before.read_bytes()).hexdigest(),
                after_sha256=hashlib.sha256(a.after.read_bytes()).hexdigest(),hardware_cycles=False)
    (a.out/'results.json').write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':main()
