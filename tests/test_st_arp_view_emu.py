"""Linked ARP view/gesture/edit lifecycle and SHA-pinned native resources.

UI allocation, stack publication, base IView ctor/dtor and text rasterization
are explicit doubles. Native immediate exclusion/core dispatcher, configuration,
selector and pool lifecycle execute target code. No physical UI/audio assertion.
"""
import argparse,hashlib,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_arp_generator_emu import Machine,W,SP,RET,KIT
from test_st_arp_selector_emu import DONOR_SHA
from unicorn.m68k_const import UC_M68K_REG_A7

APP,MAIN,MODEL,NODE,EV,ROOT,BMP,VECTOR=0x49011000,0x49012000,0x49012400,0x49012500,0x49012600,0x49013000,0x49014000,0x49015000
PARAM,PROXY,PVT,INNER,IVT=0x49018000,0x49018100,0x49018200,0x49018300,0x49018400
GET_SOUND,NOTIFY=RET+0x200,RET+0x204

class ViewMachine(Machine):
    def __init__(self,core,bank):
        self.ready=False;super().__init__(core,bank)
        self.ready=True;self.track=0;self.allocs=0;self.disposals=0;self.closed=0;self.dirty=0;self.rows=[];self.overlay=0;self.oom=False
        host=self.word(self.sym['arp_host']);storage=self.word(host+4)
        self.alloc=self.word(storage+12);self.telemetry=self.word(host+64)
        self.w(MAIN,0x401c81fc);self.w(MAIN+0xa4,MODEL);self.top(MAIN)
        self.w(MAIN+0xc0,PARAM);self.w(PARAM,0x401c97fc);self.w(PARAM+0x68,MODEL+0x40)
        self.w(PROXY,PVT);self.w(PVT+0x28,0x40012986);self.w(PROXY+0x10,INNER)
        self.w(INNER,IVT);self.w(IVT+0x28,GET_SOUND);self.w(IVT+0x10,NOTIFY)
        self.u.mem_write(GET_SOUND,b'\x4e\x75');self.u.mem_write(NOTIFY,b'\x4e\x75');self.notified=0
        self.w(0x444df618,VECTOR);self.w(0x444df61c,VECTOR+96*28)
    def top(self,v):self.w(APP+0x40+0x1c,NODE);self.w(NODE+8,v)
    def args(self,n):return struct.unpack('>%dI'%n,self.read(self.u.reg_read(UC_M68K_REG_A7)+4,4*n))
    def string(self,a):
        x=bytearray()
        while a and len(x)<128:
            c=self.read(a,1)[0];a+=1
            if not c:break
            x.append(c)
        return x.decode('ascii')
    def code(self,u,pc,n,ctx):
        if not self.ready:return super().code(u,pc,n,ctx)
        if pc==self.alloc:
            assert self.args(1)==(96,);self.allocs+=1;self.ret(0 if self.oom else ROOT);return
        if pc==0x4001ff74:assert self.args(1)==(MODEL,);self.ret(self.track);return
        if pc==0x4001c422:
            assert self.args(3)==(MODEL+0x40,0x60,0xffffffff);self.ret(PROXY);return
        if pc==GET_SOUND:
            assert self.args(1)==(INNER,);self.ret(KIT+0x2a+self.track*0x123);return
        if pc==NOTIFY:
            assert self.args(2)==(INNER,0);self.notified+=1;self.ret();return
        if pc==0x400ffba6:
            v,title=self.args(2);assert v==ROOT+16 and self.string(title)=='ARPEGGIATOR'
            self.u.mem_write(v,bytes(80));self.w(v,0x40241bdc);self.ret();return
        if pc==0x400ffaf4:assert self.args(2)==(ROOT+16,1);self.ret();return
        if pc==0x40100bc2:
            manager,pair,flags=self.args(3);assert manager==APP+0x40 and flags==0
            assert self.word(pair)==ROOT+16 and self.word(pair+4)==ROOT
            self.top(ROOT+16);self.ret(1);return
        if pc==0x4016dde8:
            # Shared owner release: stack manager retains the view after push.
            assert self.word(self.args(1)[0])==ROOT;self.ret();return
        if pc==0x400ffdfc:self.disposals+=1;self.ret();return
        if pc==0x400ffd02:
            assert self.args(1)==(ROOT+16,);self.closed+=1;self.ret();return
        if pc==0x400ffc5e:self.dirty+=1;self.ret();return
        if pc==0x400f8a0c:
            bmp,font,x,y,align,fmt,text=self.args(7);assert bmp==BMP and self.string(fmt)=='%s'
            self.rows.append(['text',self.word(font+4),x,y,align,self.string(text)]);self.ret();return
        if pc==0x400f8c18:
            bmp,font,x,y,align,frame,erase,fmt,text=self.args(9)
            assert bmp==BMP and font==0x402a9dc4 and frame==0 and align==1 and self.string(fmt)=='%s'
            assert self.string(erase)==('ARP XX' if x==14 else 'OFS' if y==9 else 'LEN' if y==26 else 'XXX')
            self.rows.append(['framed-text',font,x,y,align,self.string(text)]);self.ret();return
        if pc in [0x400f7df2,0x400f7bd6]:
            self.rows.append(['shape',pc,*self.args(6)[1:]]);self.ret();return
        if pc==0x400f8dac:
            bmp,resource,x,y,mode=self.args(5);assert bmp==BMP
            if resource>=VECTOR and resource<VECTOR+96*28:resource=(resource-VECTOR)//28
            else:
                assert self.word(resource+4)==27 and self.word(resource+8)==27
                assert self.word(resource+16)==0x402eaad4 and self.word(resource+20)==0x402d736c
                resource='rail'
            self.rows.append(['bitmap',resource,x,y,mode]);self.ret();return
        return super().code(u,pc,n,ctx)
    def key(self,key,flags,block):
        self.w(EV+12,key);self.w(EV+16,flags);self.w(self.telemetry,block)
        return self.call('arp_view_key',APP,EV)
    def encoder(self,knob,delta):
        self.w(EV+12,knob);self.w(EV+16,delta)
        return self.call('arp_view_encoder',APP,EV)
    def dispose(self):
        table=self.word(ROOT+16);self.call(self.word(table),ROOT+16);self.top(MAIN)

