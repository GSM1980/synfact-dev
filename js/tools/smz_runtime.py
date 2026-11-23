"""Package a pinned, blanked catalog for runtime validation by the C parser.

The owner explicitly chose input pins + independent core-parser validation,
with directed native comparisons, instead of exhaustive output approval pins.
Legacy catalogs keep their original mandatory output approval behavior.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[2]
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--catalog',type=Path,required=True)
 p.add_argument('--config',type=Path,required=True)
 p.add_argument('--validator',type=Path,required=True)
 p.add_argument('--out',type=Path,required=True);a=p.parse_args()
 catalog=json.loads(a.catalog.read_text(encoding='utf-8'));config=json.loads(a.config.read_text(encoding='utf-8'))['modules']
 validator=json.loads(a.validator.read_text(encoding='utf-8'))
 if [m['id'] for m in config]!=[m['id'] for m in catalog['modules']]:raise ValueError('Config and catalog order differ')
 core=json.loads((ROOT/'mods/core-st/mod.json').read_text(encoding='utf-8'))
 if validator['kind']!='core-parser-wasm' or validator['api']!=1 or validator['coreVersion']!=core['version']:
  raise ValueError('Wrong core parser')
 for name,pin in validator['sourceSha256'].items():
  if sha(ROOT/name)!=pin:raise ValueError('Core parser source changed: '+name)
 wasm=a.validator.parent/validator['file']
 if sha(wasm)!=validator['sha256']:raise ValueError('Core parser hash mismatch')
 ids={m['id'] for m in config}
 for pin,rule in zip(catalog['modules'],config):
  if not set(rule.get('requires',[])+rule.get('conflicts',[]))<=ids:raise ValueError('Unknown module rule')
  for key in ('requires','conflicts'):pin[key]=rule.get(key,[])
  pin['always']=rule.get('always',False)
 a.out.mkdir(parents=True,exist_ok=False)
 for pin in catalog['modules']:
  source=a.catalog.parent/pin['file']
  if sha(source)!=pin['sha256']:raise ValueError('Module pin mismatch')
  shutil.copyfile(source,a.out/pin['file'])
 shutil.copyfile(wasm,a.out/wasm.name)
 catalog['schema']=4;catalog['validation']=validator
 catalog['sourceSha256'].update(validator['sourceSha256'])
 target=a.out/'catalog-runtime.json'
 target.write_text(json.dumps(catalog,indent=2)+'\n',encoding='utf-8',newline='\n')
 print('Runtime catalog:',target,'SHA-256',sha(target))

if __name__=='__main__':main()
