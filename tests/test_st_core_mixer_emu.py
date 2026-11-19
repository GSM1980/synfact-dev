"""Compare linked mixer bridges with stock and the previous core, no device I/O.

Reuse the independent register/stack/table/callback/EMAC-state oracle. Event-list
operands and the send table use test addresses; handler operations are unchanged.
Unchanged native MAC arithmetic is comparative, not a physical arithmetic oracle.
"""
import argparse
import hashlib
import json
from pathlib import Path
import random
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
from test_st_chorus_emu import STOCK,STUB_BASE,STUB_LIST,SUB_A,SUB_B,stub_case


def image(folder):
    report=json.loads((folder/'verification.json').read_text())
    symbols={k.removeprefix('core:'):v for k,v in report['symbols'].items() if k.startswith('core:')}
    lo,hi=report['link']['ddr'];assert lo==STUB_BASE
    at=report['link']['run_load']-0x40000400
    code=bytearray((folder/'main.bin').read_bytes()[at:at+hi-lo])
    for name in ('track_send','delay_in','reverb_in'):
        operand=symbols['core_'+name+'_table']+2-lo
        assert code[operand-2:operand]==bytes.fromhex('41f9')
        struct.pack_into('>I',code,operand,STUB_LIST)
    # The oracle observes the table at a fixed test address. Rebind its direct
    # references without altering code/EMAC or the event-list operands above.
    from test_st_chorus_emu import TABLE
    table=symbols['core_track_sends']
    first=symbols['core_track_send']-lo;end=symbols['core_delay_in']-lo
    for delta in (0,192):
        old=struct.pack('>I',table+delta);new=struct.pack('>I',TABLE+delta)
        count=2 if delta==0 else 1          # LEA/table argument, or mute store
        assert code[first:end].count(old)==count,(delta,code[first:end].count(old))
        positions=[pos for pos in range(first,end-3) if code[pos:pos+4]==old]
        assert len(positions)==count
        for pos in positions:code[pos:pos+4]=new
    return bytes(code),symbols


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('before','after','out'):p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();data=[image(x) for x in (a.before,a.after)];rows=[]
    for kind in STOCK:
        for subscribers in ([],[SUB_A],[SUB_A,SUB_B]):
            counts=[]
            for code,symbols in data:
                rng=random.Random(0x54415)
                counts.append([stub_case(code,symbols,kind,subscribers,rng) for _ in range(40)])
            for old,new in zip(*counts):
                assert (old['track'],old['subscribers'])==(new['track'],new['subscribers'])
                assert new['instructions_including_emac_fixture']<=old['instructions_including_emac_fixture'],(kind,old,new)
            rows.append(dict(kind=kind,subscribers=len(subscribers),cases=40,
                             before=[x['instructions_including_emac_fixture'] for x in counts[0]],
                             after=[x['instructions_including_emac_fixture'] for x in counts[1]]))
    sha=lambda path:hashlib.sha256((path/'main.bin').read_bytes()).hexdigest()
    report=dict(ok=True,cases=360,rows=rows,exact_stock_register_stack_table_callbacks_and_emac=True,
                all_counts_nonincreasing=True,core_sha256=dict(before=sha(a.before),after=sha(a.after)),
                device_access=False,limits=['Event-list and table addresses are fixture rebindings only.',
                    'Native arithmetic is unchanged and compared on both sides, not a physical arithmetic oracle.',
                    'Counts include fixed EMAC setup/readback instructions, not hardware cycles.'])
    a.out.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: 360 linked mixer cases; stock registers/stack/table/callbacks/EMAC; no count increases')
    for row in rows:
        print(row['kind'],row['subscribers'],row['before'][0],'->',row['after'][0])


if __name__=='__main__':main()
