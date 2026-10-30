"""Compare authored UI primitive-for-primitive with the original DN draw.

Models, fonts and bitmap resolution are explicit doubles. The original drawing
instructions and integer graph arithmetic execute unmodified. Equality proves
the drawing command stream, not on-device ownership, glyphs or modal lifecycle.
"""
import argparse,hashlib,json,random,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_arp_selector_emu import *
from unicorn import UC_HOOK_CODE

VIEW,STRINGS,SCREEN,CANVAS=OWN+0x30000,OWN+0x21000,SETTINGS+0x100,SETTINGS+0x200
TEXT,SHAPE,BITMAP=RETURN+0x100,RETURN+0x102,RETURN+0x104

class Drawing:
    def __init__(self,lab):
        self.lab=lab;self.u=lab.u;self.rows=[]
        self.u.mem_map(0x419e0000,0x20000)
        self.h=self.u.hook_add(UC_HOOK_CODE,self.code)
    def args(self,n):
        sp=self.u.reg_read(UC_M68K_REG_A7)
        return struct.unpack('>%dI'%n,self.lab.read(sp+4,4*n))
    def string(self,a):
        s=bytearray()
        while a and len(s)<128:
            c=self.lab.read(a,1)[0];a+=1
            if not c:break
            s.append(c)
        return s.decode('ascii')
    def put_string(self,a,s):self.u.mem_write(a,s.encode()+b'\0');return a
    def ret(self,v=0):
        sp=self.u.reg_read(UC_M68K_REG_A7);pc=int.from_bytes(self.lab.read(sp,4),'big')
        self.u.reg_write(UC_M68K_REG_D0,v&0xffffffff);self.u.reg_write(UC_M68K_REG_A7,sp+4);self.u.reg_write(UC_M68K_REG_PC,pc)
    def code(self,u,pc,n,_):
        p=self.p
        fixed={0x40149fba:0,0x400250a4:0,0x4002c6ba:self.track,0x400250b0:0,0x4001dcf2:0,
               0x400deb66:5,0x400de9c8:1,0x400de96c:123,
               0x40031972:p['mode'],0x40031b0a:p['speed'],0x40031ba8:p['range'],
               0x40031c46:p['note_length'],0x4003169e:p['length']}
        if pc in fixed:self.ret(fixed[pc]);return
        if pc in [0x4003173a,0x40031804]:
            i=self.args(2)[1];self.ret(p['offset'][i] if pc==0x4003173a else int(bool(p['mask']&1<<i)));return
        if pc in [TEXT,0x400de120,0x400de32c]:
            a=self.args(6 if pc==TEXT else 7 if pc==0x400de120 else 9)
            if pc==TEXT:font,x,y,align,text=a[1:5]+(self.string(a[5]),)
            elif pc==0x400de120:
                font=0;x,y,align=a[2:5];fmt=self.string(a[5]);v=a[6]
                text=fmt%(self.string(v) if '%s' in fmt else (v if v<0x80000000 else v-0x100000000)) if '%' in fmt else fmt
            else:
                font=1;x,y,align=a[2:5];fmt=self.string(a[7]);v=a[8]
                text=fmt%(self.string(v) if '%s' in fmt else (v if v<0x80000000 else v-0x100000000)) if '%' in fmt else fmt
            self.rows.append(['text',font,x,y,align,text]);self.ret();return
        shapes={0x400dd506:(0,6),0x400dd2ea:(1,6),0x400dcd2a:(2,5),
                0x400dc9b8:(3,5),0x400dc854:(4,4),0x400dd1d4:(1,6),
                0x400dcba0:(2,5),0x400dcb14:(3,5)}
        if pc==SHAPE:
            a=self.args(7);row=['shape',*a[1:]]
        elif pc in shapes:
            k,n=shapes[pc];a=self.args(n)
            if k in [0,1]:row=['shape',k,*a[1:]]
            elif k==2:row=['shape',k,a[1],a[2],a[1],a[3],a[4]]
            elif k==3:row=['shape',k,a[1],a[2],a[3],a[2],a[4]]
            else:row=['shape',k,a[1],a[2],a[1],a[2],a[3]]
        else:row=None
        if row is not None:
            self.rows.append([v if not isinstance(v,int) or v<0x80000000 else v-0x100000000 for v in row]);self.ret();return
        if pc in [BITMAP,0x400de4c0]:
            a=self.args(5);resource=a[1]
            if pc!=BITMAP:resource=0xffffffff if resource==0x419ef074 else resource
            self.rows.append(['bitmap',resource,*a[2:]]);self.ret();return
    def compare(self,p,track,selected,show):
        self.p=p;self.track=track;lab=self.lab
        # Fake C-string tables at precisely the offsets the original draw uses.
        title='T%d ARPEGGIATOR'%(track+1);self.put_string(VIEW+0x214,title)
        for i,s in enumerate(['OFF','TRUE','UP','DOWN','CYCL']):lab.w(VIEW+0x234+4*i,self.put_string(STRINGS+32*i,s))
        for i in range(18):
            a=int.from_bytes(lab.read(0x4019a5d8+4*i,4),'big')
            lab.w(VIEW+0x274+4*i,self.put_string(STRINGS+0x200+32*i,self.string(a)))
        lab.w(VIEW+0x2bc,selected);self.u.mem_write(VIEW+0x2c0,bytes([show]))
        self.rows=[];lab.call(0x40051390,VIEW,0);native=self.rows
        self.u.mem_write(SETTINGS,struct.pack('>5BxH16b',p['mode'],p['speed'],p['range'],p['note_length'],p['length'],p['mask'],*p['offset']))
        # Original note-length strings are local test inputs, never shipped.
        length_native=int.from_bytes(lab.read(0x40233c04+4*p['note_length'],4),'big')
        length=self.put_string(STRINGS+0x500,self.string(length_native))
        self.u.mem_write(SCREEN,struct.pack('>7I',SETTINGS,track,selected,show,STRINGS+0x200+32*p['speed'],length,123))
        self.u.mem_write(CANVAS,struct.pack('>4I',0,TEXT,SHAPE,BITMAP))
        self.rows=[];lab.call('arp_draw',SCREEN,CANVAS);authored=self.rows
        assert authored==native,[(i,a,b) for i,(a,b) in enumerate(zip(authored,native)) if a!=b][:8]+[('lengths',len(authored),len(native))]
        return native

