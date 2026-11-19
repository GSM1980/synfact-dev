# SPDX-License-Identifier: AGPL-3.0-or-later
"""Linked FM+ half-block must preserve every EMAC bit, including nonzero guards."""
import argparse,json,os,random,struct,subprocess
from pathlib import Path
from elekloader.sdk.elf import Elf
from elekloader.syx import Syx
from test_st_digimono_emu import Port,RETURN


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--bank',type=Path,required=True)
    p.add_argument('--stock',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    p.add_argument('--sgdk-bin',type=Path,default=os.environ.get('SGDK_BIN'));a=p.parse_args()
    assert a.sgdk_bin,'supply --sgdk-bin or SGDK_BIN';a.out.mkdir(parents=True,exist_ok=True)
    source=' .text\n .globl seed,read_state\nseed:\n move.l 4(%sp),%a0\n move.l #0,%macsr\n'
    for n,reg in enumerate(('acc0','acc1','acc2','acc3','accext01','accext23')):
        source+=f' move.l {4+4*n}(%a0),%d0\n move.l %d0,%{reg}\n'
    source+=' move.l (%a0),%d0\n move.l %d0,%macsr\n rts\nread_state:\n move.l 4(%sp),%a0\n move.l %macsr,%d0\n move.l %d0,(%a0)\n move.l #0,%macsr\n'
    for n,reg in enumerate(('acc0','acc1','acc2','acc3','accext01','accext23')):
        source+=f' move.l %{reg},%d1\n move.l %d1,{4+4*n}(%a0)\n'
    source+=' move.l %d0,%macsr\n rts\n'
    s=a.out/'guard.s';s.write_text(source);obj=a.out/'guard.o';elf=a.out/'guard.elf'
    def run(args):subprocess.run([str(x) for x in args],check=True,capture_output=True)
    run([a.sgdk_bin/'as.exe','-mcpu=54415',s,'-o',obj])
    run([a.sgdk_bin/'ld.exe','-Ttext=0x4800e000',obj,'-o',elf])
    helper=Elf.load(elf);text=helper.section('.text');symbols={s.name:s.value for s in helper.symbols if s.name}
    binary=Elf.load(a.bank.with_suffix('.work')/'audio.elf');section=binary.section('.image')
    at=struct.unpack_from('>I',binary.raw,struct.unpack_from('>I',binary.raw,32)[0]+section.idx*40+12)[0]
    bs={s.name:s.value for s in binary.symbols if s.name}
    m=Port(Syx.load(a.stock).section(7),section.data,at,bs)
    m.uc.mem_write(RETURN+0xe000,text.data)
    seed,first,after,state,knobs,output=[RETURN+x for x in (0xf000,0xf040,0xf080,0x6000,0x7000,0x7100)]
    rnd=random.Random(56300);cases=0
    for kind in (9,10,11):
        for sr in (0,0x20,0xa0,0xb0):
            for _ in range(4):
                words=[sr]+[rnd.getrandbits(32) for _ in range(6)]
                m.uc.mem_write(seed,struct.pack('>7I',*words))
                m.uc.mem_write(knobs,struct.pack('>7H',*(rnd.randrange(0x7f01) for _ in range(7))))
                m.call(bs['dm_fm_init'],state);m.call(bs['dm_fm_trig'],state,kind,knobs)
                m.call(symbols['seed'],seed);m.call(symbols['read_state'],first)
                for callback in ('dm_fm_half','dm_fm_render'):
                    m.call(bs[callback],state,kind,knobs,60*64,output)
                    m.call(symbols['read_state'],after)
                    assert bytes(m.uc.mem_read(first,28))==bytes(m.uc.mem_read(after,28)),(kind,sr,callback)
                    cases+=1
    (a.out/'results.json').write_text(json.dumps(dict(ok=True,cases=cases,all_accumulators_extensions_and_macsr=True),indent=2)+'\n')
    print('PASS:',cases,'linked FM+ EMAC guards: all accumulators, extension words and MACSR retained exactly')


if __name__=='__main__':main()
