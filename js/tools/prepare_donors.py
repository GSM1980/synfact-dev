# SPDX-License-Identifier: AGPL-3.0-or-later
"""Make/check public donor recipes from the owner's OS files; no firmware bytes.

python js/tools/prepare_donors.py --digitakt "$DIGITAKT_OS" --monomachine "$MONOMACHINE_OS"
Run the normal importers first. --check verifies the committed recipes.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from elekloader.sdk.syntakt import donor
from elekloader.syx import Syx
from elekloader.devices import monomachine


def prepare(digitakt, mnm):
    dt=Path(digitakt).read_bytes()
    if donor.sha(dt)!='f78ba80fa7b1da5fb0e1ff61ad61e9e71aafe79f4364fc49679f3651353e3cf6':
        raise ValueError('Requires official Digitakt OS 1.54')
    image=Syx(dt).section(3)
    provenance=json.loads((ROOT/'mods/comp-st/imported/provenance.json').read_text())
    assert provenance['os_sha256']==donor.sha(dt)
    assets=provenance['assets']
    start=min(int(a[k],16)-0x40000400 for a in assets for k in ('bits','mask'))
    end=max(int(a[k],16)-0x40000400+76 for a in assets for k in ('bits','mask'))
    regions=[]
    for name,at,length in [('labels',0x1c0000,0x15000),('graphics',start,end-start)]:
        data=image[at:at+length]
        assert len(data)==length
        regions.append(dict(name=name,section=3,start=at,length=length,sha256=donor.sha(data)))
    plans={}
    for a in assets:
        plan=plans.setdefault('comp_'+a['kind']+'_bits',[])
        offset=(len(plan)//2)*152
        for side,k in enumerate(('bits','mask')):
            plan.append([offset+76*side,'graphics',int(a[k],16)-0x40000400-start,76])
    comp=dict(format=donor.FORMAT,schema=1,title='COMP',device='digitakt-mk1',versions=['1.54'],
              minimum=4,regions=regions,outputs=[],symbol_copies=plans)
    tables=monomachine.digipro_tables(Path(mnm).read_bytes())
    mnm_recipe=dict(format=donor.FORMAT,schema=2,title='SY MnM',device=monomachine.KEY,
                    versions=[monomachine.VERSION],minimum=4,
                    regions=[dict(name=k,transform=monomachine.TRANSFORM,length=len(v),sha256=donor.sha(v)) for k,v in tables.items()],
                    outputs=[],symbol_copies={'dpt_'+k:[[0,k,0,len(v)]] for k,v in tables.items()})
    return {'comp-st':('digitakt.json',comp,['ui.c']),
            'digimono-st':('monomachine.json',mnm_recipe,['dpro.c'])}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--digitakt',type=Path,required=True)
    p.add_argument('--monomachine',type=Path,required=True)
    p.add_argument('--check',action='store_true');a=p.parse_args()
    for slug,(name,recipe,sources) in prepare(a.digitakt,a.monomachine).items():
        folder=ROOT/'mods'/slug
        payload=json.dumps(recipe,indent=2)+'\n'
        manifest=json.loads((folder/'mod.json').read_text(encoding='utf-8'))
        manifest['smz' if slug=='digimono-st' else 'smz_module']['donor']={'recipe':name,'sources':sources}
        if a.check:
            assert json.loads((folder/name).read_text())==recipe,slug
            recorded=json.loads((folder/'mod.json').read_text())
            assert recorded==manifest,slug
        else:
            (folder/name).write_text(payload,encoding='utf-8',newline='\n')
            (folder/'mod.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
        print(('Verified ' if a.check else 'Prepared ')+slug+' donor recipe; no OS bytes written')

if __name__=='__main__':main()
