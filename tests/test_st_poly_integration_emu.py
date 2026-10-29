"""Retain the verified analog native kernels and improve their publication.

Uses the same linked fixtures as the original analog cost audit. Compare the
three supported analog layouts against its verified bank, without confusing
instruction counts with hardware cycles. No device access.
"""
import argparse,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_poly_cost_emu import Runtime
from elekloader import formats

def main():
    p=argparse.ArgumentParser()
    for name in ('before','after','stock','out'):p.add_argument('--'+name,required=True,type=Path)
    a=p.parse_args();stock,dev,release=formats.load(a.stock)
    assert stock.sha256=='8e2488f462c4a5656396a895f113bcd415e9900fa8709340dccf45d4cb9ed19e'
    old=Runtime(a.before,stock.section(3));new=Runtime(a.after,stock.section(3));rows=[]
    for master,voices in ((8,2),(8,3),(9,2)):
        groups=[(master,voices)]
        before=old.kernels(groups);after=new.kernels(groups)
        assert before==after,('analog native engines changed',groups,before,after)
        pub=[old.publication(groups),new.publication(groups)]
        dispatch=[old.publish_dispatch(groups),new.publish_dispatch(groups)]
        assert pub[1]<=pub[0],('analog publication regression',groups,pub)
        assert dispatch[1]<=dispatch[0],('analog dispatch regression',groups,dispatch)
        guards={}
        for name,n in (('poly_mod_first_guard',12),('poly_mod_second_guard',13),('poly_native_lfo_guard',13)):
            counts=[]
            for r in (old,new):
                r.topology(groups);counts.append(sum(r.guard(name,t) for t in range(n)))
            assert counts[0]==counts[1],('analog guard changed',name,counts)
            guards[name]=counts
        rows.append(dict(master=master+1,voices=voices,native_before=before,native_after=after,
                         publication=pub,publish_dispatch=dispatch,guards=guards))
    a.out.write_text(json.dumps(dict(ok=True,cases=rows,device_access=False,
        units='guest instructions; boundaries doubled; not cycles'),indent=2)+'\n')
    print('PASS: all three analog layouts retain native/guard counts and do not increase publication cost')
    print(json.dumps(rows,indent=2))
if __name__=='__main__':main()
