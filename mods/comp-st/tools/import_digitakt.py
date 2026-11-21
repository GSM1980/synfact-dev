"""Import comp-st's native Digitakt 1.54 UI metadata from the owner's SysEx.

Writes only ignored imported/ resources, never a firmware image.
"""
import argparse
import hashlib
import json
import re
import struct
import sys
from pathlib import Path
FOLDER=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(FOLDER.parents[1]))
from elekloader.syx import Syx
SHA='f78ba80fa7b1da5fb0e1ff61ad61e9e71aafe79f4364fc49679f3651353e3cf6'
BASE=0x40000400
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('os_file',type=Path);a=p.parse_args()
    raw=a.os_file.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=SHA:raise SystemExit('requires original Digitakt OS 1.54')
    image=Syx(raw).section(3)
    def text(at):
        assert BASE<=at<BASE+len(image)
        return image[at-BASE:at-BASE+80].split(b'\0')[0].decode('ascii')
    rows=[]
    # THR ATK REL MUP RAT MIX keep indices 0..5; SCF (record 6) is index 6.
    for n in [0,1,2,3,4,7,6]:
        at=0x401ab214+52*n;raw_record=image[at-BASE:at-BASE+52];d=struct.unpack('>13I',raw_record)
        assert d[0]==9 and 0<=d[2]<=d[4]<=d[3]<=0x7f00
        rows.append(dict(source=hex(at),sha256=hashlib.sha256(raw_record).hexdigest(),
                         short=text(d[12]),long=text(d[10]),minimum=d[2],maximum=d[3],initial=d[4]))
    assert rows[6]['short']=='SCF',rows[6]
    # Only SCF's labels come from Digitakt. Its range is COMP's own: 128
    # high-pass positions (OFF, then 60..1000 Hz), OFF by default.
    rows[6].update(minimum=0,maximum=0x7f00,initial=0)
    out=FOLDER/'imported';out.mkdir(exist_ok=True)
    h='/* Private Digitakt 1.54 metadata; regenerate with tools/import_digitakt.py. */\n'
    h+='static const char *const comp_short_names[7]={'+','.join(json.dumps(x['short']) for x in rows)+'};\n'
    h+='static const char *const comp_long_names[7]={'+','.join(json.dumps(x['long']) for x in rows)+'};\n'
    h+='const u16 comp_initial[7]={'+','.join(str(x['initial']) for x in rows)+'};\n'
    h+='const u16 comp_maximum[7]={'+','.join(str(x['maximum']) for x in rows)+'};\n'
    (out/'metadata.h').write_text(h)
    # The original parameter constructors select these six native bitmap
    # vectors. Parse their literal bitmap constructors, retaining both bits
    # and mask. Each frame is 19 columns of 17 pixels, u32 column-major.
    # SCF: the 27-frame bipolar filter dial (vector 421fa2fc, drawn by
    # 400601f0): low-pass at 0, flat at 13, high-pass at 26.
    pattern=re.compile(rb'(?:\x48\x79|\x2e\xbc)(....)\x48\x79(....)\x48\x78(..)\x48\x78(..)',re.S)
    frames=[]
    for m in pattern.finditer(image):
        mask,bits=struct.unpack('>II',m[1]+m[2]);height,width=struct.unpack('>HH',m[3]+m[4])
        if width==19 and height==17 and BASE<=mask<BASE+len(image) and BASE<=bits<BASE+len(image):
            frames.append((BASE+m.start(),mask,bits))
    specs=[('thr',0x4010cc2e,15),('time',0x40112150,13),('mup',0x4010fae6,96),
           ('rat',0x40117c4e,8),('mix',0x4010c9ca,18),('scf',0x40109c84,27)]
    graphics='/* Original Digitakt bitmap frames; private donor output. */\n'
    assets=[]
    for name,before,count in specs:
        group=[x for x in frames if x[0]<before][-count:]
        assert len(group)==count
        values=[]
        for at,mask,bits in group:
            blob=image[bits-BASE:bits-BASE+76]+image[mask-BASE:mask-BASE+76]
            assert len(blob)==152
            values.append(struct.unpack('>38I',blob))
            assets.append(dict(kind=name,constructor=hex(at),bits=hex(bits),mask=hex(mask),sha256=hashlib.sha256(blob).hexdigest()))
        graphics+='static const u32 comp_%s_bits[%d][38]={\n'%(name,count)
        graphics+=',\n'.join('{'+','.join('0x%08xu'%w for w in v)+'}' for v in values)+'};\n'
    (out/'dials.h').write_text(graphics)
    # Original MUP and RAT formatters construct QString lookup tables from
    # literal strings. Read those strings directly, including their units.
    def format_table(lo,hi,target,count):
        code=image[lo-BASE:hi-BASE];found={}
        for m in re.finditer(rb'\x48\x79(....)',code,re.S):
            dst=struct.unpack('>I',m[1])[0]
            if dst<target or dst>=target+4*count or (dst-target)%4:continue
            previous=list(re.finditer(rb'(?:\x48\x79|\x2e\xbc)(....)',code[max(0,m.start()-24):m.start()],re.S))
            possible=[struct.unpack('>I',x[1])[0] for x in previous if BASE<=struct.unpack('>I',x[1])[0]<BASE+len(image)]
            if possible:found[(dst-target)//4]=text(possible[-1])
        assert len(found)==count,(hex(lo),len(found),count)
        return [found[i] for i in range(count)]
    mup=format_table(0x40064934,0x40065224,0x4197dc34,128)
    rat=format_table(0x400634ba,0x4006358e,0x4197de60,8)
    h='/* Original Digitakt value strings, private donor output. */\n'
    h+='static const char *const comp_mup_text[128]={'+','.join(json.dumps(x) for x in mup)+'};\n'
    h+='static const char *const comp_rat_text[8]={'+','.join(json.dumps(x) for x in rat)+'};\n'
    (out/'formats.h').write_text(h)
    (out/'provenance.json').write_text(json.dumps(dict(os_sha256=SHA,controls=rows,assets=assets),indent=2)+'\n')
    print('Imported',', '.join(x['short'] for x in rows),'from verified Digitakt 1.54')
if __name__=='__main__':main()
