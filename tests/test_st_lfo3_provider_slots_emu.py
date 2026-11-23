"""Core 2.6.1: LFO3 keeps its parameter provider after four others.

In a bank with SY MnM (its subtype selector), POLY, Chorus and COMP, those
four providers register before LFO3, which initializes last. Core 2.6.0 had
four slots and dropped LFO3's silently: its page hooks stayed, every label
read ERR. The bank's own four query functions are placed in the first slots
as their initializers would; LFO3's real initializer and the native label
hooks then run on linked OS 1.42 MAIN. Pass --reference-core with a 2.6.0
build to reproduce the ERR as a negative control. No device access.
"""
import argparse,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_module_only_emu import ModuleOnly

FIRST=('dm_destination_query','poly_query','query','comp_query')

def labels(core,bank):
    m=ModuleOnly(core,bank)
    names=[n for n in FIRST if n in m.bs]
    assert m.call('core_machine_bind',0,0)==1
    providers=m.sym['core:core_parameter_providers']
    for i,n in enumerate(names):m.w(providers+4*i,m.bs[n])
    m.run('full')
    assert m.word(m.lfo['hooks_installed'])==1
    registered=m.bs['lfo3_parameter_query'] in [m.word(providers+4*i) for i in range(len(names)+1)]
    shown=[(m.text(m.invoke(0x40012b24,0,0x3f9+i)),m.text(m.invoke(0x40012b24,0,0x60+i))) for i in range(10)]
    return names,registered,shown

def verify(core,bank,reference):
    names,registered,shown=labels(core,bank)
    assert len(names)==4,names
    assert registered and all(a==b and a!='ERR' for a,b in shown),shown
    negative=None
    if reference:
        _,old,old_shown=labels(reference,bank)
        assert not old and all(a=='ERR' for a,_ in old_shown),old_shown
        negative='2.6.0: LFO3 not registered, ten labels ERR'
    return dict(ok=True,providers_before_lfo3=names,labels=[a for a,_ in shown],
                negative_control=negative,device_access=False)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core',type=Path,required=True)
    p.add_argument('--bank',type=Path,required=True)
    p.add_argument('--reference-core',type=Path)
    p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();r=verify(a.core,a.bank,a.reference_core)
    a.out.write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
