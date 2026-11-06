"""Linked ARP view/gesture/edit lifecycle and SHA-pinned native resources.

UI allocation, stack publication, base IView ctor/dtor and text rasterization
are explicit doubles. Native immediate exclusion/core dispatcher, configuration,
selector and pool lifecycle execute target code. No physical UI/audio assertion.
"""
import argparse,hashlib,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_arp_generator_emu import Machine,W,SP,RET,KIT,SEQ
from test_st_arp_selector_emu import DONOR_SHA
from unicorn.m68k_const import UC_M68K_REG_A7

APP,MAIN,MODEL,NODE,EV,ROOT,BMP,VECTOR=0x49011000,0x49012000,0x49012400,0x49012500,0x49012600,0x49013000,0x49014000,0x49015000
PARAM,PROXY,PVT,INNER,IVT=0x49018000,0x49018100,0x49018200,0x49018300,0x49018400
GET_SOUND,NOTIFY=RET+0x200,RET+0x204
# The encoder natives as the native pages call them (1.2): 400f66a2 with ticks x1 (x8
# pressed), 400f68d2 with the choices table 443da178 for every control (the values table
# 443da124 gave LEN, OFS and the chord page no travel on the unit). Both are explicit
# doubles: a tick is self.travel sub-steps (256 = one value by default). The table is built
# at boot and the acceleration curve is not modelled; the feel is checked on the instrument.
CHOICES=0x443da178

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
            assert self.args(1)==(16+0x50+12,);self.allocs+=1;self.ret(0 if self.oom else ROOT);return
        if pc==0x4001ff74:assert self.args(1)==(MODEL,);self.ret(self.track);return
        if pc==0x4001c422:
            assert self.args(3)==(MODEL+0x40,0x60,0xffffffff);self.ret(PROXY);return
        if pc==0x400f66a2:
            ev,normal,pressed=self.args(3);assert (normal,pressed)==(1,8),(normal,pressed)
            self.ret(self.word(ev+16));return
        if pc==0x400f68d2:
            _,field,delta,table,pressed=self.args(5);delta=delta-(1<<32) if delta>>31 else delta
            assert table==CHOICES and pressed<256,(field,hex(table))
            self.ret(delta*getattr(self,'travel',256));return
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
            assert self.args(1)==(ROOT+16,);self.closed+=1
            # Native close removes the view from the foreground stack.
            self.w(ROOT+16+0x30,1);self.top(MAIN);self.ret();return
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

LEDMGR=W+0x1800
LOCKMODEL=APP+0x3394+2*0x680+0x620 # pattern 3's lock model

class LockViewMachine(ViewMachine):
    """UI integration; pattern model/get/set and the LED setter are explicit
    boundary doubles."""
    def __init__(self,core,bank):
        super().__init__(core,bank);self.step_locks={};self.leds={}
        # The parameter view's +68h is not the application: never take its model.
        self.w(PARAM+0x68,W+0x1900)
    def code(self,u,pc,n,ctx):
        if self.ready:
            if pc==0x4016ede0:self.ret(LEDMGR);return
            if pc==0x4016e804:self.ret(APP);return # application, for 400c34d6
            if pc==0x40008220:
                mgr,led,mode,palette=self.args(4);assert mgr==LEDMGR
                self.leds.setdefault(led,(mode,palette));self.ret();return
            if pc==0x4001c422:self.ret(PROXY);return
            # 40019000 and 40016062 execute: pattern APP+3394h+680h*index, its
            # lock model at +620h. Only the project and pattern index are doubles.
            if pc==0x4016e946:self.ret(W+0x1a00);return
            if pc==0x400c20a8:assert self.args(1)==(W+0x1a00,);self.ret(2);return
            if pc==0x40019036:assert self.args(1)==(APP,),hex(self.args(1)[0]);self.ret(MODEL);return
            if pc==0x40027f3a:self.ret(64);return
            if pc==0x4002787c:self.ret(1);return
            if pc==self.sym['lfo3_pattern_get']:
                model,track,step,slot=self.args(4);assert model==LOCKMODEL,hex(model)
                self.ret(self.step_locks.get((track,step,slot),0xffffffff));return
            if pc==self.sym['lfo3_pattern_set']:
                model,track,step,slot,value=self.args(5);assert model==LOCKMODEL,hex(model)
                if value==0xffffffff:self.step_locks.pop((track,step,slot),None)
                else:self.step_locks[track,step,slot]=value
                self.ret(1);return
        return super().code(u,pc,n,ctx)

