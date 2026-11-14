"""Cross-core POLY events: exact states/pools, warm count gates, cold setup report.

Subscription changes intentionally prepare routes once. Configuration costs
are reported separately; every subsequent key/sequence operation must be
non-increasing. Existing same-core bank performance tests remain unchanged.
"""
import argparse,hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from bench_st_poly_events import Poly,state

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('before-core','after-core','bank','out'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();rows=[]
    for track,voices in ((0,2),(0,3),(0,4),(5,3),(8,2),(8,3),(9,2)):
        for independent in ((0,1) if track<8 else (0,)):
            ms=[Poly(core,a.bank) for core in (a.before_core,a.after_core)]
            pools=[m.pool_counts() for m in ms]
            cold=[m.config(track,voices,independent) for m in ms]
            assert state(ms[0])==state(ms[1]),('configure',track,voices,independent)
            warm=[]
            for on,notes in ((1,(60,64,67,72,60,55)),(2,(60,55,72,67,64))):
                for note in notes:
                    costs=[m.key_event(note,on,track) for m in ms]
                    assert state(ms[0])==state(ms[1]),('key',track,voices,on,note)
                    assert costs[1]<=costs[0],('key count',track,voices,on,note,costs)
                    warm.append(dict(kind='key',note=note,on=on,before=costs[0],after=costs[1]))
            for step,note in enumerate((60,62,65,72)):
                costs=[m.sequence_event(track,note,step,28800*(step+1)) for m in ms]
                assert state(ms[0])==state(ms[1]),('sequence',track,voices,step)
                assert costs[1]<=costs[0],('sequence count',track,voices,step,costs)
                warm.append(dict(kind='sequence',note=note,before=costs[0],after=costs[1]))
            for m,initial in zip(ms,pools):
                m.config(track,voices,independent,enabled=0);m.drain(200000,228800)
                assert m.pool_counts()==initial,('unbalanced pools',track,voices)
            assert state(ms[0])==state(ms[1])
            rows.append(dict(track=track,voices=voices,independent=independent,
                             cold_configure=dict(before=cold[0],after=cold[1]),warm=warm))
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    report=dict(ok=True,scenarios=rows,before_core_sha256=sha(a.before_core),
                after_core_sha256=sha(a.after_core),bank_sha256=sha(a.bank),device_access=False,
                limits=['Configuration includes one-time route preparation; warm operations have non-increasing gates.',
                        'Native Sound publication/synthesis and parent-origin return PC are explicit fixture boundaries.',
                        'Instructions, not cycles or physical MAIN percentages.'])
    a.out.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS cross-core POLY:',len(rows),'configurations;',sum(len(r['warm']) for r in rows),
          'warm operations; exact state and balanced pools')
if __name__=='__main__':main()
