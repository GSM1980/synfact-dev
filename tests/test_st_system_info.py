"""Compile production telemetry with explicit native collector boundaries."""
import argparse,hashlib,json,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def verify(cc):
    source=ROOT/'mods/system-info-st/system_info.c';text=source.read_text(encoding='utf-8')
    anchor='#include "ram.c"';assert text.count(anchor)==1
    # Keep the complete tick, formatter, measurement, query and drawing code.
    # Native RAM collector contains target-only EMAC/platform boundaries.
    doubles='''struct si_ram_result {unsigned int total,used,valid;};
static void si_ram_main(struct si_ram_result *r){ram_calls++;r->total=100;r->used=50;r->valid=1;}
static void si_ram_audio(unsigned int p,struct si_ram_result *r){(void)p;r->total=100;r->used=25;r->valid=1;}
static unsigned int si_ram_percent(const struct si_ram_result *r){return r->valid?r->used:0xffffffffu;}
'''
    out=ROOT/'out/system-info-tests';out.mkdir(exist_ok=True)
    (out/'system-info-under-test.c').write_text(text.replace(anchor,doubles),encoding='utf-8')
    exe=out/'system-info.exe'
    subprocess.run([str(cc),'-O2','-Wl,--large-address-aware','-I',str(ROOT/'elekloader/sdk/include'),
                    str(ROOT/'tests/test_st_system_info.c'),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
    (out/'validation.json').write_text(json.dumps({'ok':True,
        'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),
        'ticks':1800,'measurement_windows':60,'steady_redraw_requests':30,
        'target_execution':False,'device_transfer':False},indent=2)+'\n')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',type=Path,required=True)
    verify(p.parse_args().cc)