def lock_editing(core,bank):
    m=LockViewMachine(core,bank)
    m.key(0x13,1,0);m.key(0x13,16,10);assert m.key(0x13,1,100)==1
    m.key(0x13,16,110)
    selection=W+0x1500;m.w(PARAM+0x6c,selection)
    # Grid REC (+168h, what REC toggles) without a held trig edits the base Sound.
    m.u.mem_write(selection+0x161,b'\x01');m.u.mem_write(selection+0x168,b'\x01')
    m.encoder(2,1);assert not m.step_locks
    base=m.read(m.sym['arp_ui_parameters'],24)
    # Held trigs: the native selection test (400c34d6) and 64-bit membership
    # execute. Neither live REC (+16Ah) nor the edit mark (+160h) is needed
    # before the first edit; the edit sets the mark as the native editor does.
    m.w(selection+0x158,1<<15);m.w(selection+0x15c,1<<31) # steps 48 and 32
    m.step_locks[0,47,0x61]=14
    m.encoder(2,2)
    assert m.step_locks[0,31,0x61]==12
    assert m.step_locks[0,47,0x61]==16 # relative to this step's existing lock
    assert m.read(selection+0x160,1)==b'\x01'
    m.encoder(1,1);m.encoder(3,1);m.encoder(4,3)
    assert m.step_locks[0,31,0x60]==1 and m.step_locks[0,47,0x62]==2
    assert m.step_locks[0,31,0x63]==33 and m.step_locks[0,47,0x63]==33
    for knob in (5,6,7):m.encoder(knob,3)
    assert len(m.step_locks)==8 # MODE, SPEED, RANGE and LEN only: no OFS, LEN (F) or G
    m.key(12,1,120);m.key(12,16,121);assert len(m.step_locks)==8 # YES locks nothing
    assert m.read(m.sym['arp_parameters'],24)==base
    m.rows=[];m.call('arp_view_draw',ROOT+16,BMP,0)
    assert not any(r[0]=='text' and str(r[-1]).startswith('LOCK') for r in m.rows) # no footer over the controls
    # Releasing trigs restores base values; subsequent knob movement is normal.
    m.u.mem_write(selection+0x160,b'\x00');m.w(selection+0x158,0);m.w(selection+0x15c,0)
    m.call('arp_view_tick',APP);assert m.read(m.sym['arp_ui_parameters'],24)==base
    m.encoder(2,1);assert m.read(m.sym['arp_parameters']+1,1)==b'\x0b'
    assert len(m.step_locks)==8

def step_editing(core,bank):
    m=LockViewMachine(core,bank)
    m.key(0x13,1,0);m.key(0x13,16,10);m.key(0x13,1,100);m.key(0x13,16,110)
    selection=W+0x1500;m.w(PARAM+0x6c,selection);m.u.mem_write(selection+0x161,b'\x01')
    p,view=m.sym['arp_parameters'],ROOT+16
    enabled=lambda:int.from_bytes(m.read(p+6,2),'big')
    # The firmware's own __dynamic_cast (View -> LedHandler, hint -2), as the
    # LED frame's view walk 40100856 calls it, finds the view's LedHandler.
    handler=m.call(0x401ba378,view,0x40241b9c,0x401c7104,0xfffffffe)
    assert handler==view+0x50
    def leds():
        m.leds={};m.call(m.word(m.word(handler)+8),handler,0,0);return m.leds
    assert leds()=={k:(2,9) for k in range(1,17)}
    # A held step key selects that step; any encoder edits its note (OFS);
    # its release after an edit leaves it enabled.
    assert m.key(27,1,200)==1 and m.word(m.sym['arp_selected'])==3
    assert m.encoder(1,5)==1 and m.read(p+8+3,1)==b'\x05' and m.read(p,1)==b'\x00'
    assert m.key(27,16,201)==1 and enabled()==0xffff
    # A tap toggles the step; two held steps both take an edit.
    m.key(27,1,202);m.key(27,16,203);assert enabled()==0xfff7
    m.key(24,1,204);m.key(25,1,205);m.encoder(3,-2);m.key(24,16,206);m.key(25,16,207)
    assert m.read(p+8,2)==b'\xfe\xfe' and enabled()==0xfff7
    # Past LEN the keys are dark and a tap changes nothing.
    m.encoder(6,-12);assert m.read(p+4,1)==b'\x03'
    assert leds()=={1:(2,9),2:(2,9),3:(2,9),4:(2,3),**{k:(1,2) for k in range(5,17)}}
    m.key(30,1,208);m.key(30,16,209);assert enabled()==0xfff7
    # REC (grid or live) leaves the keys and LEDs to the sequencer.
    for flag in (0x168,0x16a):
        m.u.mem_write(selection+flag,b'\x01')
        assert m.key(24,1,210)==0 and m.key(24,16,211)==0 and leds()=={}
        m.u.mem_write(selection+flag,b'\x00')
    # A press that went to the sequencer keeps its release there.
    m.u.mem_write(selection+0x168,b'\x01');assert m.key(26,1,212)==0
    m.u.mem_write(selection+0x168,b'\x00');assert m.key(26,16,213)==0 and enabled()==0xfff7
    return {'led_handler_dynamic_cast':'native 401ba378','leds':'active 9, inactive 3, past LEN unlit',
            'step_keys':['press selects','hold+encoder edits OFS','tap toggles','REC passes to the sequencer']}

