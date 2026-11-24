"""Real COMP/Chorus providers with LFO3/POLY/Playhead initialized.

Executes shared Kit codecs and LFO3's owning copy, with the activity fixture's
explicit native heap/mutex/application boundaries. No device access.
"""
import argparse
from pathlib import Path
import struct
from test_st_lfo3_activity_emu import Activity
from test_st_module_startup_emu import DESCRIPTORS
from test_st_comp_emu import ID,KIT


def verify(core,bank):
    m=Activity(core,bank,full_catalog=True)
    api=DESCRIPTORS+0xf000;m.w(api,3);m.w(api+4,m.sym['core:core_machine_ui_api'])
    m.sr=0x2700
    def registered():return set(struct.unpack('>4I',bytes(m.u.mem_read(m.sym['core:core_parameter_providers'],16))))-{0}
    before=registered();assert before=={m.bs['poly_query'],m.bs['lfo3_parameter_query']}
    assert m.call('chorus_ui_init',api)==0
    chorus_provider=registered()-before;assert len(chorus_provider)==1
    m.call('comp_params_init');assert m.call('comp_ui_init',api)==0
    assert registered()==before|chorus_provider|{m.bs['comp_query']}
    for name,at in [('kitnew',0x400b3384),('kitenc',0x400a78f4),('kitdec',0x400a78a0),
                    ('copykit',0x4011613c),('getglobal',0x4000d94a),('setglobal',0x4000da32)]:m.bs[name]=at
    m.sr=0x2000;m.w(0x800030b8,KIT);m.w(0x800030bc,KIT+0xdce)
    m.call('kitnew',KIT)
    values=[0x1234,0x2345,0x3456,0x4567,0x500,0x5678,0x200]
    chorus=[0x100,0x2000,0x7f00,0,0x3000,0x4000,0]
    for i,v in enumerate(values):assert m.call('setglobal',api,ID+i,v,0,0,1)==0
    for i,v in enumerate(chorus):assert m.call('setglobal',api,0x403+i,v,0,0,1)==0
    slot=0x43c0dfc0+0x2e0200
    assert m.call('kitenc',slot,KIT,0)&255
    assert m.word(m.bs['kit_codec_depth'])==0
    assert m.word(slot+0x14e0)!=0 and m.word(slot+0x15d0)>>16==0x4351
    m.call('kitnew',KIT)
    assert m.call('kitdec',KIT,slot)&255
    assert m.word(m.bs['kit_codec_depth'])==0
    assert [m.call('getglobal',api,ID+i) for i in range(7)]==values
    assert [m.call('getglobal',api,0x403+i) for i in range(7)]==chorus
    dst=KIT+0x1618
    assert m.call('copykit',dst,KIT,0x1618)==dst
    m.w(0x800030b8,dst);m.w(0x800030bc,dst+0xdce)
    assert [m.call('getglobal',api,ID+i) for i in range(7)]==values
    assert [m.call('getglobal',api,0x403+i) for i in range(7)]==chorus
    assert m.word(m.lfo['hooks_installed'])==1
    # 1.1.0: a kit slot of a container only LFO3 knows (registered as its serializer
    # registers one; kitChanged's mirror slots are such slots): the real codec's
    # Sound metadata names it, so COMP's record is written and read back there,
    # which the core's notification alone never did.
    m.w(0x800030b8,KIT);m.w(0x800030bc,KIT+0xdce)
    container=0x4c000000;mirror=container+0x2e0200+5*0x1600
    m.u.mem_map(container,0x400000)                # a whole native container (3e6604h), zeroed
    m.w(m.bs['containers'],container)
    assert m.call('kitenc',mirror,KIT,0)&255
    assert m.word(mirror+0x15d0)>>16==0x4351,'COMP record in the LFO3-named container'
    m.call('kitnew',KIT)
    assert [m.call('getglobal',api,ID+i) for i in range(7)]!=values
    assert m.call('kitdec',KIT,mirror)&255
    assert [m.call('getglobal',api,ID+i) for i in range(7)]==values,'COMP back from the mirror'
    m.w(m.bs['containers'],0)
    print('PASS four real providers: COMP/Chorus values, LFO3 Kit depth/trailer, native codecs and owning copy, COMP in an LFO3-named container')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core',type=Path,required=True);p.add_argument('--bank',type=Path,required=True)
    a=p.parse_args();verify(a.core,a.bank)
