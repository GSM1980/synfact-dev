"""Standalone mono ARP fed by real OS 1.41 held-key/priority instructions.

No POLY initialization. Sound lookup/publication, smoothing and lock-apply
are explicit boundaries. Executes native held set, note priority, pool/queue,
core IMMEDIATE and linked ARP; checks pitch/gate, not synthesis/device audio.
"""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_arp_generator_emu import Machine,W,KIT,SEQ,LIST
from unicorn.m68k_const import UC_M68K_REG_A7

KEY=W+0x800
class KeyboardMachine(Machine):
    def __init__(self,core,bank):
        super().__init__(core,bank)
        self.w(LIST+4,0) # Only ARP; even the owner-map double is absent.
        self.call(self.cs['core:core_native_initialize'])
        self.withheld=0;self.fail_template=False
    def code(self,u,pc,n,ctx):
        if pc==0x400a3548:
            track=self.word(u.reg_read(UC_M68K_REG_A7)+4)
            self.ret(KIT+0x2a+track*0x123);return # Sound lookup boundary
        if pc==self.sym.get('arp_native') and getattr(self,'fail_template',False):
            sp=u.reg_read(UC_M68K_REG_A7)
            if self.word(sp+4)==2:
                event=self.word(self.word(sp+8))
                if self.word(event)!=0x41525031:
                    self.withheld=self.word(0x444de440);self.w(0x444de440,0);self.fail_template=False
        return super().code(u,pc,n,ctx)
    def key(self,note,on=1,track=0,flags=0x10601):
        a=[track,note,100,on,flags]+[0]*10
        self.u.mem_write(KEY,struct.pack('>15I',*a))
        self.call(0x40119684,KEY)
    def pitches(self):return [x['pitch'] for x in self.log]
    def advance(self):
        # Normal internal-clock advance, independent of the sequencer's PLAY.
        before=self.word(0x800046ec);self.phase='call'
        self.u.emu_start(0x400a5398,0x400a53b6,count=100)
        assert self.word(0x800046ec)==(before+28800)&0xffffffff

class ProducerMachine(KeyboardMachine):
    """Construct live events from the real native UI producer, not an event
    fixture. Factory TRIG defaults and original note-on/note-off builders run.
    MIDI bookkeeping, record-message delivery and position are explicit doubles.
    """
    def __init__(self,core,bank):
        super().__init__(core,bank);self.preview=W+0xc00;self.inputs=[]
        self.w(0x444842d0,self.preview);self.w(0x402d575c,0xffffffff)
        for track in range(self.tracks):
            self.call(0x400b3574,0x40a38b34+track*0x499,0)
            self.call(0x40104fd8,track)
        self.w(0x800030bc,KIT+0xdce);self.w(0x80003120,KIT)
        self.u.mem_write(self.preview,bytes(0x123))
    def code(self,u,pc,n,ctx):
        if pc in [0x40109efa,0x40109f6c,0x4008b7ce,0x401041c8]:self.ret();return
        if pc==0x4008da6e:
            self.u.mem_write(self.word(u.reg_read(UC_M68K_REG_A7)+12),bytes(4));self.ret();return
        if pc==self.sym.get('arp_native') and hasattr(self,'inputs'):
            sp=u.reg_read(UC_M68K_REG_A7)
            if self.word(sp+4)==2:
                e=self.word(self.word(sp+8))
                if self.word(e)!=0x41525031:self.inputs.append(list(struct.unpack('>20I',self.read(e,80))))
        return super().code(u,pc,n,ctx)
    def key(self,note,on=1,track=0,flags=None):
        assert flags is None
        if on==1:self.call(0x40104c02,track,note,100,64,0,0xffffffff,0xffffffff,0xffffffff)
        else:self.call(0x4010502c,track,note,64)

