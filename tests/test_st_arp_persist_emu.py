"""Linked Sound metadata lifecycle, shared LFO3 adapters and native codecs.

No filesystem/device access. Mutex/synthesis boundaries are the existing
generator fixture's doubles. Native Sound/Kit/pattern codecs and memcpy run
original target code; metadata notifications run through the actual core bus.
"""
import argparse,hashlib,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_arp_generator_emu import Machine,W,BANK
from test_st_arp_keyboard_emu import ProducerMachine
from elekloader.sdk import elf

C1,C2=0x4a000000,0x4a400000
LIVE,POOL=0x41ad5134,0x41b93ec9
SOUND,RECORD,KIT,KS,PS=0x123,0x107,0x1618,0x2e0200,0x390204
DEFAULT=struct.pack('>5BxH16b',0,9,1,30,15,0xffff,*([0]*16))

class Persistence(Machine):
    def __init__(self,core,bank):
        super().__init__(core,bank);self.call_sr=0x2000;self.call_budget=20000000
        self.u.mem_map(C1,0x800000)
        e=elf.Elf.load(bank.parent/(bank.stem+'.work')/'main.elf')
        index=next(i for i,s in enumerate(e.symbols) if s.name=='lfo3_tick')
        host=next(s for s in e.symbols[index:] if s.name=='host')
        self.w(BANK+host.value,self.word(self.sym['arp_host']))
        for name,value in [('image_container',C1),('kits_base',LIVE),('pool_base',POOL)]:self.w(self.sym[name],value)
        self.call('lfo3_records_init')
    def meta(self,kind,a,b=0,n=0):self.call('lfo3_meta_notify',kind,a,b,n)
    def put_settings(self,a,mode=2,seed=0):
        p=struct.pack('>5BxH16b',mode,(9+seed)%18,seed%4,30+seed,15,0xffff^(1<<seed%16),*([seed-8]*16))
        self.u.mem_write(W+0x100,p);assert self.call('arp_sound_store',a,W+0x100)==1
        assert self.settings(a)==p;return p
    def settings(self,a):return self.read(self.call('arp_sound_settings',a),24)
    def carrier(self,container,k):return container+0x200+k*0x5c00+0x5ac0
    def copy(self,d,s,n):self.call('lfo3_memcpy_entry',d,s,n)
    def fresh(self):self.call('arp_persist_init')

