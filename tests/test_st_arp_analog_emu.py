"""ARP on all audio tracks, with real analog-track native note effects.

Executes the stock track-to-voice lookup, pitch/transpose/clamp, gate storage,
trigger masks, gate expiry and release, plus UI producers and event pools.
Analog machine DSP/control translation and physical oscillators are not run.
"""
import argparse,hashlib,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_arp_keyboard_emu import ProducerMachine
from test_st_arp_generator_emu import W,KIT,FRAME,OWNER,OWNERMAP,LIST
from test_st_arp_view_emu import ViewMachine,ROOT,BMP
from unicorn.m68k_const import *

class AnalogMachine(ProducerMachine):
    def __init__(self,core,bank):
        self.native_effects=[];self.effect_event=None
        super().__init__(core,bank)
    def code(self,u,pc,n,ctx):
        if pc==0x400a4244 and self.phase=='effects':
            self.effect_event=list(struct.unpack('>20I',self.read(u.reg_read(UC_M68K_REG_A2),80)))
        if pc==0x400a3b76 and self.phase=='effects':
            e=self.effect_event;t=e[2]
            self.native_effects.append(dict(track=t,action=e[1],note=e[6],flags=e[9],
                pitch=self.word(0x800046b8+4*t),gate=self.word(0x80003084+4*t),
                trigger=u.reg_read(UC_M68K_REG_D5),gate_mask=u.reg_read(UC_M68K_REG_A5),
                release_mask=u.reg_read(UC_M68K_REG_D6)))
        return super().code(u,pc,n,ctx)

def costs(core,bank,count):
    m=ProducerMachine(core,bank);base=m.pool_counts()
    for t in range(count):assert m.configure(track=t);m.key(60+t,track=t)
    m.frame(0);m.w(0x800046ec,28800);m.count=0;m.call('arp_frame');idle=m.count
    m.w(0x800046ec,5400000);m.count=0;m.call('arp_frame');due=m.count;m.drain(5400000,5428800)
    for t in range(count):m.key(60+t,2,track=t)
    assert m.pool_counts()==base
    return dict(active_tracks=count,not_due=idle,due=due)

def off_note_cost(core,bank,t):
    m=ProducerMachine(core,bank)
    # Initialize the analog producer equally even for the digital-only bank.
    m.call(0x400b3574,0x40a38b34+t*0x499,0);m.call(0x40104fd8,t)
    base=m.pool_counts();m.count=0;m.key(60,track=t);press=m.count
    m.frame(0);m.count=0;m.key(60,2,track=t);release=m.count;m.drain(28800,57600)
    assert m.pool_counts()==base
    return dict(track=t+1,press=press,release=release)