class NativeKeyMachine(ViewMachine):
    """Original MainScreen key switch and real global core key dispatch.

    Parameter-page selection and view-manager dispatch are explicit boundaries;
    key getters, flags and the native switch execute original instructions.
    """
    def __init__(self,core,bank):
        super().__init__(core,bank);self.native_pages=[];self.manager_calls=0
    def code(self,u,pc,n,ctx):
        if pc==0x4003384a:
            v,page,secondary=self.args(3)
            assert v==MAIN and not secondary
            self.native_pages.append(page);self.ret();return
        if pc==0x401012d4:
            assert self.args(2)==(APP+0x40,EV);self.manager_calls+=1;self.ret();return
        return super().code(u,pc,n,ctx)
    def stock_key(self,key,flags=1):
        self.w(EV+12,key);self.w(EV+16,flags)
        self.call(0x40033ee0,MAIN,EV)
    def global_key(self,key,flags,block):
        self.w(EV+12,key);self.w(EV+16,flags);self.w(self.telemetry,block)
        return self.call(self.cs['core:core_key'],APP,EV)

def native_key_binding(core,bank):
    m=NativeKeyMachine(core,bank)
    # Derive TRIG from native dispatch to parameter page 0; SYN selects page 1.
    # The old 0x12 (18) does neither. This failed on TEST01's recognizer.
    m.stock_key(0x12);assert m.native_pages==[]
    m.stock_key(0x13);assert m.native_pages==[0]
    m.stock_key(0x14);assert m.native_pages==[0,1]
    keys=W+0x900;m.w(keys,m.sym['arp_view_key']);m.w(keys+4,0)
    m.w(m.cs['core:core_key_table']+2,keys)
    m.w(m.cs['core:events']+2*8,keys)
    assert not m.global_key(0x12,1,0) and not m.global_key(0x12,16,10)
    assert not m.global_key(0x12,1,20) and not m.global_key(0x12,16,30)
    assert not m.word(m.sym['arp_view']) and m.manager_calls==4
    assert not m.global_key(0x13,1,100) and not m.global_key(0x13,16,110)
    assert m.global_key(0x13,1,200)==1 and m.word(m.sym['arp_view'])==ROOT+16
    assert m.global_key(0x13,16,210)==1 and m.manager_calls==6
    return {'TRIG':19,'SYN':20,'old_wrong_key':18,'native_page_switch_executed':True,
            'global_core_key_dispatch_executed':True,'wrong_key_double_tap_rejected':True,
            'boundaries':['parameter-page selection','view-manager dispatch','view allocation/base/stack/raster as above']}

