"""Maintenance import of pinned native ColdFire kernels and authored guards.

Private generated assembler stays ignored. Normal builds never read firmware.
No donor builders or runtime patch installers are imported or executed.
"""
import argparse,hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT))
from elekloader import formats

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--stock',required=True)
    p.add_argument('--synfact',required=True,type=Path);a=p.parse_args()
    stock,dev,rel=formats.load(a.stock)
    assert dev.key=='syntakt' and rel.version=='1.41'
    assert stock.sha256=='8e2488f462c4a5656396a895f113bcd415e9900fa8709340dccf45d4cb9ed19e'
    out=ROOT/'mods/poly-st/imported';out.mkdir(exist_ok=True)
    main=stock.section(3)
    get=lambda begin,end:main[begin-0x40000400:end-0x40000400]
    def rows(data):return '\n'.join('.byte '+','.join(str(x) for x in data[i:i+32]) for i in range(0,len(data),32))
    native=get(0x40119266,0x4011958c)
    assert native[:8].hex()=='4fefffc048d77cfc'
    assert native[26:34].hex()=='70017201242f004c'
    assert native[0x240:0x244].hex()=='4ebafa50'
    def segment(begin,end):
        if begin<=0x240 and end>=0x244:
            return rows(native[begin:0x240])+'\n.word 0x4eba\n.word poly_random_bridge-.\n'+rows(native[0x244:end])
        return rows(native[begin:end])
    asm=['.text','.globl poly_native_shared,poly_native_continue,poly_native_next',
         # Keep exactly 8 bytes. GAS relaxes `jmp local` to a 4-byte PC jump,
         # shifting copied branch displacements into the middle of instructions.
         'poly_native_shared:',segment(0,26),'.word 0x4ef9','.long poly_native_lfo_guard','.word 0x4e71',
         'poly_native_continue:',segment(34,0x304),'poly_native_next:',segment(0x304,len(native)),
         'poly_random_bridge:','jmp 0x40118ef8']
    mod=get(0x40097afc,0x40097c1a)
    assert mod[:8].hex()=='4fefffc448d77cfc'
    assert mod[0x28:0x32].hex()=='2f02715c06820000007c'
    assert mod[0x62:0x6c].hex()=='486a0764797628002f04'
    assert mod[0x32:0x36].hex()=='47faff7c' and mod[0x24:0x28].hex()=='4bfaff8a'
    def part(begin,end):
        result=[];pos=begin
        for off in [0x24,0x32]:
            if begin<=off and off+4<=end:
                result += [rows(mod[pos:off]),'.word 0x'+mod[off:off+2].hex(),'.word poly_mod_helper_bridge-.'];pos=off+4
        return '\n'.join(result+[rows(mod[pos:end])])
    asm+=['.globl poly_mod_shared,poly_mod_first,poly_mod_first_continue,poly_mod_after_first',
          '.globl poly_mod_second,poly_mod_second_continue,poly_mod_return',
          'poly_mod_shared:',part(0,0x28),'poly_mod_first:',
          '.word 0x4ef9','.long poly_mod_first_guard','.word 0x4e71,0x4e71',
          'poly_mod_first_continue:',part(0x32,0x4e),'poly_mod_after_first:',part(0x4e,0x62),
          'poly_mod_second:','.word 0x4ef9','.long poly_mod_second_guard','.word 0x4e71,0x4e71',
          'poly_mod_second_continue:',part(0x6c,0x114),'poly_mod_return:',part(0x114,len(mod)),
          'poly_mod_helper_bridge:','jmp 0x40097aac']
    guards=(a.synfact/'src/features/lfo3/poly/guards.s').read_text(encoding='utf-8')
    guards=guards[guards.index('poly_mod_first_guard:'):guards.index('poly_native_dispatch:')]+guards[guards.index('poly_native_lfo_guard:'):]
    # Local guard adaptation: owners has 12 entries. Keep the bound: the
    # second native modulation loop also visits index 12 (FX). Bodies unchanged.
    assert guards.count('cmpi.l #8,%d0')==3
    guards=guards.replace('cmpi.l #8,%d0','cmpi.l #12,%d0')
    # Cached slave bitmap (same IRQ publication as owners) avoids extra owner
    # loads on independent analog tracks, and saves work on digital tracks.
    lookup=' lea poly_fast_owners,%a0\n moveq #0,%d1\n move.b (%a0,%d0.l),%d1\n cmp.l %d0,%d1\n bhs.s '
    assert guards.count(lookup)==3
    guards=guards.replace(lookup,' move.l poly_slave_mask,%d1\n btst %d0,%d1\n beq.s ')
    text='| Private source import. Pinned OS 1.41 kernels; authored Synfact guards.\n'+ '\n'.join(asm)+'\n'+guards
    (out/'kernels.s').write_text(text.replace('poly_fast_owners','poly_owner_map'),encoding='utf-8')
    (out/'provenance.json').write_text(json.dumps({'stock_sha256':stock.sha256,
        'source':'src/features/lfo3/poly/guards.s','guards_sha256':hashlib.sha256((a.synfact/'src/features/lfo3/poly/guards.s').read_bytes()).hexdigest(),
        'local_guard_changes':['owner bound 8 -> 12; retains FX index 12 exclusion; native bodies unchanged', 'IRQ-published slave bitmap replaces owner lookup; avoids added independent analog guard cost'],
        'ranges':{'native_lfo':'40119266..4011958c','modulation':'40097afc..40097c1a'},
        'kernel_sha256':hashlib.sha256((out/'kernels.s').read_bytes()).hexdigest()},indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