def native_key_binding(core,bank):
    m=NativeKeyMachine(core,bank)
    # Derive all parameter-page keys from the native MainScreen switch.
    # The old 0x12 (18) does neither. This failed on TEST01's recognizer.
    m.stock_key(0x12);assert m.native_pages==[]
    for key in range(0x13,0x18):m.stock_key(key)
    assert m.native_pages==[0,1,2,3,4]
    keys=W+0x900;m.w(keys,m.sym['arp_view_key']);m.w(keys+4,0)
    m.w(m.cs['core:core_key_table']+2,keys)
    m.w(m.cs['core:events']+2*8,keys)
    assert not m.global_key(0x12,1,0) and not m.global_key(0x12,16,10)
    assert not m.global_key(0x12,1,20) and not m.global_key(0x12,16,30)
    assert not m.word(m.sym['arp_view']) and m.manager_calls==4
    assert not m.global_key(0x13,1,100) and not m.global_key(0x13,16,110)
    assert m.global_key(0x13,1,200)==1 and m.word(m.sym['arp_view'])==ROOT+16
    assert m.global_key(0x13,16,210)==1 and m.manager_calls==6
    return {'TRIG':19,'SYN':20,'FLTR':21,'AMP':22,'LFO':23,'old_wrong_key':18,'native_page_switch_executed':True,
            'global_core_key_dispatch_executed':True,'wrong_key_double_tap_rejected':True,
            'boundaries':['parameter-page selection','view-manager dispatch','view allocation/base/stack/raster as above']}

def parameter_page_handoff(core,bank):
    for arp_page in (0,1):
        for page_key in (0x14,0x15,0x16,0x17):
            m=NativeKeyMachine(core,bank)
            keys=W+0x900;m.w(keys,m.sym['arp_view_key']);m.w(keys+4,0)
            m.w(m.cs['core:core_key_table']+2,keys)
            m.w(m.cs['core:events']+2*8,keys)
            for key,flags,block in ((0x13,1,0),(0x13,16,10),(0x13,1,100),(0x13,16,110)):
                m.global_key(key,flags,block)
            assert m.word(m.sym['arp_view'])==ROOT+16
            if arp_page:
                assert m.global_key(0x13,1,200)==1
                assert m.global_key(0x13,16,210)==1
            assert m.word(m.sym['arp_page'])==arp_page
            before=m.manager_calls
            # The first page press removes ARP and reaches the native manager;
            # release and another press remain available for native gestures.
            assert m.global_key(page_key,1,300)==0
            assert m.closed==1 and m.word(ROOT+16+0x30)==1
            assert m.word(NODE+8)==MAIN and m.manager_calls==before+1
            assert m.global_key(page_key,16,310)==0
            assert m.global_key(page_key,1,320)==0
            assert m.manager_calls==before+3 and m.closed==1
            m.dispose();assert not m.word(m.sym['arp_view'])
    return {'arp_pages':[0,1],'native_page_keys':['SYN','FLTR','AMP','LFO'],
            'first_press_closes_before_native_dispatch':True,
            'release_and_second_press_reach_native_manager':True}

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

TRACKDATA,TVT=0x4901e000,0x4901f000
TDATA,TNOTIFY=RET+0x208,RET+0x20c