def resources(donor,core):
    d=donor.read_bytes();assert hashlib.sha256(d).hexdigest()==DONOR_SHA
    s=core.read_bytes();b=0x40000400;rows=[]
    for a,z in [(0x4022ef58,0x402a7354),(0x402319c8,0x402a9dc4)]:
        dh=struct.unpack_from('>5I',d,a-b);sh=struct.unpack_from('>5I',s,z-b)
        assert dh[:2]==sh[:2]
        widths=d[dh[2]-b:dh[2]-b+256];offsets=struct.unpack_from('>256H',d,dh[3]-b)
        pixels=max(off+width for off,width in zip(offsets,widths) if off!=65535)
        for dp,sp,n in zip(dh[2:],sh[2:],[256,512,pixels]):assert d[dp-b:dp-b+n]==s[sp-b:sp-b+n]
        rows.append(dict(digitone=hex(a),syntakt=hex(z),glyph_bytes=pixels,metrics_equal=True))
    for a,z in [(0x40255b58,0x402eaad4),(0x40255aec,0x402d736c)]:assert d[a-b:a-b+108]==s[z-b:z-b+108]
    return rows

def verify(core,bank,donor,out):
    fonts=resources(donor,core);m=ViewMachine(core,bank);initial=m.pool_counts()
    assert not m.key(0x13,1,0) and not m.key(0x13,16,10)
    assert m.key(0x13,1,100)==1 and m.word(m.sym['arp_view'])==ROOT+16
    assert m.key(0x13,16,110)==1
    assert m.read(m.sym['arp_parameters'],1)==b'\x00' # opening keeps MODE OFF
    assert m.word(ROOT+16)!=0x40241bdc and m.allocs==1
    m.call('arp_view_draw',ROOT+16,BMP,0)
    assert ['text',0x402a7354,64,59,2,'T1 ARPEGGIATOR'] in m.rows
    assert ['framed-text',0x402a9dc4,14,2,1,'ARP T1'] in m.rows
    assert ['bitmap','rail',1,7,0] in m.rows
    for knob,delta in [(1,3),(2,127),(3,127),(4,127),(5,-127),(6,-127)]:assert m.encoder(knob,delta)==1
    p=m.sym['arp_parameters'];assert m.read(p,5)==bytes([3,17,3,127,0])
    assert m.read(p+8,1)==b'\xe8'
    m.rows=[];m.call('arp_view_draw',ROOT+16,BMP,0)
    assert ['text',0x402a7354,112,37,2,'128'] in m.rows
    m.w(m.telemetry,1610);m.call('arp_view_tick',APP);m.rows=[];m.call('arp_view_draw',ROOT+16,BMP,0)
    assert ['text',0x402a7354,112,37,2,'LEN'] in m.rows
    assert m.key(17,1,200)==1 and m.word(m.sym['arp_selected'])==1
    assert m.key(12,1,210)==1 and int.from_bytes(m.read(p+6,2),'big')==0xfffd
    assert m.key(12,16,211)==1
    # A native modal tail owns input and paints above this view.
    m.top(MAIN+0x800);assert m.encoder(1,1)==0 and m.key(13,1,220)==0
    m.top(ROOT+16);assert m.key(11,1,230)==0 # native STOP
    # Closing does not disable mode; destructor removes only UI pointers.
    assert m.key(13,1,240)==1 and m.closed==1;m.dispose()
    assert m.disposals==1 and m.word(m.sym['arp_view'])==0 and m.read(p,1)==b'\x03'
    assert m.key(0x13,1,300)==0 and m.key(0x13,16,310)==0 and m.key(0x13,1,400)==1
    assert m.read(p,1)==b'\x03';m.key(0x13,16,410);m.dispose()
    # Wrong view/FUNC/held repeat/track change/OOM/FX leave native action.
    q=ViewMachine(core,bank);q.top(MAIN+0x800)
    assert not q.key(0x13,1,0);q.top(MAIN);q.key(0x13,16,1)
    assert not q.key(0x13,3,10);q.key(0x13,16,11)
    assert not q.key(0x13,1,20) and not q.key(0x13,9,30)
    q.key(0x13,16,31);q.track=1;assert not q.key(0x13,1,40);q.key(0x13,16,41)
    q.oom=True;assert not q.key(0x13,1,50) and not q.key(0x13,16,51)
    q.oom=False;q.track=12;assert not q.key(0x13,1,70);q.key(0x13,16,71)
    assert not q.key(0x13,1,90) and not q.key(0x13,16,91)
    assert q.word(q.sym['arp_view'])==0
    # Encoder D updates a held root's next output rather than killing it.
    q=ViewMachine(core,bank);q.key(0x13,1,0);q.key(0x13,16,10);q.key(0x13,1,100);q.key(0x13,16,110)
    q.encoder(1,1)
    base=q.pool_counts();q.event(length=0);q.frame(0);generation=q.word(q.sym['arp_generation'])
    assert q.log[-1]['gate']==2784375 and q.encoder(4,1)==1
    assert q.word(q.sym['arp_generation'])==generation
    q.frame(5400000);assert q.log[-1]['gate']==2868750
    q.rows=[];q.call('arp_view_draw',ROOT+16,BMP,0)
    assert ['text',0x402a7354,112,37,2,'1.03'] in q.rows
    q.w(q.telemetry,0xfffffa24);assert q.encoder(4,1)==1 # deadline wraps exactly to zero
    assert q.word(q.sym['arp_length_until'])==0
    q.w(q.telemetry,0);q.call('arp_view_tick',APP);assert q.word(q.sym['arp_length_show'])==0
    q.configure(mode=0);assert q.pool_counts()==base;q.dispose()
    # A Sound-locked sequence root has its own ARP. Viewing/editing the track's
    # Sound must not show or overwrite that other Sound's runtime settings.
    q=ViewMachine(core,bank);q.key(0x13,1,0);q.key(0x13,16,10);q.key(0x13,1,100);q.key(0x13,16,110)
    other=KIT+0x500
    q.u.mem_write(W+0x100,struct.pack('>5BxH16b',3,9,1,30,15,0xffff,*([0]*16)))
    assert q.call('arp_sound_store',other,W+0x100)==1
    root,lock=q.event();q.w(root+40,other);q.frame(0)
    assert q.read(q.sym['arp_parameters'],1)==b'\x03'
    assert q.read(q.sym['arp_ui_parameters'],1)==b'\x00'
    generation=q.word(q.sym['arp_generation']);assert q.encoder(1,1)==1
    assert q.read(q.sym['arp_ui_parameters'],1)==b'\x01' and q.read(q.sym['arp_parameters'],1)==b'\x03'
    assert q.word(q.sym['arp_generation'])==generation
    q.configure(mode=0);q.dispose()
    assert m.pool_counts()==initial
    binding=native_key_binding(core,bank)
    result=dict(ok=True,fonts=fonts,key_binding=binding,rail_pixels_and_mask_equal=True,native_constructor_executed_for_rail=True,
        lifecycle=['double tap opens with MODE OFF','encoders A..F clamp','selected step/mute','modal priority',
                   'native STOP passes','NO closes without disabling','destructor/reopen','failed opening preserves native release',
                   'D changes next gate with held root retained','native length labels and popup expiry/wrap',
                   'Sound-locked root does not replace/edit the viewed Sound'],
        limits=['View stack ownership/base ctor/dtor/allocation and text rasterization are explicit doubles.',
                'Configuration uses the real native immediate/core bus. No physical framebuffer/audio assertion.',
                'One displayed digital track; concurrency, keyboard, gates and persistence have separate fixtures. Synthesis/device not run.'],device_access=False)
    out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['core','bank','digitone-main','out']:p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();verify(a.core,a.bank,a.digitone_main,a.out)
