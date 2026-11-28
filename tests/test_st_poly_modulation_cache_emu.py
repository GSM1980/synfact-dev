"""Linked POLY/MSKIP controller paths and MAIN instruction counts on OS 1.42."""
import argparse
import gc
import json
import struct
from pathlib import Path

from test_st_poly_play_emu import fixture
from test_st_poly_voice_emu import Poly, W


WORK = 0x80006348
ARGS = W + 0x4e00
MODULATION, LFO = 0x40097b74, 0x4011942a
BANK = 'synfact-machines.smz4'


def prepare(folder, bank, install=True):
    machine = Poly(folder/'main.bin', bank)
    assert machine.call('poly_record_write', 0, 0, 2) == 1
    if install:
        init = machine.word(machine.sym['modulation_skip_module']+4)
        assert machine.call(init, W) == 0
        assert stat(machine, 'mode') == 2
    native = W+0x4f00
    machine.u.mem_write(native, bytes.fromhex('a93c000000204e75'))
    machine.call(native)  # Native signed fractional MAC mode.
    machine.call(0x40118fe8)
    machine.w(ARGS, WORK)
    for track in range(13):
        machine.w(0x800046b8+4*track, (32+5*track)<<16)
        machine.u.mem_write(0x80004670+2*track, struct.pack('>H', 0x2800+0x110*track))
    return machine


def stat(machine, key):
    return machine.word(machine.sym['modulation_skip_'+key])


def configure(machine, layout):
    for root, voices, independent in layout:
        assert machine.call('poly_record_write', root, 1, voices) == 1
        machine.mode(independent, root)


def render(machine, data, tracks, oracle=False, trigs=0):
    machine.u.mem_write(WORK, data)
    before = machine.count
    machine.call('poly_mod_all' if oracle else MODULATION, WORK)
    controllers = machine.count-before
    if oracle:
        # The native kernel applies independent sources to each own row.
        # Shared slave rows must be unchanged until final publication.
        for track in tracks:
            offset = 0x1c+142*track
            machine.u.mem_write(WORK+offset, data[offset:offset+142])
    modulated = machine.read(WORK, len(data))
    before = machine.count
    machine.call(LFO, WORK, 14400, trigs)
    lfo = machine.count-before
    before = machine.count
    if tracks:
        machine.call(machine.cs['core:core_native'], 3, ARGS)
    return modulated, machine.read(WORK, len(data)), (controllers,lfo,machine.count-before)


def correctness(folder,bank):
    machine,reference = prepare(folder,bank),prepare(folder,bank,install=False)
    layouts = [((0,2,0),), ((0,4,0),(8,3,0)), ((0,4,1),(8,3,0)), ((0,4,1),(8,3,1))]
    for layout in layouts:
        for case in (machine,reference):
            for track in (0,8):assert case.call('poly_record_write',track,0,2)==1
            configure(case, layout)
        tracks = {root+voice for root,voices,independent in layout if not independent for voice in range(1,voices)}
        assert machine.word(machine.sym['poly_shared_mask']) == sum(1<<t for t in tracks)
        for phase in ('empty','sparse','keytrack','dense','empty'):
            data = bytearray(fixture(1,1,0)[0])
            for track in range(13):
                row, raw = 0x1c+142*track, 0x754+124*track
                struct.pack_into('>H',data,row+2*0x22,0x4f00-track*17)
                if phase=='sparse' and track in (0,1,3,8,10):
                    struct.pack_into('>I',data,raw+16,0x18000022+track%3)
                elif phase=='keytrack':
                    # Distinct sources on every row catch a shifted pitch
                    # cursor, even for tracks outside a group and for FX.
                    struct.pack_into('>I',data,raw+80,0x1a000022)
                elif phase=='dense':
                    for i in range(24):struct.pack_into('>I',data,raw+4*i,0x04000023+i)
            machine.w(machine.sym['modulation_skip_machine_writes'],stat(machine,'machine_writes')+2)
            for block in range(21):
                source=0x1000+(block//7)*0x100
                for case in (machine,reference):
                    case.u.mem_write(0x80004746,struct.pack('>H',source))
                    case.w(0x800046b8+4*8,(55+block//7)<<16)
                trigs=1<<(block%12) if block%3==0 else 0
                before=render(reference,bytes(data),tracks,oracle=True,trigs=trigs)
                actual=render(machine,bytes(data),tracks,trigs=trigs)
                assert actual[:2]==before[:2],(layout,phase,block,
                    [next((i for i,(a,b) in enumerate(zip(x,y)) if a!=b),None) for x,y in zip(before,actual)])
                assert stat(machine,'errors')==0 and stat(machine,'mode')==2,(layout,phase,block)
    assert stat(machine,'partial_runs')>0 and stat(machine,'skips')>0
    print('PASS: 420 blocks match native controller/LFO/publication output in mixed groups and cache transitions')


def costs(folder,bank):
    data = fixture(3,0,0)[0]  # Neutral LFOs, small unchanged controller table.
    cases = [('POLY off',()),('P.MOD 2',((0,2,1),)),('P.MOD 4',((0,4,1),)),
             ('M.MOD 2',((0,2,0),)),('M.MOD 4',((0,4,0),)),
             ('Analog M.MOD 2',((8,2,0),)),('Analog M.MOD 3',((8,3,0),))]
    results = {}
    for name,layout in cases:
        machine = prepare(folder,bank)
        configure(machine,layout)
        tracks = {root+voice for root,voices,independent in layout if not independent for voice in range(1,voices)}
        costs = [render(machine,data,tracks)[2] for _ in range(130)][2:]
        averages = [round(sum(stage)/len(costs),1) for stage in zip(*costs)]
        results[name] = dict(controllers=averages[0],LFO=averages[1],publication=averages[2],
                             total=round(sum(averages),1),quiet_controllers=min(row[0] for row in costs),
                             cache_blocks=stat(machine,'blocks'), errors=stat(machine,'errors'))
        assert not results[name]['errors']
        del machine
        gc.collect()
    return results


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--bank',type=Path)
    parser.add_argument('--previous',type=Path)
    parser.add_argument('--out',type=Path)
    args=parser.parse_args()
    bank=args.bank or args.build/BANK
    correctness(args.build,bank)
    if args.previous:
        old,new=costs(args.previous,args.previous/BANK),costs(args.build,bank)
        for case in ('M.MOD 2','M.MOD 4','Analog M.MOD 2','Analog M.MOD 3'):
            assert old[case]['cache_blocks']==0 and new[case]['cache_blocks']==130,(old[case],new[case])
            assert new[case]['total'] < old[case]['total']*0.7,(old[case],new[case])
        for case in ('POLY off','P.MOD 2','P.MOD 4'):
            assert new[case]['quiet_controllers']==old[case]['quiet_controllers'],(old[case],new[case])
        result=dict(previous=old,current=new,units='guest instructions per block')
        if args.out:args.out.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2))