class ChordViewMachine(LockViewMachine):
    """1.2 chord page. The native track-model setters (4002755x..400282e8) and
    their notification descriptors execute; the active track model's data
    accessor (+28h) and notify (+10h) are explicit doubles."""
    def __init__(self,core,bank):
        super().__init__(core,bank);self.notices=[]
        self.w(MODEL,TVT);self.w(TVT+0x28,TDATA);self.w(TVT+0x10,TNOTIFY)
        self.u.mem_write(TDATA,b'\x4e\x75');self.u.mem_write(TNOTIFY,b'\x4e\x75')
        data=bytearray(0x499);data[0x2c0:0x3c0]=b'\xff'*256;data[0x3c5:0x3c9]=bytes([60,64,71,0])
        self.u.mem_write(TRACKDATA,bytes(data))
    def lane(self,n,step=None):return self.read(TRACKDATA+(0x3c5+n if step is None else 0x2c0+n*0x40+step),1)[0]
    def code(self,u,pc,n,ctx):
        if self.ready:
            if pc==0x400f68d2 and getattr(self,'no_extra_chord_travel',False) and self.args(5)[1]>=5:
                self.ret(0);return
            if pc==TDATA:assert self.args(1)==(MODEL,);self.ret(TRACKDATA);return
            if pc==TNOTIFY:
                track,notice=self.args(2);assert track==MODEL
                self.notices.append(self.word(notice));self.ret();return
            if pc==0x400f8104:self.rows.append(['shape',pc,*self.args(6)[1:]]);self.ret();return
        return super().code(u,pc,n,ctx)
    def texts(self):
        self.rows=[];self.call('arp_view_draw',ROOT+16,BMP,0)
        return [r[-1] for r in self.rows if r[0]=='text']

def chord_page(core,bank):
    m=ChordViewMachine(core,bank);page=lambda:m.word(m.sym['arp_page'])
    m.u.mem_write(SEQ,m.read(TRACKDATA,0x499))
    m.key(0x13,1,0);m.key(0x13,16,10);m.key(0x13,1,100);m.key(0x13,16,110)
    assert page()==0 and 'T1 ARPEGGIATOR' in m.texts()
    # TRIG on the open view switches page; press and release are both consumed.
    assert m.key(0x13,1,200)==1 and m.key(0x13,16,210)==1 and page()==1
    t=m.texts();assert t[:9]==['T1 ARP CHORD','C4','NOTE','OFF','NOT2','+7','NOT3','OFF','NOT4'],t
    keys=[r for r in m.rows if r[0]=='shape' and r[1]==0x400f8104]
    assert len(keys)==2,keys # C4 and G4, hatched
    # E..H edit the track defaults through the native setters; A..D do nothing.
    base=m.read(m.sym['arp_parameters'],24)
    for knob in (1,2,3,4):assert m.encoder(knob,3)==1
    assert m.read(m.sym['arp_parameters'],24)==base and not m.notices
    m.encoder(6,3);m.encoder(5,-1);m.encoder(8,-200)
    assert (m.lane(0),m.lane(1),m.lane(3))==(59,67,1),(m.lane(0),m.lane(1),m.lane(3))
    assert m.read(SEQ+0x3c6,1)==b'\x43' and m.read(SEQ+0x3c8,1)==b'\x01'
    assert m.notices==[0x401c690c]*3
    assert m.texts()[1:8:2]==['B3','+3','+7','-63'] # the lowest step is raw 1; 0 is OFF
    m.encoder(8,1);assert m.lane(3)==2 and m.texts()[7]=='-62'
    # The arp page's keys do nothing here; the trig keys stay the sequencer's.
    assert m.key(12,1,220)==1 and m.key(16,1,221)==1 and m.read(m.sym['arp_parameters'],24)==base
    assert m.key(24,1,230)==0 and m.key(24,16,231)==0
    # Held trigs (REC): each held step from its own value or the default.
    selection=W+0x1500;m.w(PARAM+0x6c,selection)
    m.u.mem_write(selection+0x161,b'\x01');m.u.mem_write(selection+0x168,b'\x01')
    m.w(selection+0x158,1<<15);m.w(selection+0x15c,1<<31) # steps 48 and 32
    m.u.mem_write(TRACKDATA+0x2c0+2*0x40+47,b'\x50')
    m.notices=[];m.encoder(7,2)
    assert (m.lane(2,31),m.lane(2,47),m.lane(2))==(73,82,71)
    assert m.read(SEQ+0x2c0+2*0x40+31,1)==b'\x49'
    assert m.read(SEQ+0x2c0+2*0x40+47,1)==b'\x52'
    assert m.notices==[0x401c68fc]*2 and m.read(selection+0x160,1)==b'\x01'
    # Hardware reported no effective native choices travel for F..H. Their
    # step edits still advance directly from the event's signed tick count.
    m.no_extra_chord_travel=True
    m.notices=[];m.encoder(6,1);m.encoder(8,1)
    got=(m.lane(1,31),m.lane(1,47),m.lane(3,31),m.lane(3,47))
    assert got==(68,68,3,3),got
    assert [m.read(SEQ+0x2c0+lane*0x40+step,1)[0]
            for lane in (1,2,3) for step in (31,47)]==[68,68,73,82,3,3]
    assert m.notices==[0x401c68fc]*4
    assert m.texts()[1:8:2]==['B3','+4','+9','-61']
    m.texts();assert ['shape',0x400f7df2,66,47,93,54,0xffffffff] in m.rows # NOT3 shown as the step's own
    # G's push: the held steps inherit NOT3 again. E's push leaves NOTE alone.
    m.u.mem_write(TRACKDATA+0x2c0+31,b'\x30');m.notices=[]
    assert m.key(46,1,240)==1 and m.lane(2,31)==255 and m.lane(2,47)==255 and m.notices==[0x401c68fc]*2
    assert m.read(SEQ+0x2c0+2*0x40+31,1)==b'\xff'
    assert m.read(SEQ+0x2c0+2*0x40+47,1)==b'\xff'
    assert m.key(44,1,241)==1 and m.lane(0,31)==0x30
    # Back to the arp page; a reopened view starts there.
    m.u.mem_write(selection+0x160,b'\x00');m.w(selection+0x158,0);m.w(selection+0x15c,0)
    assert m.key(0x13,1,300)==1 and m.key(0x13,16,301)==1 and page()==0
    m.key(0x13,1,310);m.key(0x13,16,311);assert page()==1
    assert m.key(13,1,320)==1;m.dispose();assert page()==0
    m.key(0x13,1,400);m.key(0x13,16,410);m.key(0x13,1,500);m.key(0x13,16,510)
    assert page()==0 and m.word(m.sym['arp_view'])
    return ['TRIG switches pages, both edges consumed','NOTE/NOT2..NOT4 drawn with the keyboard',
            'E..H through the native default setters; A..D, YES and arrows inert; trig keys pass',
            'held trigs: native step setters from each step value or default, selection marked, value inverted',
            'G push clears held steps; E push leaves NOTE','reopen starts on the arp page']

