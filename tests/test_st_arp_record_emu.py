"""Live ARP chord recording after the native one-note recorder writes a step.

The real UI note producer and ARP native dispatch execute. The native recorder's
step write and track-model lookup are explicit doubles; the ARP's fixup and
subsequent sequence playback execute linked target code. No device access.
"""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_arp_keyboard_emu import ProducerMachine
from test_st_arp_generator_emu import W,SEQ,KIT
from unicorn.m68k_const import UC_M68K_REG_A7

APP,PATTERN,MODEL,VT,DATA,REC=W+0x3000,W+0x5000,W+0x5070,W+0x7000,W+0x6000,W+0x4000
GET_DATA=0x4f000300

class RecordMachine(ProducerMachine):
    def __init__(self,core,bank):
        self.rec_step=0
        super().__init__(core,bank)
        self.w(0x444e13ec,REC);self.w(0x444e13f4,APP)
        self.u.mem_write(REC+0x169,b'\x01')
        self.w(MODEL,VT);self.w(VT+0x28,GET_DATA)
        self.u.mem_write(DATA+0x2c0,b'\xff'*256)
        self.u.mem_write(SEQ+0x2c0,b'\xff'*256)
    def code(self,u,pc,n,ctx):
        if pc==0x40019000:
            assert self.word(u.reg_read(UC_M68K_REG_A7)+4)==APP
            self.ret(PATTERN);return
        if pc==GET_DATA:
            assert self.word(u.reg_read(UC_M68K_REG_A7)+4)==MODEL
            self.ret(DATA);return
        if pc==0x4008da6e:
            out=self.word(u.reg_read(UC_M68K_REG_A7)+12)
            self.u.mem_write(out,bytes([self.rec_step,0,0,0]));self.ret();return
        return super().code(u,pc,n,ctx)
    def recorded(self,note):
        # What stock live recording writes after the UI note message: its
        # latest note becomes NOTE, the other lanes inherit the defaults.
        for lane,value in enumerate((note,255,255,255)):
            self.u.mem_write(DATA+0x2c0+lane*0x40+self.rec_step,bytes([value]))
        self.call('arp_view_tick',0)
    def lanes(self,base):
        return [self.read(base+0x2c0+lane*0x40+self.rec_step,1)[0] for lane in range(4)]

def verify(core,bank,out):
    m=RecordMachine(core,bank);initial=m.pool_counts()
    assert m.configure(mode=1,range_=2)
    for note,expected in ((60,[60,255,255,255]),
                          (64,[60,68,64,64]),
                          (67,[60,68,71,64])):
        m.key(note)
        if note==64:
            # The tick may precede the stock recorder's queued write.
            m.call('arp_view_tick',0)
            assert m.lanes(DATA)==[60,255,255,255]
        m.recorded(note)
        assert m.lanes(DATA)==expected,(note,m.lanes(DATA))
        if note!=60:assert m.lanes(SEQ)==expected
    assert m.read(m.sym['arp_rec_chords']+10,1)==b'\x03'
    for note in (67,64,60):m.key(note,2)
    assert m.read(m.sym['arp_rec_chords']+10,1)==b'\x03'
    assert not m.word(m.sym['arp_live'])
    e,_=m.event(note=60,length=0)
    m.u.mem_write(W+0x200,struct.pack('>6I',0,SEQ,KIT,0,e,0))
    assert m.call('arp_native',0,W+0x200,W+0x240)==0
    for t in (0,5400000,10800000,16200000):m.frame(t)
    assert m.pitches()==[60,64,67,72],m.log
    assert m.configure(mode=0) and m.pool_counts()==initial
    # A later recorder step starts a new root; stopping REC clears the capture.
    n=RecordMachine(core,bank);assert n.configure(mode=1,range_=0)
    n.key(60);n.recorded(60);n.key(64);n.recorded(64)
    n.rec_step=1;n.key(67);n.recorded(67)
    assert n.lanes(DATA)==[67,255,255,255]
    assert n.read(n.sym['arp_rec_chords']+10,1)==b'\x01'
    n.u.mem_write(REC+0x169,b'\x00');n.call('arp_view_tick',0)
    assert n.read(n.sym['arp_rec_chords']+10,1)==b'\x00'
    for note in (67,64,60):n.key(note,2)
    assert n.pool_counts()==initial
    # A fifth key cannot fit in NOT2..NOT4, but the stock last-note write
    # still must be replaced by the captured four-note chord.
    z=RecordMachine(core,bank);assert z.configure(mode=1,range_=0)
    for note in (60,64,67,72,76):z.key(note);z.recorded(note)
    assert z.lanes(DATA)==[60,68,71,76] and z.lanes(SEQ)==[60,68,71,76]
    z.key(64,2);z.key(64);z.recorded(64)
    assert z.lanes(DATA)==[60,68,71,76]
    for note in (76,72,67,64,60):z.key(note,2)
    assert z.pool_counts()==initial
    out.write_text(json.dumps({'ok':True,'recorded_lanes':[60,68,71,64],
        'sequence_pitches':[60,64,67,72],
        'boundaries':['stock recorder step write','track model lookup'],
        'device_access':False},indent=2)+'\n')
    print('PASS: live chord C4/E4/G4 records C4 root, +4/+7 and plays all three')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('core','bank','out'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();verify(a.core,a.bank,a.out)
