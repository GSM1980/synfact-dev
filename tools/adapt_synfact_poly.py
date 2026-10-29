"""Explicit maintenance import of authored Synfact POLY C/assembler sources.

No legacy builders are imported or run. Normal SMZ builds use local sources.
"""
import argparse,hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def adapt(donor):
    out=ROOT/'mods/poly-st';out.mkdir(exist_ok=True)
    names=['poly_core.c','poly_event.c','poly_groups.c','poly_pattern.c',
           'poly_sequence.c','poly_publication.c','poly_type.c','poly_ui.c']
    hashes={}
    for name in names:
        src=donor/'src/current'/name;data=src.read_bytes()
        hashes['src/current/'+name]=hashlib.sha256(data).hexdigest()
        text=data.decode('utf-8')
        if name=='poly_core.c':
            text=text.replace('#include "poly_fm_transport.c"','/* Generic core parameter transport owns all machine frames. */')
            alias='__asm__(".globl poly_owner_map\\n.set poly_owner_map,poly_seq_config+16");'
            assert alias in text
            text=text.replace(alias,'#ifndef POLY_HOST_TEST\n'+alias+'\n#else\n__asm__(".globl _poly_owner_map\\n.set _poly_owner_map,_poly_seq_config+16");\n#endif')
            at=text.index('    const volatile u16 *stack;',text.index('static u32 poly_live_from_keys('))
            end=text.index('\n}\n',at)
            text=text[:at]+'#ifdef POLY_HOST_TEST\n    return poly_test_origin;\n#else\n'+text[at:end]+'\n#endif'+text[end:]
        # These are declarations of the borrowed stock replays supplied by
        # this port's prelude, never the donor's fixed-address patch registry.
        text=re.sub(r'^extern (?:u32 \*stock_trig\([^;]*|u32 stock_enqueue\([^;]*|u32 stock_header_load\([^;]*|void stock_header_reset\([^;]*|const u32 \*stock_page_layout\([^;]*);\n','',text,flags=re.M)
        if name=='poly_ui.c':
            text=text.replace('    if(poly_ui_slave(view))return 1;',
                              '    /* Shared core editor gate checks slave ownership before subscribers. */')
        # Named call boundary permits explicit native-host doubles. The target
        # still compiles each invocation to the same absolute native address.
        text=re.sub(r'\(\(([^;\n]+?)\)0x([0-9a-f]+)u\)',
                    lambda m:'POLY_CALL(0x'+m[2]+'u,'+m[1]+')',text)
        (out/name).write_text(text,encoding='utf-8')
    for name in ['publication.s','guards.s']:
        src=donor/'src/features/lfo3/poly'/name;data=src.read_bytes()
        hashes['src/features/lfo3/poly/'+name]=hashlib.sha256(data).hexdigest()
        if name=='publication.s':(out/name).write_bytes(data)
    (out/'provenance.json').write_text(json.dumps({'source':'Synfact authored digital POLY',
        'source_sha256':hashes,'changes':['shared core events and bounded native transaction replays',
        'generic current machine transport replaces legacy FM frame',
        'UI input uses existing KEY/ENC view contexts; no runtime patches']},indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--synfact',type=Path,required=True)
    adapt(p.parse_args().synfact.resolve())