def verify(donor_path,out,toolchain):
    donor=donor_path.read_bytes();assert hashlib.sha256(donor).hexdigest()==DONOR_SHA
    lab=Lab(donor,build(out,toolchain));draw=Drawing(lab);rng=random.Random(29102026)
    rows=[]
    for i in range(192):
        p=dict(mode=i%5,speed=i%18,range=i%4,note_length=i%128,length=i%16,mask=[0,0xffff,0xa55a][i%3],offset=[rng.randrange(-24,25) for _ in range(16)])
        commands=draw.compare(p,i%12,(i//12)%16,i%2)
        if i==1:rows=commands
    # Wrong selection must produce a different stream in the real C drawing.
    baseline=draw.compare(p,0,0,0)
    lab.w(SCREEN+8,1);draw.rows=[];lab.call('arp_draw',SCREEN,CANVAS)
    assert draw.rows!=baseline,'negative control: source selection was ignored'
    result=dict(ok=True,cases=192,donor_main_sha256=DONOR_SHA,draw_entry='0x40051390',draw_end='0x40051928',
                scope='Identical primitive command streams with model/font/bitmap doubles; no pixel or device verification.',
                example=rows,audit=lab.audit,negative_controls=1,device_access=False)
    (out/'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: 192 original/authored drawing command streams; fonts/bitmaps/models are doubles, device UI pending')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--digitone-main',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--toolchain',type=Path,required=True)
    a=p.parse_args();verify(a.digitone_main,a.out,a.toolchain)
