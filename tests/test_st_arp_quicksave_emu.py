"""ARP 1.10.8 / LFO3 1.3.4: ARP settings through the native quick save.

FUNC+YES (4001a8d2) encodes pattern k into the save buffer 40607754 and
writes it to the project's Temp Area before it encodes Kit k (into
40600554); FUNC+NO reads the pattern into 40601b54, then the Kit into
405fe354 and decodes it. The disk round trip is a copy of the two buffers.
LFO3's real pattern-encode wrapper and the core's native bus run as linked;
the native pattern codec runs original target code. No filesystem/device.
"""
import argparse,json,struct,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_st_arp_persist_emu import Persistence,LIVE,SOUND,RECORD,KIT,KS,DEFAULT,C1

PATTERNS,PATTERN=0x40a39b34,0x2138c
SAVE_PATTERN,RELOAD_PATTERN=0x40607754,0x40601b54
SAVE_KIT,RELOAD_KIT=0x40600554,0x405fe354
TAIL=0x5ac0
KIT_ENCODE,KIT_DECODE=5,6

def pattern_image(m,address):
    # The same encodable pattern image as test_st_arp_persist_emu.py.
    m.u.mem_write(address,bytes(PATTERN))
    m.u.mem_write(address+0x3bc5,b'\xff'*(0x21360-0x3bc5))

def quick_save(m,k):
    kit=LIVE+k*KIT
    assert m.call('lfo3_pattern_encode',SAVE_PATTERN,PATTERNS+k*PATTERN,0)&255
    m.meta(KIT_ENCODE,SAVE_KIT,kit,0)

def disk_round_trip():
    return [(RELOAD_PATTERN,SAVE_PATTERN,0x5c00),(RELOAD_KIT,SAVE_KIT,0x1600)]

def quick_reload(m,k):
    for d,s,n in disk_round_trip():m.u.mem_write(d,m.read(s,n))
    m.meta(KIT_DECODE,LIVE+k*KIT,RELOAD_KIT,0)

def rows(m,k):return [m.settings(LIVE+k*KIT+0x2a+t*SOUND) for t in range(12)]

def verify(core,bank,out):
    m=Persistence(core,bank);cases=[]
    k=5;kit=LIVE+k*KIT
    for p in range(4,7):pattern_image(m,PATTERNS+p*PATTERN)
    saved=[m.put_settings(kit+0x2a+t*SOUND,mode=1+t%4,seed=t) for t in range(12)]
    quick_save(m,k)
    assert m.read(SAVE_PATTERN+TAIL,4)==b'APT1'
    # Edited after the save, then FUNC+NO: the saved settings come back.
    for t in range(12):m.put_settings(kit+0x2a+t*SOUND,mode=4,seed=15)
    quick_reload(m,k);assert rows(m,k)==saved
    cases.append('FUNC+YES then edits then FUNC+NO restores the 12 saved rows')
    # Cleared RAM (a restart) before FUNC+NO: the Temp Area alone carries them.
    m.fresh();m.call('lfo3_records_init');quick_reload(m,k);assert rows(m,k)==saved
    cases.append('restart between save and reload: restored from the pattern record alone')
    # The carrier is bound to its Kit: pattern 5's tail never loads Kit 6.
    other=[m.put_settings(LIVE+6*KIT+0x2a+t*SOUND,mode=2,seed=3) for t in range(12)]
    m.meta(KIT_DECODE,LIVE+6*KIT,RELOAD_KIT,0)
    assert rows(m,6)==[DEFAULT]*12,rows(m,6)
    # A damaged tail is no carrier.
    m.u.mem_write(SAVE_PATTERN+TAIL+40,b'\x00');quick_reload(m,k);assert rows(m,k)==[DEFAULT]*12
    cases.append('another Kit or a damaged carrier: defaults, as before 1.10.8')
    # All defaults leave the tail empty; the reload stays OFF.
    for t in range(12):m.u.mem_write(W100:=0x49010100,DEFAULT);m.call('arp_sound_store',kit+0x2a+t*SOUND,W100)
    m.u.mem_write(SAVE_PATTERN+TAIL,b'\xa5'*4);quick_save(m,k)
    assert m.read(SAVE_PATTERN+TAIL,4)==bytes(4);quick_reload(m,k);assert rows(m,k)==[DEFAULT]*12
    cases.append('default settings: empty tail, defaults on reload')
    # A project's pattern slot is ARP's APC1 carrier: a pattern encode into
    # a container leaves it to the Kit codec. A reload from the project
    # (APC1 in the reloaded pattern, bound to the reloaded Sound records)
    # restores the rows too.
    saved=[m.put_settings(kit+0x2a+t*SOUND,mode=1+(t+1)%4,seed=t+1) for t in range(12)]
    slot=C1+0x200+k*0x5c00;record=C1+KS+k*0x1600
    canary=b'\x5a'*320;m.u.mem_write(slot+TAIL,canary)
    assert m.call('lfo3_pattern_encode',slot,PATTERNS+k*PATTERN,0)&255
    assert m.read(slot+TAIL,320)==canary
    m.meta(KIT_ENCODE,record,kit,C1);assert m.read(slot+TAIL,4)==b'APC1'
    m.u.mem_write(RELOAD_PATTERN,m.read(slot,0x5c00));m.u.mem_write(RELOAD_KIT,m.read(record,0x1600))
    for t in range(12):m.put_settings(kit+0x2a+t*SOUND,mode=4,seed=15)
    m.meta(KIT_DECODE,kit,RELOAD_KIT,0);assert rows(m,k)==saved
    cases.append('container pattern encode leaves APC1 to the Kit codec; reload from the project restores')
    result=dict(ok=True,cases=cases,device_access=False,
        limits=['eMMC writes/reads are buffer copies; the native Kit codec, the save/reload '
                'routines and the panel are not run here.'])
    out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ['core','bank','out']:p.add_argument('--'+n,type=Path,required=True)
    a=p.parse_args();verify(a.core,a.bank,a.out)
