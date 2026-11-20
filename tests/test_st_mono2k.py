"""Check the 48 kHz port against pinned MONO2K using native 64-bit arithmetic.

This validates extraction, sample-rate tables, arithmetic, pitch and controls;
it is not a comparison against Elektron's original DSP or a CPU benchmark.
"""
import argparse
import importlib.util
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
MOD = ROOT / 'mods/digimono-st'


def function(source, name):
    match = re.search(r'^static[^\n]+\b' + name + r'\([^\n]*\)\s*(?:/\*[^\n]*\*/\s*)?\n?\{', source, re.M)
    assert match, name
    start = match.end() - 1
    depth = 1
    for end in range(start + 1, len(source)):
        depth += (source[end] == '{') - (source[end] == '}')
        if not depth:
            return source[match.start():end+1] + '\n'
    raise AssertionError(name)


def oracle():
    upstream = MOD / 'mono2k/upstream'
    core = (upstream / 'm2k_core.c').read_text().replace('MUL_FN ', 'static inline ')
    dsp = (upstream / 'm2k_dsp.c').read_text()
    code = '''#include "mono2k/voice.h"
#include "mono2k/tables.h"
#define M2K_SUB 16
#define P_SYN 0
#define M_SID DM_SID
#define M_FMS DM_FMSTAT
#define M_FMD DM_FMDYN
#define M_FMP DM_FMPAR
#define M_ENS DM_ENS
#define ENS_LEN 1024u
#define ENS_GAIN 7119
#define SID_A 24950
#define SID_MIX0 1024
#define SID_MIXA (SID_A * 15 / 32)
#define P8(t,id) ((int32_t)p[id])
#define P7(t,id) (P8(t,id)>>8)
#define PB8(t,id) 0
#define PK(t,id,k) ((id)==1 && (k)==64 ? (P8(t,id)*64*441+61440)/122880 : (P8(t,id)*(k))>>8)
'''
    for name in ('sat16', 'sinp', 'inc_of', 'mulu_hi'):
        code += function(core, name)
    for name in ('blep_saw', 'tq_u16'):
        code += function(dsp, name)
    cases = dsp[dsp.index('    case M_SID: {'):dsp.index('    case M_FMS: {')]
    # Original previous-track lookup is supplied by the Syntakt adapter.
    code += '''static struct {int32_t pitch;} trk[8];
#define M2K_TRACKS6 8
static void ref_block(struct dm_m2k_voice *t,unsigned type,const uint16_t *p,int32_t pitch,int16_t *o){
    int ti=1;
    switch(type){
''' + cases + '''    }
}
void dm_ref_render(struct dm_m2k_voice *v,unsigned type,const uint16_t *p,int32_t pitch,int32_t previous,int16_t *out){
    trk[0].pitch=previous;
    ref_block(v,type,p,pitch,out);ref_block(v,type,p,pitch,out+16);
}
'''
    ens = dsp[dsp.index('    case M_ENS: {'):dsp.index('    case M_WAV: {')]
    # Measured control-law corrections; native-wide arithmetic independently
    # checks the phase mapping's ColdFire split/divide implementation.
    ens = ens.replace('12124','9830').replace('53412','55706')
    ens = ens.replace('(uint32_t)P8(t, P_SYN + 4) << 17',
                      '(uint32_t)(42949673u + (((uint64_t)P8(t, P_SYN + 4) * (1u<<17) * 49u + 25u) / 50u))')
    code += '''static void ref_ens_block(struct dm_ens_voice *t,const uint16_t *p,int32_t pitch,int16_t *o,int16_t *side){
    int ti=0;int16_t *ens_mem[1]={t->line},*eside[1]={side};
    switch(DM_ENS){
''' + ens + '''    }
}
void dm_ref_ens_render(struct dm_ens_voice *v,const uint16_t *p,int32_t pitch,int16_t *left,int16_t *right){
    int16_t mid[32],side[32];
    ref_ens_block(v,p,pitch,mid,side);ref_ens_block(v,p,pitch,mid+16,side+16);
    for(int i=0;i<32;i++){left[i]=(int16_t)sat16((int32_t)mid[i]+side[i]);right[i]=(int16_t)sat16((int32_t)mid[i]-side[i]);}
}
'''
    return code


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cc', type=Path, required=True)
    p.add_argument('--out', type=Path, default=Path('out/mono2k-tests'))
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    spec = importlib.util.spec_from_file_location('mono2k_tables', MOD / 'mono2k/generate.py')
    generator = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(generator)
    machines, tables = generator.generate()
    assert machines == (MOD / 'mono2k/machines.inc').read_text()
    assert tables == (MOD / 'mono2k/tables.h').read_text()
    assert generator.generate_ens() == (MOD/'mono2k/ens.inc').read_text()
    assert generator.generate_ens_ui() == (MOD/'mono2k/ens-ui.h').read_text()
    # At the upstream rate, every selected table must match the pinned output.
    original = (MOD / 'mono2k/upstream/m2k_tables.h').read_text()
    _, tables44 = generator.generate(44100)
    for name in generator.TABLES:
        pattern = r'static const [^\n]+\b' + name + r'\[.*?\n};\n'
        assert re.search(pattern, tables44, re.S)[0] == re.search(pattern, original, re.S)[0], name
    reference = (a.out / 'mono2k-reference.c').resolve()
    reference.write_text(oracle())
    exe = (a.out / 'mono2k.exe').resolve()
    subprocess.run([str(a.cc), '-O2', '-Wall', '-Werror', '-I', str(MOD),
                    'tests/test_st_mono2k.c', 'mods/digimono-st/fmplus.c', str(reference), '-o', str(exe)], cwd=ROOT, check=True)
    subprocess.run([str(exe)], check=True)
    print('PASS: pinned MONO2K hashes, deterministic extraction, original 44.1 kHz tables and regenerated 48 kHz tables', flush=True)


if __name__ == '__main__':
    main()