def encoder_travel(core,bank):
    """1.2: only whole values move; partial travel adds up in the same direction."""
    m=ViewMachine(core,bank);p=m.sym['arp_parameters']
    m.key(0x13,1,0);m.key(0x13,16,10);m.key(0x13,1,100);m.key(0x13,16,110)
    m.travel=100;mode=lambda:m.read(p,1)[0]
    assert mode()==0
    m.encoder(1,1);m.encoder(1,1);assert mode()==0  # 200/256 of a value
    m.encoder(1,1);assert mode()==1                 # 300: one value, 44 left
    m.encoder(1,-1);m.encoder(1,-1);assert mode()==1  # turning back drops the rest
    m.encoder(1,-1);assert mode()==0
    m.travel=256;m.encoder(2,1);assert m.read(p+1,1)[0]==10  # SPEED 9 -> 10
    m.travel=600;m.encoder(4,1);assert m.read(p+3,1)[0]==32  # LEN 30 -> 32, 88 left
    m.encoder(4,1);assert m.read(p+3,1)[0]==34
    m.configure(mode=0);m.dispose()
    return ['ticks x1 (x8 pressed) through the native choices table, every control',
            'whole values only; partial travel adds up, reversing drops it']

def verify(core,bank,donor,out):
    lock_editing(core,bank);steps=step_editing(core,bank);chord=chord_page(core,bank);travel=encoder_travel(core,bank)
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
    # The open page follows the selected track and edits its own Sound.
    m.track=1;m.call('arp_view_tick',APP);m.rows=[];m.call('arp_view_draw',ROOT+16,BMP,0)
    assert ['text',0x402a7354,64,59,2,'T2 ARPEGGIATOR'] in m.rows
    assert m.encoder(1,1)==1 and m.read(m.track_address('arp_parameters',1),1)==b'\x01'
    m.track=0;m.call('arp_view_tick',APP)
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
    binding=native_key_binding(core,bank);page_handoff=parameter_page_handoff(core,bank)
    result=dict(ok=True,fonts=fonts,key_binding=binding,parameter_page_handoff=page_handoff,step_editing=steps,chord_page=chord,encoder_travel=travel,rail_pixels_and_mask_equal=True,native_constructor_executed_for_rail=True,
        lifecycle=['double tap opens with MODE OFF','encoders A..F clamp','selected step/mute','modal priority',
                   'native held-trig selection; MODE/SPEED/RANGE/LEN locks only; per-step relative values; base unchanged; release restores base',
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