def verify(core,bank,out):
    m=Persistence(core,bank);cases=[]
    # Identical synthesis bytes must retain independent Sound-owned settings.
    a,b=LIVE+0x2a,LIVE+0x2a+SOUND
    p=m.put_settings(a,seed=1);q=m.put_settings(b,mode=3,seed=2)
    assert m.read(a,SOUND)==m.read(b,SOUND) and m.settings(a)!=m.settings(b)
    m.copy(W+0x4000,a,SOUND);assert m.settings(W+0x4000)==p
    m.copy(b,W+0x4000,SOUND);assert m.settings(b)==p
    m.meta(2,b,0,SOUND);assert m.settings(b)==DEFAULT
    m.meta(3,W+0x4000,0,SOUND);assert m.settings(W+0x4000)==DEFAULT
    cases.append('identical-Sounds-independent; clipboard copy; constructor; free')
    # Original native codecs: storage slots may not overwrite carrier bytes.
    kit=LIVE+3*KIT
    m.call(0x400b330c,kit)
    values=[m.put_settings(kit+0x2a+t*SOUND,mode=1+t%4,seed=t) for t in range(12)]
    lfo_value=struct.pack('>8H',0x4400,0x300,0x4000,0,0x100,0,0,0x2000)
    lfo_row=m.sym['kit_shadow']+3*13*16+2*16
    m.u.mem_write(lfo_row,lfo_value)
    rec=C1+KS+3*0x1600
    canary=bytes([0xa5])*320;m.u.mem_write(m.carrier(C1,3),canary)
    assert m.call(0x400a787c,rec,kit,0)&255
    assert m.read(m.carrier(C1,3),320)==canary
    shared_pool=m.put_settings(POOL+3*SOUND,mode=4,seed=4)
    shared_record=C1+PS+3*RECORD
    assert m.call(0x400a7366,shared_record,POOL+3*SOUND,0)&255
    m.call('lfo3_sound_encoded',shared_record,POOL+3*SOUND,1)
    m.call('lfo3_kit_encoded',rec,kit,1)
    carrier=m.read(m.carrier(C1,3),320);assert carrier[:4]==b'APC1'
    lfo_tail=m.read(rec+0x14e0,0x120)
    pattern=C1+0x3c0000;slot=C1+0x200+3*0x5c00
    m.u.mem_write(pattern,bytes(0x2138c))
    m.u.mem_write(pattern+0x3bc5,b'\xff'*(0x21360-0x3bc5))
    assert m.call(0x400a81ec,slot,pattern,0)&255
    assert m.read(m.carrier(C1,3),320)==carrier
    assert m.call('lfo3_pattern_encode',slot,pattern,0)&255
    assert m.read(m.carrier(C1,3),320)==carrier
    # Preserve bytes through a native-size project snapshot and simulated reboot.
    m.w(C1,0xbeefbace);m.w(C1+4,13);m.w(C1+0x3a5600,0xbacef00c)
    # Register the second full container, as a project A/B destination.
    m.w(m.sym['containers'],C2)
    m.copy(C2,C1,0x3a5604)
    assert m.read(m.carrier(C2,3),320)==carrier
    assert m.read(C2+0x3a6000,4)==b'LP\x02\x80'
    m.fresh();m.call('lfo3_records_init')
    copied_rec=C2+KS+3*0x1600
    assert m.call(0x400a7828,kit,copied_rec)&255
    m.call('lfo3_kit_decoded',kit,copied_rec,1)
    assert [m.settings(kit+0x2a+t*SOUND) for t in range(12)]==values
    m.call('lfo3_sound_decoded',POOL+3*SOUND,C2+PS+3*RECORD,1)
    assert m.settings(POOL+3*SOUND)==shared_pool
    assert m.read(lfo_row,16)==lfo_value
    assert m.read(rec+0x14e0,0x120)==lfo_tail
    cases.append('native Kit codec; 12 Sound settings; A/B snapshot; cleared RAM; LFO3 tail untouched')
    # Snapshot has no extra capacity requirement. Corrupt/absent payload = OFF.
    m.u.mem_write(m.carrier(C1,3)+24,b'\xff')
    m.call('lfo3_kit_decoded',kit,rec,1)
    assert all(m.settings(kit+0x2a+t*SOUND)==DEFAULT for t in range(12))
    m.u.mem_write(m.carrier(C1,3),carrier)
    m.call('lfo3_kit_decoded',kit,rec,1)
    cases.append('corrupt project carrier resets OFF; valid carrier restores')
    # Native save/reload Kit buffers need metadata outside their short records.
    save,reload=0x405ff554,0x405fd354
    m.copy(save,rec,0x1600);m.copy(reload,save,0x1600)
    m.meta(6,W+0x6000,reload,0)
    assert [m.settings(W+0x6000+0x2a+t*SOUND) for t in range(12)]==values, [x.hex() for x in [m.settings(W+0x6000+0x2a),values[0],m.read(m.sym['encoded_kit'],32)]]
    # Incremental parameter refresh rewrites one Sound without whole-Kit codec.
    changed=m.put_settings(a,mode=4,seed=4)
    assert m.call(0x400a7366,save+0x2e+5*RECORD,a,0)&255
    m.call('lfo3_sound_encoded',save+0x2e+5*RECORD,a,1)
    m.meta(6,W+0x6000,save,0)
    expect=list(values);expect[5]=changed
    assert [m.settings(W+0x6000+0x2a+t*SOUND) for t in range(12)]==expect
    # A native encoded copy back into a project slot must bind its carrier,
    # rather than leave a stale destination carrier or a RAM-only association.
    m.copy(C1+KS+4*0x1600,save,0x1600)
    m.fresh();m.call('lfo3_kit_decoded',W+0x6000,C1+KS+4*0x1600,1)
    assert [m.settings(W+0x6000+0x2a+t*SOUND) for t in range(12)]==expect
    cases.append('save/reload Kit snapshots and incremental Sound update')
    # A short standalone encoded Sound exports only native bytes; ARP uses S1.
    assert m.put_settings(a,mode=4,seed=4)==changed
    encoded,scratch,dest=W+0x8000,W+0x9000,W+0xa000
    m.u.mem_write(encoded-16,b'\xa5'*(16+RECORD+16))
    assert m.call(0x400a7366,encoded,a,0)&255
    m.call('lfo3_sound_encoded',encoded,a,1)
    assert m.read(encoded-16,16)==b'\xa5'*16 and m.read(encoded+RECORD,16)==b'\xa5'*16
    m.u.mem_write(scratch-16,b'\xa5'*544)
    m.call('lfo3_s1_prepare',encoded,scratch)
    wire=m.read(scratch,0x13f)
    assert wire[0x11f:0x123]==b'ARP1'
    assert m.read(scratch-16,16)==b'\xa5'*16 and m.read(scratch+0x13f,512-0x13f+16)==b'\xa5'*(512-0x13f+16)
    m.fresh();m.u.mem_write(scratch,wire+bytes(512-len(wire)))
    m.call('lfo3_s1_loaded',encoded,1,scratch)
    assert m.call(0x400a6f7e,dest,encoded)&255
    m.call('lfo3_sound_decoded',dest,encoded,1);assert m.settings(dest)==changed
    # Legacy and corrupt Sound records replace stale active settings with OFF.
    for n in [0x107,0x11f]:
        m.u.mem_write(scratch,wire[:n]+bytes(512-n));m.call('lfo3_s1_loaded',encoded,1,scratch)
        m.call('lfo3_sound_decoded',dest,encoded,1);assert m.settings(dest)==DEFAULT
    cases.append('short native exports canaries; S1 save/load; legacy 107/11f OFF')
    # Pool Sound row survives clearing metadata just like the Kit rows.
    pool_value=m.put_settings(POOL+7*SOUND,mode=3,seed=7)
    pool_record=C1+PS+7*RECORD
    assert m.call(0x400a7366,pool_record,POOL+7*SOUND,0)&255
    m.call('lfo3_sound_encoded',pool_record,POOL+7*SOUND,1)
    m.fresh();m.call('lfo3_sound_decoded',POOL+7*SOUND,pool_record,1)
    assert m.settings(POOL+7*SOUND)==pool_value
    # Saving directly from the encoded pool must resolve its project carrier.
    m.call('lfo3_s1_prepare',pool_record,scratch);pool_wire=m.read(scratch,0x13f)
    m.u.mem_write(scratch,pool_wire+bytes(512-len(pool_wire)))
    m.call('lfo3_s1_loaded',encoded,1,scratch)
    m.copy(C1+PS+8*RECORD,encoded,RECORD)
    m.call('lfo3_sound_decoded',POOL+8*SOUND,C1+PS+8*RECORD,1)
    assert m.settings(POOL+8*SOUND)==pool_value
    m.fresh();m.call('lfo3_sound_decoded',POOL+8*SOUND,C1+PS+8*RECORD,1)
    assert m.settings(POOL+8*SOUND)==pool_value
    # Replacing just native bytes must not validate an unrelated old carrier.
    m.u.mem_write(pool_record+32,b'\x81')
    m.call('lfo3_sound_decoded',POOL+7*SOUND,pool_record,1)
    assert m.settings(POOL+7*SOUND)==DEFAULT
    m.call('lfo3_project_new',C1)
    assert m.read(m.carrier(C1,7),4)==bytes(4)
    cases.append('pool reboot; native record binding rejects stock import; NEW PROJECT')
    # Production restoration, with no private experiment configuration/page.
    native=ProducerMachine(core,bank);pools=native.pool_counts();sound=native.word(0x444842d0)
    native.u.mem_write(W+0x100,p)
    assert native.call('arp_sound_store',sound,W+0x100)==1
    for note in [60,64,67]:native.key(note)
    assert native.word(native.sym['arp_running'])==1
    for now in [0,5400000,10800000]:native.frame(now)
    assert len(native.log)>=2 and all(x['kind']==2 for x in native.log)
    # A reset/new OFF Sound stops a held chord without waiting for another key.
    native.u.mem_write(W+0x180,struct.pack('>4I',2,sound,0,SOUND))
    native.call(native.cs['core:core_native'],21,W+0x180)
    assert native.word(native.sym['arp_running'])==0 and native.read(native.sym['arp_parameters'],1)==b'\0'
    for note in [60,64,67]:native.key(note,2)
    native.frame(10828800);assert native.word(native.sym['arp_running'])==0 and native.pool_counts()==pools
    cases.append('Sound enables native UI chord ARP without page/configuration command')
    # Whole native serializer orders Kit and pool metadata after any pattern
    # encoding. Patch the exact two entries owned by LFO3, as install_hooks does.
    full=Persistence(core,bank);full.call_budget=100000000
    full.w(full.cs['core:core_parameter_providers'],full.sym['lfo3_parameter_query'])   # slot 0 (core 1.2)
    full.w(full.sym['lfo3_bank'],W+0xc000)
    for at,name in [(0x400a787c,'lfo3_kit_encode_entry'),(0x400a7828,'lfo3_kit_decode_entry')]:
        full.u.mem_write(at,b'\x4e\xf9'+struct.pack('>I',full.sym[name])+b'\x4e\x71')
    kit7=LIVE+7*KIT;full.call(0x400b330c,kit7)
    full_value=full.put_settings(kit7+0x2a+6*SOUND,mode=4,seed=6)
    full_pool=full.put_settings(POOL+11*SOUND,mode=3,seed=3)
    # 53h = Kit, pool, project header; native pattern records already cached.
    before_serialize=full.count
    assert full.call('lfo3_serialize_entry',C1,0x40a38b18,0,0x53,W+0xe000)&255
    serialize_instructions=full.count-before_serialize
    assert [i for i in range(128) if full.read(full.carrier(C1,i),4)==b'APC1']==[7,11]
    full.fresh()
    assert full.call(0x400a7828,kit7,C1+KS+7*0x1600)&255
    assert full.settings(kit7+0x2a+6*SOUND)==full_value
    assert full.call(0x400a6f7e,POOL+11*SOUND,C1+PS+11*RECORD)&255
    assert full.settings(POOL+11*SOUND)==full_pool
    full.u.mem_write(W+0x100,DEFAULT)
    assert full.call('arp_sound_store',kit7+0x2a+6*SOUND,W+0x100)==1
    assert full.call('arp_sound_store',POOL+11*SOUND,W+0x100)==1
    assert full.call('lfo3_serialize_entry',C1,0x40a38b18,0,0x53,W+0xe000)&255
    assert all(full.read(full.carrier(C1,i),4)!=b'APC1' for i in range(128))
    full.fresh()
    full.call('lfo3_kit_decoded',kit7,C1+KS+7*0x1600,1)
    full.call('lfo3_sound_decoded',POOL+11*SOUND,C1+PS+11*RECORD,1)
    assert all(full.settings(kit7+0x2a+t*SOUND)==DEFAULT for t in range(12))
    assert full.settings(POOL+11*SOUND)==DEFAULT
    cases.append('native whole-project Kit/pool serializer through shared adapters')
    # Pattern-owned ARP MODE/SPEED/RANGE/LEN and the native chord lanes must
    # survive an encoded slot, an A/B project snapshot and a fresh decode.
    locked=Persistence(core,bank)
    assert locked.call('lfo3_locks_setup')==1
    pattern=0x40a38b34;matrix=pattern+0x3bc5;encoded_pattern=C1+0x200
    locked.u.mem_write(pattern,bytes(0x2138c))
    locked.u.mem_write(matrix,b'\xff'*(0x21360-0x3bc5))
    for track in range(13):locked.u.mem_write(matrix+track*0x2447+0x2400,bytes(71))
    chord_lane=pattern+0x2c0+2*0x40+16
    locked.u.mem_write(chord_lane,b'\x46')  # step 17 NOT3=+6
    store=locked.call('lfo3_owned_lookup',matrix);assert store
    for param,value in ((8,1),(9,10),(10,2),(11,33)):
        assert locked.call('lfo3_owned_set',store,0,16,param,value,0)==1
    # Install the exact native compressor hook used by the production bank.
    locked.u.mem_write(0x400a811a,b'\x4e\xf9'+struct.pack('>I',locked.sym['lfo3_pattern_compress'])+b'\x4e\x71')
    assert locked.call('lfo3_pattern_encode',encoded_pattern,pattern,0)&255
    locked.w(C1,0xbeefbace);locked.w(C1+4,13);locked.w(C1+0x3a5600,0xbacef00c)
    locked.w(locked.sym['containers'],C2)
    locked.copy(C2,C1,0x3a5604)
    locked.call('lfo3_owned_clear',store)
    locked.u.mem_write(chord_lane,b'\xff')
    assert locked.call('lfo3_owned_pattern_decode',pattern,C2+0x200,0)&255
    assert [locked.call('lfo3_locks_get',store,0,16,param) for param in (8,9,10,11)]==[1,10,2,33]
    assert locked.read(chord_lane,1)==b'\x46'
    cases.append('ARP pattern locks and NOT3 step 17 survive encoded A/B project copy')
    result=dict(ok=True,cases=cases,serializer_emulated_instructions=serialize_instructions,device_access=False,
                core_sha256=hashlib.sha256(core.read_bytes()).hexdigest(),
                bank_sha256=hashlib.sha256(bank.read_bytes()).hexdigest(),
                limits=['Native IO is not exercised; no physical project save/reboot assertion.',
                        'Mutex and synthesis boundaries are doubles; full scheduler not modeled.'])
    out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['core','bank','out']:p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();verify(a.core,a.bank,a.out)