def verify(core,bank,out):
    results=[]
    for mode,expected in [(1,[67,60,64,67]),(2,[60,64,67,60]),(3,[67,64,60,67])]:
        m=KeyboardMachine(core,bank);pools=m.pool_counts();m.w(0x43bd73bc,2)
        assert m.configure(mode=mode,range_=0)
        for n in [67,60,64]:m.key(n)
        assert m.word(m.sym['arp_live'])==1 and not m.word(0x444de434),'input chord sounded directly'
        for t in [0,5400000,10800000,16200000]:m.frame(t)
        assert m.pitches()==expected,(mode,m.log)
        # Each output still targets track 0; other pitches/gates stay untouched.
        assert all(m.word(0x800046b8+4*t)==0 and m.word(0x80003084+4*t)==0 for t in range(1,12))
        for n in [60,67,64]:m.key(n,2)
        m.frame(16228800);assert m.pool_counts()==pools and not m.word(m.sym['arp_live'])
        results.append({'mode':mode,'pitches':expected,'pools_balanced':True})
    # A release suppressed by native mono priority must still remove that key.
    m=KeyboardMachine(core,bank);pools=m.pool_counts();m.configure(range_=0)
    for n in [60,64,67]:m.key(n)
    m.frame(0);assert m.pitches()==[60]
    m.key(64,2);m.frame(5400000);assert m.pitches()==[60,67],m.log
    m.key(72);m.frame(10800000);m.frame(16200000)
    assert m.pitches()==[60,67,72,60],m.log
    m.key(60,2);m.key(67,2);m.key(72,2);m.frame(16228800)
    assert m.pool_counts()==pools
    results.append({'case':'held-set-edits-and-non-current-release','pitches':[60,67,72,60]})
    # OFF/STOP release only the mono track, retaining no template/lock refs.
    for action in ['off','stop','kit','pattern']:
        m=KeyboardMachine(core,bank);pools=m.pool_counts();m.configure();m.key(60);m.frame(0)
        if action=='off':assert m.configure(mode=0)
        elif action=='stop':
            m.u.mem_write(W+0x140,struct.pack('>6I',0x41525031,0x53544f50,0,0,0,0))
            m.call(0x4011a722,W+0x140)
        else:m.w(0x43bd743c if action=='kit' else 0x43bd7430,(KIT if action=='kit' else SEQ)+0x30000);m.frame(28800)
        assert m.pool_counts()==pools and m.word(0x80003084)==1 and not m.word(m.sym['arp_live'])
        results.append({'case':action,'pools_balanced':True})
    # A changed set with a cyclic native list is bounded and stops the ARP.
    m=KeyboardMachine(core,bank);pools=m.pool_counts();m.configure();m.key(60);m.frame(0)
    base=0x4553f6e0;node=m.word(base+0x410);m.w(node+4,node)
    m.w(base+4,m.word(base+4)|(1<<29))
    m.frame(28800);assert m.pool_counts()==pools and not m.word(m.sym['arp_live'])
    results.append({'case':'corrupt-held-list-stops-bounded'})
    # Default OFF keeps native keyboard playback, with no retained template.
    m=KeyboardMachine(core,bank);pools=m.pool_counts();m.key(60);m.frame(0)
    assert m.pitches()==[60] and not m.word(m.sym['arp_live']) and m.pool_counts()==pools
    results.append({'case':'OFF-native-input-passthrough'})
    # Every independent digital track remains a mono output, without a group.
    for track in range(8):
        m=KeyboardMachine(core,bank);pools=m.pool_counts();assert m.configure(track=track,range_=0)
        m.key(60,track=track);m.key(64,track=track);m.key(67,track=track)
        for t in [0,5400000,10800000]:m.frame(t)
        assert m.pitches()==[60,64,67]
        for other in range(12):
            if other!=track:assert not m.word(0x800046b8+4*other) and not m.word(0x80003084+4*other)
        for n in [60,64,67]:m.key(n,2,track=track)
        m.frame(10828800);assert m.pool_counts()==pools
    results.append({'case':'eight-independent-digital-tracks-no-POLY'})
    # Real allocator returns empty at template acquisition: original plays.
    m=KeyboardMachine(core,bank);pools=m.pool_counts();m.configure();m.fail_template=True;m.key(60)
    assert m.withheld and not m.word(0x444de440)
    m.w(0x444de440,m.withheld);m.frame(0)
    assert m.pitches()==[60] and not m.word(m.sym['arp_live']) and m.pool_counts()==pools
    results.append({'case':'template-pool-empty-native-fallback'})
    # Output bucket pressure is bounded; keep the chord, resume next division.
    m=KeyboardMachine(core,bank);pools=m.pool_counts();m.configure(range_=0);m.key(60);m.key(64)
    held=m.word(0x444de43c);m.w(0x444de43c,0);m.frame(0);assert not m.log
    m.w(0x444de43c,held);m.frame(5400000);assert m.pitches()==[64]
    m.key(60,2);m.key(64,2);m.frame(5428800);assert m.pool_counts()==pools
    results.append({'case':'output-bucket-full-next-division-recovery'})
    m=KeyboardMachine(core,bank);pools=m.pool_counts();m.w(0x43bd73bc,2);m.configure(range_=0)
    for n in [60,64,67]:m.key(n)
    while m.word(0x800046ec)<10828800:
        m.frame(m.word(0x800046ec));m.advance()
    assert m.pitches()==[60,64,67] and [x['time'] for x in m.log]==[0,5400000,10800000]
    for n in [60,64,67]:m.key(n,2)
    m.frame(m.word(0x800046ec));assert m.pool_counts()==pools
    results.append({'case':'stopped-transport-native-internal-clock-advance'})
    m=KeyboardMachine(core,bank);pools=m.pool_counts();m.configure(range_=0)
    for n in range(128):m.key(n)
    m.frame(0);assert m.pitches()==[0] and m.word(m.sym['arp_voice']+144)==128
    for n in range(127):m.key(n,2)
    m.frame(5400000);assert m.pitches()==[0,127]
    m.key(127,2);m.frame(5428800);assert m.pool_counts()==pools
    results.append({'case':'all-128-notes-four-bitmaps-bounded-removal'})
    m=KeyboardMachine(core,bank);pools=m.pool_counts();m.configure(range_=0);m.key(60);m.key(64)
    m.frame(0);generation=m.word(m.sym['arp_generation'])
    assert m.configure(range_=0,note_length=0) and m.word(m.sym['arp_generation'])==generation
    m.event(note=72,when=28800);m.frame(28800);assert m.pitches()==[60]
    m.frame(5400000);assert m.pitches()==[60,64] and m.log[-1]['gate']==253125
    m.key(60,2);m.key(64,2);m.frame(5428800);assert m.pool_counts()==pools
    results.append({'case':'live-LEN-edit-retains-chord-and-sequence-root-yields'})
    m=KeyboardMachine(core,bank);m.configure(range_=0)
    for n in [60,64,67]:m.key(n)
    m.frame(0);m.w(0x800046ec,28800);m.count=0;m.call('arp_frame');idle=m.count
    m.w(0x800046ec,5400000);m.count=0;m.call('arp_frame');due=m.count
    m.drain(5400000,5428800)
    for n in [60,64,67]:m.key(n,2)
    report={'ok':True,'standalone':True,'poly_initialized':False,'output_track_count':1,
            'cases':results,'boundaries':['Sound lookup/publication','smoothing','lock apply'],
            'frame_callback_instructions':{'held_set_unchanged_not_due':idle,'due_with_native_pool_copy_queue':due},
            'device_access':False,'synthesis_executed':False}
    out.parent.mkdir(parents=True,exist_ok=True);out.write_text(json.dumps(report,indent=2)+'\n')
    print('PASS: native held keys/priority -> standalone mono ARP -> native pitch/gate; no POLY; '+str(len(results))+' cases')
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for k in ['core','bank','out']:p.add_argument('--'+k,type=Path,required=True)
    a=p.parse_args();verify(a.core,a.bank,a.out)
