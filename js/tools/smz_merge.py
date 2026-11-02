"""Merge independently validated catalogs, reusing only unchanged artifacts."""
import argparse,hashlib,itertools,json,re
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--catalog',type=Path,required=True,help='new catalog with the target artifacts')
    p.add_argument('--config',type=Path,required=True,help='current module requirements, conflicts and always-included modules')
    p.add_argument('--reuse',type=Path,action='append',required=True,help='previous independently validated catalog')
    p.add_argument('--out',type=Path,required=True,help='new catalog beside target artifacts')
    p.add_argument('--require-complete',action='store_true',help='fail unless every permitted nonempty selection is present')
    a=p.parse_args()
    new=json.loads(a.catalog.read_text(encoding='utf-8'))
    pins={x['id']:x['sha256'] for x in new['modules']}
    order=[x['id'] for x in new['modules']]
    mods=json.loads(a.config.read_text(encoding='utf-8'))['modules']
    if [m['id'] for m in mods]!=order:raise ValueError('Config differs from target catalog')
    always={m['id'] for m in mods if m.get('always')}
    def permitted(ids):
        selected=set(ids)
        return always<=selected and all(set(m.get('requires',[]))<=selected and not set(m.get('conflicts',[]))&selected
                   for m in mods if m['id'] in selected)
    if a.out.parent.resolve()!=a.catalog.parent.resolve():raise ValueError('Output must be beside target artifacts')
    if any(not permitted(x['ids']) for x in new['approved']):raise ValueError('Target catalog contains a disallowed selection')
    entries={tuple(x['ids']):x for x in new['approved']}
    for oldpath in a.reuse:
        old=json.loads(oldpath.read_text(encoding='utf-8'))
        if (old['schema'],old['device'],old['os'])!=(new['schema'],new['device'],new['os']):raise ValueError('Incompatible catalogs')
        oldpins={x['id']:x['sha256'] for x in old['modules']}
        for item in old['approved']:
            ids=item['ids']
            if not ids or len(set(ids))!=len(ids) or not permitted(ids) or any(name not in pins or oldpins.get(name)!=pins[name] for name in ids):continue
            if ids!=[name for name in order if name in ids] or not re.fullmatch(r'[0-9a-f]{64}',item['sha256']):
                raise ValueError('Invalid old catalog entry')
            key=tuple(ids)
            if key in entries and entries[key]['sha256']!=item['sha256']:raise ValueError('Conflicting validated output')
            entries[key]=item
    if a.require_complete:
        if len(order)>16:raise ValueError('Too many modules for exhaustive completeness check')
        expected={tuple(item for item,bit in zip(order,bits) if bit) for bits in itertools.product((False,True),repeat=len(order))}
        expected={ids for ids in expected if ids and permitted(ids)}
        if set(entries)!=expected:raise ValueError(f'Incomplete catalog: {len(expected-set(entries))} permitted selections missing')
    new['approved']=list(entries.values())
    data=(json.dumps(new,indent=2)+'\n').encode()
    with a.out.open('xb') as f:f.write(data)
    print('Merged',len(entries),'validated selections; catalog SHA-256:',hashlib.sha256(data).hexdigest())

if __name__=='__main__':main()
