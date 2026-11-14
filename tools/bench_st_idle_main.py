"""Compare complete integer idle stages across core/bank pairs; no device I/O.

LFO3 rendering is an explicit disabled-render boundary in the native-dispatch
measurements. The separate cfbench LFO3 matrix executes actual physical EMAC.
Counts are instructions, never cycles or physical CPU percentages.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from bench_st_poly_events import Poly
from test_st_arp_generator_emu import W, LIST, KIT, SEQ
from test_st_poly_main_cost_emu import Main
from unicorn import UC_HOOK_CODE

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def machine(folder, bank):
    m = Poly(folder / 'main.bin', bank)
    m.call('poly_sync')
    names = ('lfo3_native', 'arp_native', 'poly_native')
    m.u.mem_write(LIST, struct.pack('>4I', *(m.sym[n] for n in names), 0))
    m.call(m.cs['core:core_native_interest_set'], m.sym['lfo3_native'],
           (1 << 17) | (1 << 9) | (1 << 10) | (1 << 11))
    m.call(m.cs['core:core_native_initialize'])
    m.u.mem_map(0x10000000, 0x10000)
    m.w(0x100005c4, 0x53545232)
    m.w(0x100005b0, 1)
    m.w(0x100007d0, 1)
    for t in range(12):
        m.u.mem_write(SEQ + t * 0x499 + 0x3c5, bytes([60, 64, 64, 64]))
    return m

def callbacks(m, descriptor, event):
    _, _, subs, count = struct.unpack('>4I', m.read(m.sym[descriptor], 16))
    return [fn for op, fn in (struct.unpack('>2I', m.read(subs + 8*i, 8)) for i in range(count))
            if op == event]

def idle(folder, bank):
    m = machine(folder, bank)
    stages = [('stereo_receive', callbacks(m, 'st_stereo_module', 5)),
              ('arp_frame', [m.sym['arp_frame']]), ('poly_frame', [m.sym['poly_frame']]),
              ('stereo_delay', callbacks(m, 'st_stereo_module', 13)),
              ('stereo_reverb', callbacks(m, 'st_stereo_module', 14)),
              ('stereo_main_direct', callbacks(m, 'st_stereo_module', 10)),
              ('recorder_mix', callbacks(m, 'recorder_module', 10))]
    rows = []
    for block in range(8):
        costs = {}
        for name, fns in stages:
            assert fns, name
            m.count = 0
            for fn in fns:
                m.call(fn)
            costs[name] = m.count
        assert m.read(0x8000dad0, 64*4) == bytes(64*4)
        rows.append(costs)
    return rows

def native(folder, bank):
    rows = []
    for op in (0, 3, 15, 16, 17, 20):
        m = machine(folder, bank)
        event = m.call(0x4011a4c2)
        data = [0]*20
        data[1:4] = [1, 0, 1]
        data[6], data[9], data[10], data[12] = 60, 0x10601, KIT+0x2a, 14400
        m.u.mem_write(event, struct.pack('>20I', *data))
        m.stock_trig_result = event
        args = W+0x300
        if op == 0:
            values = [0, SEQ, KIT, 0, event, W+0x380]
        elif op == 20:
            values = [event, KIT+0x2a, 28800, 57600]
        elif op == 17:
            values = [0x800021b0, 14400, 0]
        elif op == 3:
            values = [0x800021b0]
        else:
            values = [0]
        m.u.mem_write(args, struct.pack('>%dI' % len(values), *values))
        m.call(m.cs['core:core_native'], op, args)
        hits = dict(lfo3_native=0, arp_native=0, poly_native=0)
        addresses = {m.sym[n]:n for n in hits}
        def hook(u, at, size, user):
            if at in addresses:
                hits[addresses[at]] += 1
        h = m.u.hook_add(UC_HOOK_CODE, hook)
        m.count = 0
        result = m.call(m.cs['core:core_native'], op, args)
        count = m.count
        m.u.hook_del(h)
        m.call(0x4011a4f8, event)
        rows.append(dict(operation=op, instructions=count, result=result, hits=hits))
    return rows

def sender(folder, bank):
    rows = []
    for trigs in (0, 255):
        m = Main(folder, bank, 'off', False)
        m.u.mem_write(0x80002f50, bytes([4]*12))
        costs = []
        for block in range(8):
            m.stages = {}
            m.stage('sender', m.core['core:core_machine_snapshot'], trigs)
            costs.append(sum(m.stages['sender'].values()))
        assert m.u.mem_read(0x800021a9, 1) == b'\x00'
        rows.append(dict(trigs=trigs, instructions=costs))
    return rows

def main():
    p = argparse.ArgumentParser(description=__doc__)
    for n in ('before-core', 'after-core', 'before-bank', 'after-bank', 'out'):
        p.add_argument('--'+n, type=Path, required=True)
    a = p.parse_args()
    old = dict(idle=idle(a.before_core, a.before_bank), native=native(a.before_core, a.before_bank),
               sender=sender(a.before_core, a.before_bank))
    new = dict(idle=idle(a.after_core, a.after_bank), native=native(a.after_core, a.after_bank),
               sender=sender(a.after_core, a.after_bank))
    core_symbols=json.loads((a.after_core/'verification.json').read_text())['symbols']
    routed_dispatch=('core:native_routes' in core_symbols or 'core:core_native_route_fill' in core_symbols)
    for before, after in zip(old['idle'], new['idle']):
        for name in before:
            assert after[name] <= before[name], (name, before[name], after[name])
    for before, after in zip(old['native'], new['native']):
        assert after['result'] == before['result']
        assert after['instructions'] <= before['instructions'], (before, after)
        if after['operation'] in (17,20):
            expected=(dict(lfo3_native=1,arp_native=0,poly_native=0) if after['operation']==17
                      else dict(lfo3_native=0,arp_native=1,poly_native=0)) if routed_dispatch else dict(lfo3_native=1,arp_native=1,poly_native=1)
            assert after['hits']==expected
    for before, after in zip(old['sender'], new['sender']):
        assert all(y <= x for x, y in zip(before['instructions'], after['instructions']))
    result = dict(ok=True, before=old, after=new, routed_dispatch=routed_dispatch,
                  before_core_sha256=sha(a.before_core/'main.bin'),
                  after_core_sha256=sha(a.after_core/'main.bin'),
                  before_bank_sha256=sha(a.before_bank), after_bank_sha256=sha(a.after_bank),
                  fixture_sha256=sha(Path(__file__)), device_access=False,
                  limits=['Integer stages only; no full IRQ, UI scheduler, cache or FPGA timing.',
                          'Native dispatch uses disabled LFO3 render; separate cfbench runs its real engine/EMAC.',
                          'TRIG preparation, Sound publication, smoothing and native lock application are boundaries.',
                          'Sender uses the existing transport fixture, all digital types stock.',
                          'Instructions are not cycles or hardware CPU percentages.'])
    a.out.write_text(json.dumps(result, indent=2)+'\n')
    print('PASS integer idle stages, callback selection and stock sender')
    print('Idle:', old['idle'][-1], '->', new['idle'][-1])
    print('Native:', old['native'], '->', new['native'])
    print('Sender:', old['sender'], '->', new['sender'])

if __name__ == '__main__':
    main()