def verify(core,bank,previous,out):
    cases=[];voice_map=[]
    for t in range(8,12):
        # Factory producer and full chord lifecycle on each analog track.
        m=AnalogMachine(core,bank);base=m.pool_counts();assert m.tracks==12
        voice=m.read(0x40253541+t,1)[0];assert voice==t-8;voice_map.append(voice)
        assert m.configure(track=t,range_=0,note_length=0)
        for note in [60,64,67]:m.key(note,track=t)
        for now in [0,5400000,10800000]:m.frame(now)
        assert m.pitches()==[60,64,67]
        assert [x['pitch'] for x in m.native_effects]==[n<<16 for n in [60,64,67]]
        assert all(x['trigger']==1<<t and x['gate_mask']==1<<t and x['gate']==253125 for x in m.native_effects)
        for note in [60,64,67]:m.key(note,2,track=t)
        assert m.pool_counts()==base and m.word(0x80003084+4*t)==1
        assert m.gate_tick()&(1<<t) and int.from_bytes(m.read(0x80003084+4*t,4),'big',signed=True)<=0
        cases.append('analog-%d-factory-chord-trigger-gate-release'%(t+1))
        # Repeated identical notes retrigger; finite gate expires between them.
        m=AnalogMachine(core,bank);base=m.pool_counts();assert m.configure(track=t,range_=0,note_length=0)
        m.key(60,track=t);m.frame(0)
        for i in range(17):assert not m.gate_tick()&(1<<t)
        assert m.gate_tick()&(1<<t) and int.from_bytes(m.read(0x80003084+4*t,4),'big',signed=True)<=0
        m.frame(5400000);assert m.pitches()==[60,60]
        assert m.native_effects[-1]['trigger']==m.native_effects[0]['trigger']==1<<t
        assert m.native_effects[-1]['gate_mask']==1<<t and m.word(0x80003084+4*t)==253125
        m.configure(track=t,mode=0);assert m.pool_counts()==base
        cases.append('analog-%d-same-note-retriggers-after-expiry'%(t+1))
        # A new note retriggers even while the previous long gate is high.
        m=AnalogMachine(core,bank);base=m.pool_counts();assert m.configure(track=t,range_=0,note_length=126)
        m.key(60,track=t);m.frame(0);long_gate=m.word(0x80003084+4*t)
        for i in range(10):assert not m.gate_tick()&(1<<t)
        assert m.word(0x80003084+4*t)<long_gate
        m.frame(5400000);assert m.pitches()==[60,60] and m.word(0x80003084+4*t)==long_gate
        assert m.native_effects[-1]['trigger']==1<<t and m.native_effects[-1]['gate_mask']==1<<t
        m.key(60,2,track=t);assert m.pool_counts()==base
        cases.append('analog-%d-same-note-retriggers-during-long-gate'%(t+1))
        # Native Sound transpose/clamping runs after the authored ARP selector.
        for note,offset,expected in [(60,12,72),(120,24,127),(2,-24,0)]:
            m=AnalogMachine(core,bank);assert m.configure(track=t,range_=0)
            m.u.mem_write(m.preview+0xa8,bytes([offset&255]));m.key(note,track=t);m.frame(0)
            assert m.native_effects[-1]['pitch']==expected<<16
            m.key(note,2,track=t)
        cases.append('analog-%d-native-transpose-and-clamp'%(t+1))
        # Sequence roots use the same native consumer on tracks 9..12.
        m=AnalogMachine(core,bank);base=m.pool_counts();assert m.configure(track=t,range_=1)
        m.event(track=t,flags=0x10781,length=10800000);m.frame(0);m.frame(5400000)
        assert m.pitches()==[60,72] and all(x['trigger']==1<<t for x in m.native_effects)
        m.configure(track=t,mode=0);assert m.pool_counts()==base
        cases.append('analog-%d-sequence-root'%(t+1))
        # Native analog note-on resets its voice state; note-off clears the
        # held gate through D6/80003124, not that note-on state reset.
        m=AnalogMachine(core,bank);base=m.pool_counts()
        at=0x80005160+voice*32;m.w(at,0x12345678);m.w(at+4,0x87654321)
        m.key(60,track=t);m.frame(0);assert m.word(at)==m.word(at+4)==0
        m.key(60,2,track=t);m.drain(28800,57600)
        assert m.native_effects[-1]['release_mask']==1<<t and not m.word(0x80003124)&(1<<t)
        assert m.pool_counts()==base
        cases.append('analog-%d-OFF-native-note-on-state-and-gate-release'%(t+1))
    # Chords on all twelve tracks simultaneously, one pooled template each.
    m=AnalogMachine(core,bank);base=m.pool_counts()
    for t in range(12):
        assert m.configure(track=t,range_=0)
        for note in [48+t,52+t,55+t]:m.key(note,track=t)
    assert m.word(m.sym['arp_running'])==0xfff
    for now in [0,5400000,10800000]:m.frame(now)
    for t in range(12):assert [x['pitch'] for x in m.log if x['track']==t]==[48+t,52+t,55+t]
    assert m.pool_counts()[0]==base[0]-12
    m.u.mem_write(W+0x140,struct.pack('>6I',0x41525031,0x53544f50,0,0,0,0));m.call(0x4011a722,W+0x140)
    assert not m.word(m.sym['arp_running']) and m.pool_counts()==base
    cases.append('twelve-simultaneous-chords-STOP-balanced-pools')
    # Analogs respect the same owner map and MIDI-mode refusal as digitals.
    m=AnalogMachine(core,bank);m.w(LIST+4,OWNER);m.w(LIST+8,0)
    m.call(m.cs['core:core_native_interest_set'],OWNER,1<<15)
    m.u.mem_write(OWNERMAP,bytes([*range(9),8,10,11]))
    assert not m.configure(track=8) and not m.configure(track=9)
    m.u.mem_write(OWNERMAP,bytes(range(12)));m.u.mem_write(KIT+0x1550,b'\x08\x00')
    assert not m.configure(track=11) and not m.configure(track=12)
    cases.append('analog-master-slave-MIDI-FX-refused')
    # Analog pages display correct track numbers and start with OFF.
    m=ViewMachine(core,bank)
    for t in range(8,12):
        m.track=t;now=t*1000;m.key(0x13,1,now);m.key(0x13,16,now+10)
        assert m.key(0x13,1,now+100)==1;m.key(0x13,16,now+110)
        assert m.read(m.sym['arp_ui_parameters'],1)==b'\0'
        m.rows=[];m.call('arp_view_draw',ROOT+16,BMP,0)
        assert ['text',0x402a7354,64,59,2,'T%d ARPEGGIATOR'%(t+1)] in m.rows
        assert m.encoder(1,1)==1;m.key(12,1,now+120);m.dispose()
    cases.append('analog-T9-T12-pages-MODE-OFF-edit-YES')
    measured=[costs(core,bank,n) for n in [0,1,4,8,9,10,11,12]]
    old=[costs(core,previous,n) for n in [0,1,4,8]]
    assert measured[:4]==old,'unused analog instances changed the frame callback cost'
    off_notes=[dict(current=off_note_cost(core,bank,t),previous=off_note_cost(core,previous,t)) for t in range(8,12)]
    result=dict(ok=True,cases=cases,native_analog_voice_map=voice_map,
        callback_instructions=measured,previous_TEST05=old,ARP_OFF_UI_note_instructions=off_notes,
        bank_sha256=hashlib.sha256(bank.read_bytes()).hexdigest(),device_access=False,
        limits=['Native UI producer, shared pitch/gate/trigger masks and expiry execute original code.',
                'Sound publication, smoothing and lock application are explicit fixture boundaries.',
                'No analog machine control translation, SPI/DAC or physical audio/CPU measurement.'])
    out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['core','bank','previous-bank','out']:p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();verify(a.core,a.bank,a.previous_bank,a.out)
