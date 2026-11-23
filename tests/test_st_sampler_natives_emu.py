"""SP machines 1.6.1: the selection getters the sample list relies on, run on
linked OS 1.42 MAIN (no doubles for the getters themselves).

The list needs "a trig is held" (its lock context) and "live REC" (it never
opens then). It used 400c2dd6 for the first and 400c3582 for the second;
on the firmware they are "a lock was edited during this hold" (+160h) and
"trigs held within the track length". This test executes 400c2dd6,
400c3582 and 400c3062 on a selection object and pins the addresses
browser.inc uses. Only the application/track/length getters that 400c3582
calls are explicit doubles. No device access.
"""
import argparse,json,re,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT),str(ROOT/'tests')]
from test_st_safe_boot_emu import Core

SELECTION,APP,TRACK=0x46000100,0x46000800,0x46000c00
HELD_IN_LENGTH,LOCK_EDITED,LIVE_REC=0x400c3582,0x400c2dd6,0x400c3062

class Natives(Core):
    def __init__(self,folder):
        self.length=16;super().__init__(folder)
    def code(self,u,pc,n,user):
        if pc==0x4016e9c8:self.ret(APP);return          # the application
        if pc==0x40019036:assert self.args(1)==(APP,);self.ret(TRACK);return   # its active track
        if pc==0x40027f3a:assert self.args(1)==(TRACK,);self.ret(self.length);return   # track length
        super().code(u,pc,n,user)
    def get(self,at):
        self.sym['core:selection_probe']=at
        return self.call('selection_probe',SELECTION)&255
    def selection(self,steps=(),lock_edited=0,active=0,grid=0,live=0):
        mask=0
        for s in steps:mask|=1<<s
        self.u.mem_write(SELECTION,bytes(0x170))
        # +15Ch holds steps 0..31, +158h steps 32..63 (as 400c2e40 reads them).
        self.w(SELECTION+0x15c,mask&0xffffffff);self.w(SELECTION+0x158,mask>>32)
        self.u.mem_write(SELECTION+0x160,bytes([lock_edited,active]))
        self.u.mem_write(SELECTION+0x168,bytes([grid]));self.u.mem_write(SELECTION+0x16a,bytes([live]))

def verify(folder,out):
    m=Natives(folder);cases=[]
    for steps,length,held in (((),16,0),((3,),16,1),((20,),16,0),((20,),32,1),((0,63),64,1)):
        for lock_edited in (0,1):
            m.length=length;m.selection(steps,lock_edited)
            assert bool(m.get(HELD_IN_LENGTH))==bool(held),(steps,length,lock_edited)
            assert m.get(LOCK_EDITED)==lock_edited,(steps,lock_edited)
    cases.append('400c3582: a trig held within the track length, whatever +160h says')
    cases.append('400c2dd6: +160h only (a lock edited during the hold), not whether a trig is held')
    for active,grid,live,rec in ((0,0,1,0),(1,0,0,0),(1,0,1,1),(1,1,0,0),(1,1,1,1)):
        m.selection((3,),0,active,grid,live)
        assert bool(m.get(LIVE_REC))==bool(rec),(active,grid,live)
        assert m.get(HELD_IN_LENGTH)               # a held trig is not live REC
    cases.append('400c3062: live REC (+161h and +16Ah), independent of held trigs and grid REC')
    source=(ROOT/'mods/samplers-st/browser.inc').read_text(encoding='utf-8')
    defined={n:int(v,16) for n,v in re.findall(r'#define SP_BROWSER_(TRIG|RECORD) \(\(u32 \(\*\)\(void \*\)\)0x([0-9a-f]+)u\)',source)}
    assert defined=={'TRIG':HELD_IN_LENGTH,'RECORD':LIVE_REC},defined
    cases.append('browser.inc: TRIG = 400c3582, RECORD = 400c3062')
    source=(ROOT/'elekloader/sdk/include/syntakt/subtype-picker.h').read_text(encoding='utf-8')
    defined={n:int(v,16) for n,v in re.findall(r'#define STP_(TRIG|RECORD) \(\(stp_u32 \(\*\)\(void \*\)\)0x([0-9a-f]+)u\)',source)}
    assert defined=={'TRIG':HELD_IN_LENGTH,'RECORD':LIVE_REC},defined
    cases.append('subtype-picker.h (MD KIT, SY MnM): STP_TRIG = 400c3582, STP_RECORD = 400c3062')
    result=dict(ok=True,cases=cases,device_access=False,
                limits=['The application, active track and length getters are doubles; the panel is not run.'])
    out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--core',type=Path,required=True);p.add_argument('--out',type=Path,required=True)
    a=p.parse_args();verify(a.core,a.out)
