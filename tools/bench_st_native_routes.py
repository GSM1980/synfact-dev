"""Count all-interested native dispatch before/after, including its routing guard.

Subscriber bodies are fixed callback boundaries; only the real core dispatcher
instructions are counted. Test both full-list and consuming paths. No device I/O.
"""
import argparse,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
from test_st_native_routes_emu import Routes,LIST,ARGS,RESULT,CALLBACK,pack

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('before','after','out'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();rows=[]
    for count in (0,1,2,3,4,7,8,40):
        entries=[CALLBACK+32*(i%16) for i in range(count)]
        for take in ('none','first','last'):
            costs=[]
            for folder in (a.before,a.after):
                m=Routes(folder);m.operation=17
                m.consume=(entries[0] if take=='first' else entries[-1] if take=='last' else None) if entries else None
                m.u.mem_write(LIST,pack(*entries,0))
                m.w(m.sym.get('core:core_native_list',m.sym['core:events']+88),LIST)
                m.u.mem_write(m.sym['core:core_native_interest'],bytes([128])*24)
                m.instructions=0;ret=m.invoke('core_event_native',17,ARGS,RESULT)
                want=entries[:entries.index(m.consume)+1] if m.consume else entries
                assert m.hits==want and ret==int(bool(m.consume))
                costs.append(m.instructions)
            rows.append(dict(subscribers=count,consume=take,before=costs[0],after=costs[1]))
    assert all(r['after']<=r['before'] for r in rows),rows
    a.out.write_text(json.dumps(dict(ok=True,rows=rows,device_access=False,
        limits=['Subscriber bodies are boundaries. Counts are dispatcher instructions, not cycles.']),indent=2)+'\n')
    print(json.dumps(rows,indent=1))
if __name__=='__main__':main()
